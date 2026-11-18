"""Bridge a ChatGPT Dot call over scoped Core Audio devices."""
import asyncio
import ctypes as C
import queue
import subprocess
import time
from pathlib import Path

import av
import numpy as np

from codex_voice import FRAME, RATE
from call_control import CallControl, chrome_control, chrome_unavailable

APP_BUNDLES = {'com.openai.codex', 'com.openai.codex.helper'}
CHROME_BUNDLES = {'com.google.Chrome', 'com.google.Chrome.helper'}
BLACKHOLE = 'BlackHole2ch_UID'
HARDWARE_RATE = 48000
PACKET_SECONDS = FRAME / (RATE * 2)
MIN_SINGLE_PACKETS = 16
FIRST_REPLY_TIMEOUT = 23
MAX_SINGLE_REPLY_BYTES = RATE * 2 * 60
PLAYBACK_PREFILL_PACKETS = 25


async def play_audio(emit, pcm, *, prefill_packets=PLAYBACK_PREFILL_PACKETS):
    await emit('speaking')
    deadline = asyncio.get_running_loop().time()
    prefill = prefill_packets * FRAME
    for offset in range(0, len(pcm), FRAME):
        if offset >= prefill:
            deadline += PACKET_SECONDS
            await asyncio.sleep(max(0, deadline - asyncio.get_running_loop().time()))
        await emit('audio', bytes(pcm[offset:offset + FRAME]).ljust(FRAME, b'\0'))
    await emit('done')


class NativeVoice:
    def __init__(self, emit, mode, transcriber, *, chrome_calls=False):
        self.emit, self.mode, self.transcriber = emit, mode, transcriber
        self.tap = self.output = self.reader = None
        self.dial_tap = None
        self.uplink = queue.Queue(maxsize=750 if mode == 'single' else 50)
        self.downlink = queue.Queue(maxsize=128)
        self.allowed = self.released = self.started = self.speaking = self.closed = False
        self.release_error = ''
        self.reply_deadline = None
        self.output_buffer = b''
        self.transcript = bytearray()
        self.pids = []
        self.native = None
        self.chrome = chrome_calls and mode == 'call'
        self.call_control = None
        if mode == 'call':
            self.call_control = chrome_control() if self.chrome else CallControl()
        self.requested_dial = False

    def audio(self, frame):
        if self.speaking or self.released or self.closed:
            return
        try:
            self.uplink.put_nowait(frame)
        except queue.Full:
            if self.mode == 'call':
                try:
                    self.uplink.get_nowait()
                    self.uplink.put_nowait(frame)
                except (queue.Empty, queue.Full):
                    pass

    def release(self):
        packets = self.uplink.qsize()
        if packets < MIN_SINGLE_PACKETS:
            self.release_error = '录音太短，请按住黄键说完再松开'
            return
        self.reply_deadline = time.monotonic() + packets * PACKET_SECONDS + FIRST_REPLY_TIMEOUT
        self.released = True

    def render(self, data, frames, _time, _status):
        data[:] = bytes(len(data))
        if not self.allowed or self.speaking or (self.mode == 'single' and not self.released):
            self.output_buffer = b''
            return
        if not self.started and self.mode == 'call' and self.uplink.qsize() < 4:
            return
        while len(self.output_buffer) < len(data):
            try:
                packet = self.uplink.get_nowait()
            except queue.Empty:
                break
            samples = np.frombuffer(packet, dtype='<i2')
            if not self.started and np.max(np.abs(samples.astype(np.int32)), initial=0) > 200:
                self.started = True
            self.output_buffer += np.repeat(samples, 6).tobytes()  # 3x rate and stereo
        n = min(len(data), len(self.output_buffer))
        data[:n] = self.output_buffer[:n]
        self.output_buffer = self.output_buffer[n:]

    def capture(self, _device, _now, source, *_):
        from coreaudio_tap import OneBufferList
        if source and self.allowed:
            buffers = C.cast(source, C.POINTER(OneBufferList)).contents
            if buffers.count == 1:
                block = buffers.buffers[0]
                if block.channels == 1 and block.data and 0 < block.byte_size <= 65536 and not block.byte_size % 4:
                    try:
                        self.downlink.put_nowait(C.string_at(block.data, block.byte_size))
                    except queue.Full:
                        pass
        return 0

    def check_route(self):
        from process_input_check import check_process_inputs
        if not check_process_inputs(self.native, BLACKHOLE, self.pids)['ok']:
            raise RuntimeError('ChatGPT call is not listening on BlackHole 2ch')
        device = self.native.uint(1, 'dOut')
        if self.native.string(device, 'uid ') == BLACKHOLE:
            raise RuntimeError('BlackHole 2ch cannot be the default output')

    def active_processes(self):
        bundles = CHROME_BUNDLES if self.chrome else APP_BUNDLES
        return [p for p in self.native.list_processes()
                if p.get('bundle_id') in bundles and p.get('input_running')]

    def open_tap(self, pids):
        from coreaudio_tap import Tap, fourcc
        tap = Tap(self.native, pids, mute_while_capturing=True)
        try:
            tap.__enter__()
            fmt = tap.format()
            if (fmt.format_id != fourcc('lpcm') or not fmt.format_flags & 1
                    or fmt.format_flags & 2 or fmt.channels_per_frame != 1
                    or fmt.bits_per_channel != 32 or not 8000 <= fmt.sample_rate <= 192000):
                raise RuntimeError('Unsupported ChatGPT process tap audio format')
            tap.start_capture(self.capture)
            return tap, round(fmt.sample_rate)
        except BaseException:
            tap.close()
            raise

    async def start(self):
        import sounddevice as sd
        from coreaudio_tap import Native
        from hal_output import output_index, verify_hal
        self.native = Native()
        processes = self.active_processes()
        if self.chrome:
            if not self.call_control.ready():
                await self.emit('error', chrome_unavailable())
                return
            audio_processes = self.native.list_processes()
            if processes or any(p.get('bundle_id') in APP_BUNDLES and p.get('input_running')
                                for p in audio_processes):
                await self.emit('error', '请先结束 Mac 上已接通的通话')
                return
            chrome_audio = [p for p in audio_processes if p.get('bundle_id') in CHROME_BUNDLES]
            dial_pids = [p['pid'] for p in chrome_audio if p.get('output_running')]
            if not dial_pids:
                dial_pids = [p['pid'] for p in chrome_audio]
            if dial_pids:
                try:
                    self.dial_tap, dial_rate = self.open_tap(dial_pids)
                    print('Chrome output muted before dial', flush=True)
                except (RuntimeError, OSError) as exc:
                    print(f'Chrome dial mute unavailable: {exc}', flush=True)
        if not processes and self.call_control and self.call_control.ready():
            if not self.chrome and self.native.string(self.native.uint(1, 'dIn '), 'uid ') != BLACKHOLE:
                await self.emit('error', '请将 Mac 声音输入切到 BlackHole 2ch')
                return
            self.requested_dial = True
            try:
                await self.call_control.send('dial')
            except (RuntimeError, OSError) as exc:
                await self.emit('error', str(exc))
                return
            deadline = time.monotonic() + 16
            while time.monotonic() < deadline:
                processes = self.active_processes()
                if len(processes) == 1 and processes[0].get('output_running'):
                    self.pids = [processes[0]['pid']]
                    try:
                        self.check_route()
                    except RuntimeError:
                        pass
                    else:
                        break
                await asyncio.sleep(.25)
        if len(processes) != 1 or not processes[0].get('output_running'):
            await self.emit('error', 'Chrome 未接通 ANNA' if self.chrome else '请先在 Mac 的 ChatGPT 接通 ANNA')
            return
        self.pids = [processes[0]['pid']]
        try:
            self.check_route()
        except RuntimeError:
            message = '请将 Chrome 麦克风选为 BlackHole 2ch' if self.chrome else '请将 Mac 声音输入切到 BlackHole 2ch'
            await self.emit('error', message)
            return
        verify_hal(self.native, BLACKHOLE)
        index = output_index(sd.query_devices(), sd.query_hostapis())
        if self.dial_tap and list(self.dial_tap.pids) == self.pids:
            self.tap, sample_rate = self.dial_tap, dial_rate
            self.dial_tap = None
        else:
            self.tap, sample_rate = self.open_tap(self.pids)
            if self.dial_tap:
                self.dial_tap.close()
                self.dial_tap = None
        self.output = sd.RawOutputStream(device=index, channels=2, samplerate=HARDWARE_RATE,
                                         dtype='int16', blocksize=960, callback=self.render)
        self.output.start()
        self.allowed = True
        self.reader = asyncio.create_task(self.read_reply(sample_rate))
        await self.emit('connected')
        route = 'Chrome' if self.chrome else 'native'
        print(f'{route} Dot transport ready; mode={self.mode}', flush=True)

    async def transcribe(self):
        python = self.transcriber
        script = str(Path(__file__).with_name('native_transcribe.py'))
        child = await asyncio.create_subprocess_exec(
            python, script, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            out, _ = await asyncio.wait_for(child.communicate(bytes(self.transcript)), 25)
        finally:
            if child.returncode is None:
                child.kill()
                await child.wait()
        if child.returncode:
            return ''
        return out.decode().strip()

    async def play_single_reply(self):
        try:
            text = await self.transcribe()
        except (TimeoutError, OSError, UnicodeError):
            text = ''
        await self.emit('reply', text or '已收到语音，暂时无法转成文字。')
        await play_audio(self.emit, self.transcript)

    async def read_reply(self, sample_rate):
        resampler = av.AudioResampler(format='s16', layout='mono', rate=RATE)
        pending = bytearray()
        last_voice = next_check = 0.
        loop = asyncio.get_running_loop()
        try:
            while not self.closed:
                now = loop.time()
                if self.release_error:
                    await self.emit('error', self.release_error)
                    return
                if self.reply_deadline and not self.speaking and time.monotonic() >= self.reply_deadline:
                    await self.emit('error', 'ANNA 暂未回复，请再试一次')
                    return
                if now >= next_check:
                    self.check_route()
                    next_check = now + .5
                try:
                    block = self.downlink.get_nowait()
                except queue.Empty:
                    await asyncio.sleep(.008)
                    continue
                if not self.started:
                    continue
                samples = np.frombuffer(block, dtype='<f4')
                if self.mode == 'call':
                    # Raise normal speech and soften peaks before int16 conversion.
                    magnitude = np.abs(samples) * 1.20
                    softened = .72 + .18 * np.tanh((magnitude - .72) / .18)
                    samples = np.copysign(np.where(magnitude > .72, softened, magnitude), samples)
                frame = av.AudioFrame.from_ndarray(samples[None, :],
                                                   format='flt', layout='mono')
                frame.sample_rate = sample_rate
                for out in resampler.resample(frame):
                    pending.extend(out.to_ndarray().astype('<i2', copy=False).tobytes())
                while len(pending) >= FRAME:
                    pcm = bytes(pending[:FRAME])
                    del pending[:FRAME]
                    samples = np.frombuffer(pcm, dtype='<i2').astype(np.float32)
                    if np.sqrt(np.mean(samples * samples)) > (150 if self.mode == 'call' else 125):
                        last_voice = now
                        if not self.speaking:
                            self.speaking = True
                            while not self.uplink.empty():
                                try:
                                    self.uplink.get_nowait()
                                except queue.Empty:
                                    break
                            await self.emit('preparing' if self.mode == 'single' else 'speaking')
                    if self.speaking:
                        if self.mode == 'single':
                            self.transcript.extend(pcm)
                            if len(self.transcript) >= MAX_SINGLE_REPLY_BYTES:
                                break
                        else:
                            await self.emit('audio', pcm)
                if self.speaking and (now - last_voice > 1.05 or
                                      len(self.transcript) >= MAX_SINGLE_REPLY_BYTES):
                    if self.mode == 'single':
                        await self.play_single_reply()
                        return
                    await self.emit('listening')
                    self.started = self.speaking = False
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            self.allowed = False
            print(f'native Dot audio stopped: {type(exc).__name__}: {exc}', flush=True)
            await self.emit('error', 'Mac 上的 ANNA 通话音频已中断')

    async def close(self):
        self.closed = True
        self.allowed = False
        if self.reader:
            self.reader.cancel()
            await asyncio.gather(self.reader, return_exceptions=True)
        if self.output:
            self.output.abort()
            self.output.close()
        if self.tap and not self.chrome:
            self.tap.close()
        try:
            if self.requested_dial:
                await self.call_control.send('hangup')
                deadline = time.monotonic() + 3
                while self.active_processes() and time.monotonic() < deadline:
                    await asyncio.sleep(.25)
                if self.active_processes():
                    print('native Dot hangup not confirmed: ChatGPT input still active', flush=True)
                elif self.chrome:
                    # Chrome can play the hangup tone after releasing its microphone.
                    await asyncio.sleep(.65)
                    print(f'Chrome Dot hangup confirmed; mode={self.mode}', flush=True)
        except (RuntimeError, OSError) as exc:
            print(f'native Dot hangup not confirmed: {exc}', flush=True)
        finally:
            if self.tap and self.chrome:
                self.tap.close()
            if self.dial_tap:
                self.dial_tap.close()
