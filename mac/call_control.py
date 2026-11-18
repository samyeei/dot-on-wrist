"""Local request channel to ANNA's browser or native controller."""
import asyncio
import json
import os
import time
import uuid
from pathlib import Path


def chrome_control():
    directory = os.environ.get('ANNA_CHROME_CONTROL_DIRECTORY')
    return CallControl(directory or Path.home() / 'Library/Application Support/ANNA Chrome Call Helper')


def chrome_unavailable():
    if os.environ.get('ANNA_CHROME_CONTROL_DIRECTORY'):
        return '请打开 Chrome，并保留一个 ANNA 对话标签页'
    return '请打开 Mac 上的 ANNA 通话助手'


class CallControl:
    def __init__(self, directory=None):
        self.directory = (Path(directory) if directory is not None else
                          Path.home() / 'Library/Application Support/ANNA Call Helper')
        self.lease = str(uuid.uuid4())

    def ready(self):
        try:
            heartbeat = json.loads((self.directory / 'ready.json').read_text())
            return 0 <= time.time() - heartbeat['at'] < 3
        except (OSError, ValueError, KeyError, TypeError):
            return False

    async def send(self, action, timeout=8, *, text=None, on_update=None, on_preview=None, on_submitted=None):
        key = str(uuid.uuid4())
        draft = self.directory / (key + '.tmp')
        request = draft.with_suffix('.request')
        response = draft.with_suffix('.response')
        deadline = time.time() + timeout
        previous = None
        try:
            fd = os.open(draft, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            with os.fdopen(fd, 'w') as output:
                payload = {'action': action, 'lease': self.lease, 'expires': deadline}
                if text is not None:
                    payload['text'] = text
                json.dump(payload, output, ensure_ascii=False)
            draft.replace(request)
            while time.time() < deadline:
                try:
                    result = json.loads(response.read_text())
                except FileNotFoundError:
                    await asyncio.sleep(.1)
                    continue
                if not result.get('ok'):
                    raise RuntimeError(result.get('message') or 'ANNA 通话助手无法操作')
                if on_submitted and (result.get('submitted') is True or result.get('reply')):
                    await on_submitted()
                    on_submitted = None
                if result.get('partial'):
                    update = result.get('reply')
                    preview = result.get('preview') is True
                    callback = on_preview if preview else on_update
                    if callback and isinstance(update, str) and (update, preview) != previous:
                        previous = (update, preview)
                        await callback(update)
                    await asyncio.sleep(.1)
                    continue
                return result.get('reply')
            raise RuntimeError('ANNA 通话助手未及时响应')
        finally:
            for path in (draft, request, response):
                path.unlink(missing_ok=True)
