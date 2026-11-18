import unittest
from unittest.mock import AsyncMock, patch

from text_voice import TextVoice


class TextVoiceTest(unittest.IsolatedAsyncioTestCase):
    async def test_release_displays_text_before_synthesizing_speech(self):
        events = []

        async def emit(event, data=None):
            events.append((event, data))

        backend = TextVoice(emit, 'transcriber')
        backend.control.ready = lambda: True
        backend.control.send = AsyncMock(return_value='已收到')
        async def synthesize(answer):
            self.assertEqual(events[-1], ('reply-view', answer))
            return bytes(640)
        backend.synthesize = AsyncMock(side_effect=synthesize)
        await backend.start()
        for _ in range(20):
            backend.audio(bytes(640))
        backend.control.send.assert_not_awaited()
        with patch('text_voice.run_local', new=AsyncMock(return_value='你好'.encode())):
            await backend.release()
        self.assertEqual(backend.control.send.await_args.args, ('text', 120))
        self.assertEqual(backend.control.send.await_args.kwargs['text'], '你好')
        backend.synthesize.assert_awaited_once_with('已收到')
        self.assertEqual([event for event, _ in events],
                         ['preparing', 'reply-view', 'speaking', 'audio', 'done'])

    async def test_progress_is_visible_and_only_the_followup_is_read_aloud(self):
        emit = AsyncMock()
        backend = TextVoice(emit, 'transcriber')
        backend.control.ready = lambda: True
        backend.synthesize = AsyncMock(return_value=bytes(640))
        backend.audio(bytes(640 * 16))

        async def send(_action, _timeout, *, text, on_update, on_preview, on_submitted):
            await on_submitted()
            emit.assert_awaited_with('submitted')
            await on_update('请稍等')
            emit.assert_awaited_with('reply-view', '请稍等')
            await on_preview('请稍等\n\n这是结果')
            emit.assert_awaited_with('reply-view', '请稍等\n\n这是结果')
            return '请稍等\n\n这是结果'

        backend.control.send = send
        with patch('text_voice.run_local', new=AsyncMock(return_value='你好'.encode())):
            await backend.release()
        backend.synthesize.assert_awaited_once_with('这是结果')
        self.assertEqual([call.args[1] for call in emit.await_args_list if call.args[0] == 'reply-view'],
                         ['请稍等', '请稍等\n\n这是结果'])

    async def test_synthesis_leaves_headroom_for_the_watch_speaker(self):
        import wave
        import numpy as np

        samples = np.tile(np.array([24000, -24000, 12000, -12000], dtype='<i2'), 160)

        async def synthesize_wave(*args, **kwargs):
            with wave.open(args[args.index('-o') + 1], 'wb') as output:
                output.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
                output.writeframes(samples.tobytes())

        with patch('text_voice.run_local', side_effect=synthesize_wave):
            pcm = await TextVoice(None, None).synthesize('测试')
        audio = np.frombuffer(pcm, dtype='<i2')
        self.assertEqual(len(audio), len(samples))
        self.assertEqual(int(np.max(np.abs(audio.astype(np.int32)))), 19000)
        self.assertEqual((audio[0], audio[-1]), (0, 0))
        np.testing.assert_array_equal(audio[160:-160], np.rint(samples[160:-160] * (19000 / 24000)).astype(np.int16))
