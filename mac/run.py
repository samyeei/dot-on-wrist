import argparse
import asyncio
import json
import os
import ssl
import sys
from contextlib import AsyncExitStack
from functools import partial
from pathlib import Path
from websockets.asyncio.server import serve

from codex_voice import CodexVoice
from gateway import VoiceGateway

DEFAULT_CODEX = '/Applications/ChatGPT.app/Contents/Resources/codex-cli/bin/codex'

async def run(args):
    directory = Path(args.private)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(directory / 'server.pem', directory / 'server.key')
    native_config = directory / 'native.json'
    speaker_text = None
    inbox = notice_audio = None
    if native_config.is_file():
        config = json.loads(native_config.read_text())
        sys.path.append(config['audio_tools'])
        from native_voice import NativeVoice
        from text_voice import TextVoice
        if config.get('chrome_calls') is True:
            from call_control import chrome_control, chrome_unavailable
            if os.environ.get('ANNA_CHROME_CONTROL_DIRECTORY'):
                from inbox import Inbox
                inbox = Inbox(chrome_control().directory)
                notice_audio = TextVoice(None, config['transcriber']).synthesize
            async def speaker_text(prompt):
                control = chrome_control()
                if not control.ready():
                    raise RuntimeError(chrome_unavailable())
                reply = await control.send('text', 120, text=prompt)
                if not isinstance(reply, str) or not reply.strip():
                    raise RuntimeError('ANNA 暂未回复')
                return reply
        def factory(emit, mode):
            if config.get('chrome_calls') is True and mode == 'single':
                return TextVoice(emit, config['transcriber'])
            return NativeVoice(emit, mode, config['transcriber'],
                               chrome_calls=config.get('chrome_calls') is True)
        backend = 'native ChatGPT Dot audio'
    else:
        def factory(emit, mode):
            return CodexVoice(emit, mode, args.codex, args.cwd)
        backend = 'independent Codex voice'
    gateway = VoiceGateway(factory, (directory / 'token').read_text().strip(), speaker_text,
                           inbox=inbox, notice_audio=notice_audio)
    async with AsyncExitStack() as stack:
        await stack.enter_async_context(serve(gateway.handle, args.bind, args.port, ssl=context,
                     process_request=gateway.authenticate, max_size=4096, compression=None,
                     ping_interval=10, ping_timeout=12))
        if args.apple_watch_port:
            from apple_watch import serve_apple_watch
            watch_token = (directory / 'apple-watch-token').read_text().strip()
            if len(watch_token) < 32:
                raise ValueError('Apple Watch pairing token is missing or too short')
            if inbox is not None:
                inbox.enable_apple_watch()
            runner = await serve_apple_watch(gateway, watch_token, args.bind,
                                             args.apple_watch_port, context)
            stack.push_async_callback(runner.cleanup)
        if speaker_text:
            await stack.enter_async_context(serve(
                partial(gateway.handle, speaker=True), '127.0.0.1', 8766,
                process_request=partial(gateway.authenticate, path='/speaker'),
                max_size=4096, compression=None, ping_interval=10, ping_timeout=12))
        print(f'ANNA Mac gateway listening ({backend} backend)', flush=True)
        await asyncio.Future()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--private', default=str(Path(__file__).resolve().parents[1] / 'private'))
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--apple-watch-port', type=int)
    parser.add_argument('--codex', default=DEFAULT_CODEX)
    parser.add_argument('--cwd', default=str(Path(__file__).resolve().parents[2]))
    asyncio.run(run(parser.parse_args()))
