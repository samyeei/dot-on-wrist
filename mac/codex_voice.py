"""Headless Codex voice transport. The local Codex login is used by app-server."""
import asyncio
import json
import time
from fractions import Fraction

import av
import numpy as np
from aiortc import AudioStreamTrack, RTCPeerConnection, RTCSessionDescription

FRAME = 640
RATE = 16000

class Rpc:
    def __init__(self, executable):
        self.executable = executable
        self.pending = {}
        self.number = 0
        self.child = self.reader = None

    async def start(self, on_event):
        self.child = await asyncio.create_subprocess_exec(
            self.executable, 'app-server', '--listen', 'stdio://',
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL, limit=8 * 1024 * 1024)
        self.reader = asyncio.create_task(self.read(on_event))
        await self.request('initialize', {
            'clientInfo': {'name': 'anna_stopwatch', 'version': '0.1.0'},
            'capabilities': {'experimentalApi': True}})
        await self.send({'method': 'initialized', 'params': {}})

    async def send(self, packet):
        self.child.stdin.write((json.dumps(packet) + '\n').encode())
        await self.child.stdin.drain()

    async def request(self, method, params):
        self.number += 1
        request_id = self.number
        result = asyncio.get_running_loop().create_future()
        self.pending[request_id] = result
        try:
            await self.send({'id': request_id, 'method': method, 'params': params})
            return await asyncio.wait_for(result, 30)
        finally:
            self.pending.pop(request_id, None)

    async def read(self, on_event):
        try:
            while line := await self.child.stdout.readline():
                packet = json.loads(line)
                if 'method' in packet:
                    if 'id' in packet:
                        await self.send({'id': packet['id'], 'error': {
                            'code': -32601, 'message': 'Device session does not accept tool approvals'}})
                    else:
                        await on_event(packet['method'], packet.get('params', {}))
                else:
                    future = self.pending.get(packet.get('id'))
                    if future and not future.done():
                        if 'error' in packet:
                            future.set_exception(RuntimeError('Codex RPC rejected the request'))
                        else:
                            future.set_result(packet.get('result'))
        finally:
            for future in self.pending.values():
                if not future.done():
                    future.set_exception(RuntimeError('Codex RPC disconnected'))

    async def close(self):
        if self.child and self.child.returncode is None:
            self.child.terminate()
            try:
                await asyncio.wait_for(self.child.wait(), 3)
            except TimeoutError:
                self.child.kill()
                await self.child.wait()
        if self.reader:
            self.reader.cancel()
            await asyncio.gather(self.reader, return_exceptions=True)

class WatchUplink(AudioStreamTrack):
    def __init__(self, mode):
        super().__init__()
        self.mode = mode
        self.queue = asyncio.Queue(maxsize=750 if mode == 'single' else 75)
        self.released = False
        self.position = 0
        self.deadline = None
        self.playing = False

    async def recv(self):
        now = time.monotonic()
        self.deadline = now if self.deadline is None else max(self.deadline + .02, now - .04)
        await asyncio.sleep(max(0, self.deadline - now))
        raw = bytes(FRAME)
        if not self.playing and (self.mode == 'call' or self.released) and not self.queue.empty():
            raw = self.queue.get_nowait()
        frame = av.AudioFrame(format='s16', layout='mono', samples=320)
        frame.sample_rate = RATE
        frame.pts = self.position
        frame.time_base = Fraction(1, RATE)
        frame.planes[0].update(raw)
        self.position += 320
        return frame

class CodexVoice:
    def __init__(self, emit, mode, executable, cwd):
        self.mode, self.emit, self.cwd = mode, emit, cwd
        self.rpc = Rpc(executable)
        self.peer = RTCPeerConnection()
        self.uplink = WatchUplink(mode)
        self.peer.addTrack(self.uplink)
        self.peer.createDataChannel('oai-events')
        self.thread = None
        self.answered = asyncio.Event()
        self.connected = asyncio.Event()
        self.failure = None
        self.receiver = None
        self.finished = False
        self.closing = False

        @self.peer.on('connectionstatechange')
        async def state():
            if self.peer.connectionState == 'connected':
                self.connected.set()
            elif self.peer.connectionState == 'failed':
                self.failure = 'WebRTC connection failed'
                self.connected.set()

        @self.peer.on('track')
        def track(remote):
            if remote.kind == 'audio':
                self.receiver = asyncio.create_task(self.downlink(remote))

    async def start(self):
        await self.rpc.start(self.event)
        response = await self.rpc.request('thread/start', {
            'cwd': self.cwd, 'ephemeral': True, 'sandbox': 'read-only',
            'approvalPolicy': 'never',
            'developerInstructions': '这是手表语音通道的独立 Codex 会话。用简体中文简明回答。仅查询和解释，不执行任何写操作。'})
        self.thread = response['thread']['id']
        await self.peer.setLocalDescription(await self.peer.createOffer())
        await self.rpc.request('thread/realtime/start', {
            'threadId': self.thread, 'version': 'v3', 'voice': 'maple',
            'outputModality': 'audio', 'includeStartupContext': False,
            'transport': {'type': 'webrtc', 'sdp': self.peer.localDescription.sdp}})
        await self.answered.wait()
        if self.failure:
            raise RuntimeError(self.failure)
        await self.connected.wait()
        if self.failure:
            raise RuntimeError(self.failure)
        await self.emit('connected')

    async def event(self, method, params):
        if params.get('threadId') != self.thread:
            return
        if method == 'thread/realtime/sdp':
            try:
                await self.peer.setRemoteDescription(RTCSessionDescription(params['sdp'], 'answer'))
            except Exception:
                self.failure = 'Invalid media answer'
            self.answered.set()
        elif method in ('thread/realtime/error', 'thread/realtime/closed') and not self.closing:
            self.failure = 'Codex voice session ended'
            if self.connected.is_set():
                await self.emit('error', '通话已断开')
            self.answered.set()
            self.connected.set()
        elif method == 'thread/realtime/transcript/done' and params.get('role') == 'assistant':
            if not self.finished and (self.mode == 'call' or self.uplink.released):
                value = str(params.get('text') or '').strip()
                if value:
                    await self.emit('reply', value)

    def audio(self, frame):
        if self.uplink.playing or self.finished:
            return
        if self.uplink.queue.full():
            if self.mode == 'single':
                return
            self.uplink.queue.get_nowait()
        self.uplink.queue.put_nowait(frame)

    def release(self):
        self.uplink.released = True

    async def downlink(self, remote):
        resampler = av.AudioResampler(format='s16', layout='mono', rate=RATE)
        last_voice = 0.
        buffer = bytearray()
        try:
            while not self.finished:
                try:
                    frame = await asyncio.wait_for(remote.recv(), .25)
                except TimeoutError:
                    frame = None
                if not self.connected.is_set() or (self.mode == 'single' and not self.uplink.released):
                    continue
                for out in resampler.resample(frame) if frame is not None else []:
                    samples = out.to_ndarray().reshape(-1)
                    loud = np.max(np.abs(samples.astype(np.int32)), initial=0) > 170
                    now = time.monotonic()
                    if loud:
                        last_voice = now
                        if not self.uplink.playing:
                            self.uplink.playing = True
                            if self.mode == 'call':
                                while not self.uplink.queue.empty():
                                    self.uplink.queue.get_nowait()
                            await self.emit('speaking')
                    if self.uplink.playing:
                        buffer.extend(samples.astype('<i2', copy=False).tobytes())
                        while len(buffer) >= FRAME:
                            await self.emit('audio', bytes(buffer[:FRAME]))
                            del buffer[:FRAME]
                if self.uplink.playing and time.monotonic() - last_voice > .7:
                    if buffer:
                        await self.emit('audio', bytes(buffer).ljust(FRAME, b'\0'))
                        buffer.clear()
                    self.uplink.playing = False
                    if self.mode == 'single':
                        # The final transcript can follow the last media packet.
                        await asyncio.sleep(.9)
                        self.finished = True
                        await self.emit('done')
                    else:
                        await self.emit('listening')
        except asyncio.CancelledError:
            raise
        except Exception:
            if not self.closing:
                await self.emit('error', '音频连接中断')

    async def close(self):
        self.closing = True
        if self.receiver:
            self.receiver.cancel()
            await asyncio.gather(self.receiver, return_exceptions=True)
        await self.peer.close()
        if self.thread:
            try:
                await asyncio.wait_for(self.rpc.request('thread/realtime/stop', {'threadId': self.thread}), 2)
            except Exception:
                pass
        await self.rpc.close()
