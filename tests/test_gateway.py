import asyncio
import json
import unittest
from unittest.mock import patch
from websockets.asyncio.client import connect
from websockets.asyncio.server import serve

from gateway import VoiceGateway

class EchoBackend:
    def __init__(self, emit, mode):
        self.emit = emit
        self.mode = mode
        self.frames = []
        self.closed = False

    async def start(self):
        await self.emit('connected')

    def audio(self, frame):
        self.frames.append(frame)

    def release(self):
        async def reply():
            await self.emit('reply', '已收到')
            await self.emit('audio', self.frames[0])
            await self.emit('done')
        return asyncio.create_task(reply())

    async def close(self):
        self.closed = True

class GatewayTest(unittest.IsolatedAsyncioTestCase):
    async def test_idle_watch_allows_speaker_and_cannot_end_its_turn(self):
        from functools import partial

        gateway = VoiceGateway(EchoBackend, 'test-token', speaker_text=lambda text: asyncio.sleep(0, result=text))
        headers = {'Authorization': 'Bearer test-token'}
        async with serve(gateway.handle, '127.0.0.1', 0, process_request=gateway.authenticate) as watch_server, \
                   serve(partial(gateway.handle, speaker=True), '127.0.0.1', 0,
                         process_request=partial(gateway.authenticate, path='/speaker')) as speaker_server:
            watch_url = f'ws://127.0.0.1:{watch_server.sockets[0].getsockname()[1]}/voice'
            speaker_url = f'ws://127.0.0.1:{speaker_server.sockets[0].getsockname()[1]}/speaker'
            async with connect(watch_url, additional_headers=headers) as watch, \
                       connect(speaker_url, additional_headers=headers) as speaker, asyncio.timeout(2):
                self.assertEqual(await watch.recv(), 'ready')
                self.assertEqual(await speaker.recv(), 'ready')
                await speaker.send('text:你好')
                self.assertEqual(await speaker.recv(), 'reply:你好')
                self.assertEqual(await speaker.recv(), 'done')
                await watch.send('call')
                self.assertIn('另一台设备', await watch.recv())
                await watch.send('hangup')
                self.assertEqual(await watch.recv(), 'idle')
                await watch.send('single')
                self.assertIn('另一台设备', await watch.recv())
                await speaker.send('hangup')
                self.assertEqual(await speaker.recv(), 'idle')
                await watch.send('single')
                self.assertEqual(await watch.recv(), 'connected')

    async def test_one_shot_releases_and_disconnect_closes_backend(self):
        instances = []
        def factory(*args):
            instances.append(EchoBackend(*args))
            return instances[-1]
        gateway = VoiceGateway(factory, 'test-token')
        async with serve(gateway.handle, '127.0.0.1', 0, process_request=gateway.authenticate) as server:
            port = server.sockets[0].getsockname()[1]
            async with connect(f'ws://127.0.0.1:{port}/voice', additional_headers={'Authorization':'Bearer test-token'}) as ws:
                self.assertEqual(await ws.recv(), 'ready')
                await ws.send('single')
                self.assertEqual(await ws.recv(), 'connected')
                frame = bytes(640)
                await ws.send(frame)
                await ws.send('release')
                self.assertEqual(await ws.recv(), 'reply:已收到')
                self.assertEqual(await ws.recv(), frame)
                self.assertEqual(await ws.recv(), 'done')
                await ws.send('hangup')
                self.assertEqual(await ws.recv(), 'idle')
                self.assertTrue(instances[0].closed)

    async def test_reconnecting_watch_is_accepted_while_previous_call_closes(self):
        closing = asyncio.Event()
        release_old = asyncio.Event()

        class SlowClose(EchoBackend):
            async def close(self):
                closing.set()
                await release_old.wait()

        gateway = VoiceGateway(SlowClose, 'test-token')
        headers = {'Authorization': 'Bearer test-token'}
        async with serve(gateway.handle, '127.0.0.1', 0, process_request=gateway.authenticate) as server:
            port = server.sockets[0].getsockname()[1]
            url = f'ws://127.0.0.1:{port}/voice'
            async with asyncio.timeout(3), connect(url, additional_headers=headers) as old:
                self.assertEqual(await old.recv(), 'ready')
                await old.send('call')
                self.assertEqual(await old.recv(), 'connected')
                try:
                    async with connect(url, additional_headers=headers) as replacement:
                        self.assertEqual(await replacement.recv(), 'ready')
                        await closing.wait()
                        await replacement.send('call')
                        release_old.set()
                        self.assertEqual(await replacement.recv(), 'connected')
                finally:
                    release_old.set()

    async def test_watch_link_diagnostic_logs_only_allowed_fields_and_acks_retries(self):
        gateway = VoiceGateway(EchoBackend, 'test-token')
        with patch('gateway.diagnostic') as log:
            async with serve(gateway.handle, '127.0.0.1', 0,
                             process_request=gateway.authenticate) as server:
                port = server.sockets[0].getsockname()[1]
                async with connect(f'ws://127.0.0.1:{port}/voice',
                                   additional_headers={'Authorization': 'Bearer test-token'}) as ws, asyncio.timeout(2):
                    self.assertEqual(await ws.recv(), 'ready')
                    await ws.send('wifi-diag:123,1,1,200,-72,6,4500,private-payload')
                    for _ in range(2):
                        await ws.send('wifi-diag:123,1,1,200,-72,6,4500')
                        self.assertEqual(await ws.recv(), 'wifi-diag-ack:1')
            records = [call.kwargs for call in log.call_args_list if call.args == ('watch_link',)]
            self.assertEqual(len(records), 1)
            self.assertEqual((records[0]['kind'], records[0]['wifi_reason'],
                              records[0]['rssi_dbm'], records[0]['channel']),
                             ('wifi_down', 200, -72, 6))
            self.assertNotIn('private-payload', str(log.call_args_list))

    async def test_watch_notice_waits_for_call_and_acknowledges_after_playback(self):
        from pathlib import Path
        from tempfile import TemporaryDirectory

        from inbox import Inbox

        with TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            inbox.observe([['before', False, '旧消息']])
            history = [['before', False, '旧消息'], ['offline', False, '关机时的回复']]
            inbox.observe(history)
            speech_ready = asyncio.Event()

            async def synthesize(text):
                await speech_ready.wait()
                return bytes(640)

            gateway = VoiceGateway(EchoBackend, 'test-token', inbox=inbox,
                                   notice_audio=synthesize)
            headers = {'Authorization': 'Bearer test-token'}
            async with serve(gateway.handle, '127.0.0.1', 0,
                             process_request=gateway.authenticate) as server:
                port = server.sockets[0].getsockname()[1]
                async with connect(f'ws://127.0.0.1:{port}/voice', additional_headers=headers) as ws:
                    self.assertEqual(await ws.recv(), 'ready')
                    await ws.send('notice-ready')
                    with self.assertRaises(TimeoutError):
                        await asyncio.wait_for(ws.recv(), .15)
                    await ws.send('call')
                    self.assertEqual(await ws.recv(), 'connected')
                    inbox.observe(history + [['new', False, '新消息']])
                    await ws.send('notice-ready')
                    with self.assertRaises(TimeoutError):
                        await asyncio.wait_for(ws.recv(), .15)
                    await ws.send('hangup')
                    self.assertEqual(await ws.recv(), 'idle')
                    await ws.send('notice-ready')
                    offer = await asyncio.wait_for(ws.recv(), 2)
                    self.assertTrue(offer.startswith('notice:'))
                    key = offer.split(':')[1]
                    with self.assertRaises(TimeoutError):
                        await asyncio.wait_for(ws.recv(), .15)
                    await ws.send('notice-accept:' + key)
                    self.assertEqual(await asyncio.wait_for(ws.recv(), 2), 'notice-text:新消息')
                    speech_ready.set()
                    self.assertEqual(await ws.recv(), 'speaking')
                    self.assertEqual(await ws.recv(), bytes(640))
                    self.assertEqual(await ws.recv(), 'done')
                    self.assertIsNotNone(inbox.pending())
                    await ws.send('notice-done:' + key)
                    await ws.ping()
                    for _ in range(10):
                        if inbox.pending() is None:
                            break
                        await asyncio.sleep(.01)
                    self.assertIsNone(inbox.pending())
