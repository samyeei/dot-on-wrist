"""Translate Chrome native messaging to the gateway's local request protocol."""
import fcntl
import json
import re
import struct
import sys
import time
from pathlib import Path
from inbox import Inbox

FRAME = struct.Struct('=I')
UUID = re.compile(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}')
TARGET = re.compile(r'https://chatgpt\.com/dots/[a-zA-Z0-9~-]+')


def receive():
    header = sys.stdin.buffer.read(FRAME.size)
    if not header:
        return None
    size, = FRAME.unpack(header)
    if size > 1048576:
        raise ValueError('invalid Chrome message size')
    return json.loads(sys.stdin.buffer.read(size))


def send(message):
    body = json.dumps(message, ensure_ascii=False).encode()
    sys.stdout.buffer.write(FRAME.pack(len(body)) + body)
    sys.stdout.buffer.flush()


def write(path, message):
    temporary = path.with_suffix('.host-tmp')
    temporary.write_text(json.dumps(message, ensure_ascii=False))
    temporary.replace(path)


class Bridge:
    def __init__(self, directory):
        self.directory = directory
        self.inbox = Inbox(directory)
        self.ready = directory / 'ready.json'
        self.lease_path = directory / 'call.json'
        self.lease = self.load_lease()
        self.job = None
        self.previous_lease = None
        self.last_ready = 0

    def load_lease(self):
        try:
            return json.loads(self.lease_path.read_text())
        except (OSError, ValueError):
            return None

    def set_lease(self, value):
        if value is None:
            self.lease_path.unlink(missing_ok=True)
        else:
            write(self.lease_path, value)
        self.lease = value

    def response(self, job, result):
        path = self.directory / (job['id'] + '.request')
        if path.exists():
            write(path.with_suffix('.response'), result)

    def poll(self, message):
        now = time.time()
        available = message.get('available') is True
        observed = message.get('observed')
        if isinstance(observed, dict):
            self.inbox.observe(observed.get('rows'))
        if available and now - self.last_ready >= 1:
            write(self.ready, {'at': now})
            self.last_ready = now
        elif not available:
            self.ready.unlink(missing_ok=True)
            self.last_ready = 0

        if self.job:
            job = self.job
            result = message.get('result')
            if isinstance(result, dict) and result.get('id') == job['id']:
                ok = result.get('ok') is True
                partial = ok and job['action'] == 'text' and result.get('partial') is True
                snapshot = result.get('snapshot')
                if ok and job['action'] == 'text' and isinstance(snapshot, dict):
                    self.inbox.observe(snapshot.get('rows'), snapshot.get('covered', ()))
                if job['action'] == 'hangup' and ok:
                    self.set_lease(None)
                elif job['action'] == 'dial' and ok and type(result.get('tabId')) is int:
                    self.set_lease({**self.lease, 'tabId': result['tabId']})
                elif job['action'] == 'dial' and not ok:
                    self.set_lease(self.previous_lease)
                self.response(job, {
                    'ok': ok, 'partial': partial,
                    'submitted': partial and result.get('submitted') is True,
                    'preview': partial and result.get('preview') is True,
                    'reply': str(result.get('reply', ''))[:1800],
                    'message': str(result.get('message', ''))[:200],
                })
                if not partial:
                    self.job = None
            if now >= job['expires'] or not (self.directory / (job['id'] + '.request')).exists():
                self.job = None
        if self.job or not available:
            return self.job
        for path in sorted(self.directory.glob('*.request'), key=lambda entry: entry.name):
            if not UUID.fullmatch(path.stem):
                continue
            response = path.with_suffix('.response')
            try:
                request = json.loads(path.read_text())
                action, lease, expires = request['action'], request['lease'], request['expires']
                if not UUID.fullmatch(lease) or not now < expires <= now + 121:
                    continue
                prior = json.loads(response.read_text()) if response.exists() else None
            except (OSError, ValueError, KeyError, TypeError):
                continue
            job = dict(id=path.stem, action=action, expires=expires)
            if prior:
                if prior.get('partial'):
                    self.response(job, {'ok': False, 'message': 'Chrome 连接中断，请查看 ANNA 对话'})
                continue
            if action == 'hangup' and (not self.lease or self.lease['id'] != lease):
                self.response(job, {'ok': True})
                continue
            if action == 'text':
                text = request.get('text')
                if not isinstance(text, str) or not text.strip() or len(text.encode()) > 1800:
                    self.response(job, {'ok': False, 'message': '消息为空或过长'})
                    continue
                job['text'] = text
            elif action == 'dial':
                # Persist ownership before dispatch so a cancelled dial can still be ended.
                self.previous_lease = self.lease
                self.set_lease({'id': lease, 'at': now})
            elif action == 'hangup':
                job['dialedAt'] = self.lease['at']
                if 'tabId' in self.lease:
                    job['tabId'] = self.lease['tabId']
            else:
                self.response(job, {'ok': False, 'message': '未知操作'})
                continue
            self.response(job, {'ok': True, 'partial': True})
            self.job = job
            return job
        return None


def main(directory, target_path):
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (directory / 'host.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        bridge = Bridge(directory)
        bridge.ready.unlink(missing_ok=True)
        try:
            target = target_path.read_text().strip()
            if not TARGET.fullmatch(target):
                raise ValueError('invalid ANNA target')
            send({'target': target})
            while (message := receive()) is not None:
                send({'job': bridge.poll(message)})
        finally:
            bridge.ready.unlink(missing_ok=True)


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]))
