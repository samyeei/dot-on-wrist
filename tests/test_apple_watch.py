import asyncio
import base64
import unittest

from aiohttp.test_utils import TestClient, TestServer

from apple_watch import make_app
from gateway import VoiceGateway


class Backend:
    def __init__(self, emit, mode):
        self.emit = emit
        self.mode = mode
        self.frames = []

    async def start(self):
        await self.emit('connected')

    def audio(self, data):
        self.frames.append(data)

    def release(self):
        async def finish():
            await self.emit('audio', b''.join(self.frames))
            await self.emit('done')
        return asyncio.create_task(finish())

    async def close(self):
        pass


class SynchronousReleaseBackend(Backend):
    def release(self):
        asyncio.create_task(self.emit('done'))


class WatchHTTPTests(unittest.IsolatedAsyncioTestCase):
    async def test_single_round_trip_and_exclusive_call(self):
        gateway = VoiceGateway(Backend, 'legacy')
        async with TestClient(TestServer(make_app(gateway, 'watch'))) as client:
            auth = {'Authorization': 'Bearer watch'}
            self.assertEqual((await client.get('/watch/health')).status, 401)
            self.assertEqual((await client.get('/watch/health', headers=auth)).status, 200)
            response = await client.post('/watch/sessions', headers=auth, json={'mode': 'single'})
            self.assertEqual(response.status, 201)
            path = '/watch/sessions/' + (await response.json())['id']
            busy = await client.post('/watch/sessions', headers=auth, json={'mode': 'call'})
            self.assertEqual(busy.status, 409)
            sample = bytes(640)
            response = await client.post(path, headers=auth,
                                         data=sample, params={'release': '1'})
            events = await response.json()
            for _ in range(20):
                if 'done' in events['events']:
                    break
                response = await client.post(path, headers=auth)
                batch = await response.json()
                events['events'] += batch['events']
                events['audio'] += batch['audio']
            self.assertIn('done', events['events'])
            self.assertIn(base64.b64encode(sample).decode(), events['audio'])
            self.assertEqual((await client.delete(path, headers=auth)).status, 204)
            response = await client.post('/watch/sessions', headers=auth, json={'mode': 'call'})
            self.assertEqual(response.status, 201)
            path = '/watch/sessions/' + (await response.json())['id']
            self.assertEqual((await client.delete(path, headers=auth)).status, 204)
            self.assertIsNone(gateway.owner)

    async def test_backend_with_synchronous_release(self):
        gateway = VoiceGateway(SynchronousReleaseBackend, 'legacy')
        async with TestClient(TestServer(make_app(gateway, 'watch'))) as client:
            auth = {'Authorization': 'Bearer watch'}
            response = await client.post('/watch/sessions', headers=auth, json={'mode': 'single'})
            path = '/watch/sessions/' + (await response.json())['id']
            await client.post(path, headers=auth, data=bytes(640), params={'release': '1'})
            for _ in range(20):
                events = await (await client.post(path, headers=auth)).json()
                self.assertFalse(any(event.startswith('error:') for event in events['events']))
                if 'done' in events['events']:
                    break
            else:
                self.fail('No completion event')

    async def test_notice_is_authenticated_and_survives_until_acknowledged(self):
        import tempfile
        from pathlib import Path
        from inbox import Inbox

        with tempfile.TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            inbox.enable_apple_watch()
            inbox.observe([])
            history = [['offline', False, '离线旧消息']]
            inbox.observe(history)
            from time import time
            opened = time()
            await asyncio.sleep(.02)
            inbox.observe(history + [['message', False, '测试通知']])
            gateway = VoiceGateway(Backend, 'legacy', inbox=inbox)
            gateway.owner = object()  # Another device is using the shared voice backend.
            async with TestClient(TestServer(make_app(gateway, 'watch'))) as client:
                auth = {'Authorization': 'Bearer watch'}
                path = '/watch/notices'
                self.assertEqual((await client.get(path)).status, 401)
                path += '?since=' + str(opened)
                payload = await (await client.get(path, headers=auth)).json()
                self.assertEqual(payload['text'], '测试通知')
                self.assertEqual(await (await client.get(path, headers=auth)).json(), payload)
                ack_path = '/watch/notices/' + str(payload['id'])
                self.assertEqual((await client.delete(ack_path, headers=auth)).status, 204)
                self.assertEqual((await client.get(path, headers=auth)).status, 204)
                self.assertEqual(inbox.pending()[1], '离线旧消息\n\n测试通知')
