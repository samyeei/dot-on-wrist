import asyncio
import unittest
from unittest.mock import AsyncMock

import numpy as np

from native_voice import FRAME, NativeVoice


class NativeSingleTest(unittest.TestCase):
    def test_only_continuous_calls_use_chrome_audio(self):
        class Audio:
            def list_processes(self):
                return [
                    {'bundle_id': 'com.openai.codex.helper', 'input_running': True},
                    {'bundle_id': 'com.google.Chrome.helper', 'input_running': True},
                ]

        for mode, bundle in [('single', 'com.openai.codex.helper'),
                             ('call', 'com.google.Chrome.helper')]:
            with self.subTest(mode=mode):
                voice = NativeVoice(None, mode, '', chrome_calls=True)
                voice.native = Audio()
                self.assertEqual([p['bundle_id'] for p in voice.active_processes()], [bundle])
                self.assertEqual(voice.call_control is not None, mode == 'call')

    def test_short_press_stays_silent(self):
        voice = NativeVoice(None, 'single', '')
        voice.allowed = True
        for _ in range(2):
            voice.audio(b'\x00\x10' * (FRAME // 2))
        voice.release()
        rendered = bytearray(FRAME * 6)
        voice.render(rendered, 960, None, None)
        self.assertFalse(any(rendered))
        self.assertIn('按住黄键', voice.release_error)

    def test_spoken_request_is_forwarded_after_release(self):
        voice = NativeVoice(None, 'single', '')
        voice.allowed = True
        frame = b'\x00\x10' * (FRAME // 2)
        for _ in range(30):
            voice.audio(frame)
        rendered = bytearray(FRAME * 6)
        voice.render(rendered, 960, None, None)
        self.assertFalse(any(rendered))
        voice.release()
        voice.render(rendered, 960, None, None)
        self.assertEqual(rendered, b'\x00\x10' * (len(rendered) // 2))


class ReplyOrderTest(unittest.IsolatedAsyncioTestCase):
    async def test_single_reply_sends_text_before_audio(self):
        events = []

        async def emit(event, data=None):
            events.append((event, data))

        voice = NativeVoice(emit, 'single', '')
        voice.started = True
        voice.check_route = lambda: None
        voice.transcribe = AsyncMock(return_value='测试成功')
        voice.downlink.put(np.full(320, .2, dtype='<f4').tobytes())
        reader = asyncio.create_task(voice.read_reply(16000))
        try:
            await asyncio.sleep(1.10)
            voice.downlink.put(bytes(320 * 4))
            await asyncio.wait_for(reader, 2)
        finally:
            reader.cancel()
            await asyncio.gather(reader, return_exceptions=True)
        self.assertEqual([event for event, _ in events],
                         ['preparing', 'reply', 'speaking', 'audio', 'audio', 'done'])
        self.assertIn(('reply', '测试成功'), events)
