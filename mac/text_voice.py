"""One watch recording, one text message to ANNA, and local speech playback."""
import asyncio
import re
import tempfile
import wave
from pathlib import Path

import av
import numpy as np

from call_control import chrome_control, chrome_unavailable
from native_voice import FRAME, RATE, MIN_SINGLE_PACKETS, play_audio


def prepare_speech(pcm):
    if not pcm:
        return pcm
    audio = np.frombuffer(pcm, dtype='<i2').astype(np.float32)
    peak = np.max(np.abs(audio))
    # Bring quiet synthesized replies up while retaining speaker headroom.
    if peak:
        audio *= min(2.0, 19000 / peak)
    fade_samples = min(RATE // 100, len(audio) // 2)
    if fade_samples:
        fade = np.linspace(0, 1, fade_samples, dtype=np.float32)
        audio[:fade_samples] *= fade
        audio[-fade_samples:] *= fade[::-1]
    return np.rint(audio).astype('<i2').tobytes()


async def run_local(*args, data=None, timeout=20):
    child = await asyncio.create_subprocess_exec(
        *args, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL)
    try:
        out, _ = await asyncio.wait_for(child.communicate(data), timeout)
        if child.returncode:
            raise RuntimeError('Mac 本地语音处理失败')
        return out
    finally:
        if child.returncode is None:
            child.kill()
            await child.wait()


class TextVoice:
    mode = 'single'

    def __init__(self, emit, transcriber):
        self.emit = emit
        self.transcriber = transcriber
        self.recording = bytearray()
        self.control = chrome_control()

    async def start(self):
        pass

    def audio(self, frame):
        if len(self.recording) < 750 * FRAME:
            self.recording.extend(frame)

    def release(self):
        return asyncio.create_task(self.reply())

    async def close(self):
        self.recording.clear()

    async def synthesize(self, text):
        voice = 'Tingting' if re.search(r'[\u3400-\u9fff]', text) else 'Samantha'
        with tempfile.TemporaryDirectory(prefix='anna-tts-') as directory:
            script = Path(directory) / 'reply.txt'
            audio = Path(directory) / 'reply.wav'
            script.write_text(text, encoding='utf-8')
            await run_local('/usr/bin/say', '-v', voice, '-r', '210', '-f', str(script), '-o', str(audio),
                            '--file-format=WAVE', f'--data-format=LEI16@{RATE}')
            with wave.open(str(audio), 'rb') as file:
                if (file.getnchannels(), file.getsampwidth(), file.getframerate()) == (1, 2, RATE):
                    return prepare_speech(file.readframes(file.getnframes()))
            with av.open(str(audio)) as source:
                resampler = av.AudioResampler(format='s16', layout='mono', rate=RATE)
                pcm = b''.join(out.to_ndarray().tobytes()
                               for frame in source.decode(audio=0) for out in resampler.resample(frame))
                return prepare_speech(pcm)

    async def reply(self):
        if len(self.recording) < MIN_SINGLE_PACKETS * FRAME:
            await self.emit('error', '录音太短，请按住黄键说完再松开')
            return
        if not self.control.ready():
            await self.emit('error', chrome_unavailable())
            return
        await self.emit('preparing')
        shown = progress = ''

        async def display(answer):
            nonlocal shown
            answer = answer.encode('utf-8')[:1800].decode('utf-8', errors='ignore').strip()
            if answer and answer != shown:
                shown = answer
                await self.emit('reply-view', answer)

        async def display_progress(answer):
            nonlocal progress
            progress = answer
            await display(answer)

        try:
            script = Path(__file__).with_name('native_transcribe.py')
            result = await run_local(self.transcriber, str(script), data=bytes(self.recording), timeout=25)
            prompt = result.strip().decode('utf-8', errors='ignore').strip()
            if not prompt:
                raise RuntimeError('没识别到文字，请再说一次')
            pending = asyncio.create_task(self.control.send(
                'text', 120, text=prompt, on_update=display_progress, on_preview=display,
                on_submitted=lambda: self.emit('submitted')))
            try:
                while not (await asyncio.wait({pending}, timeout=35))[0]:
                    await self.emit('preparing')
                answer = pending.result()
            finally:
                if not pending.done():
                    pending.cancel()
                    await asyncio.gather(pending, return_exceptions=True)
            if not isinstance(answer, str) or not answer.strip():
                raise RuntimeError('ANNA 暂未回复，请在 Chrome 查看')
        except TimeoutError:
            await self.emit('error', '文字处理超时，请重试')
            return
        except (RuntimeError, OSError) as exc:
            await self.emit('error', str(exc))
            return
        await display(answer)
        spoken = answer.removeprefix(progress).strip() or answer.strip()
        try:
            speech = await self.synthesize(spoken)
        except (RuntimeError, OSError, TimeoutError, ValueError, wave.Error, av.FFmpegError):
            speech = b''
        # Locally synthesized audio can be buffered further ahead than live speech.
        await play_audio(self.emit, speech, prefill_packets=75)
