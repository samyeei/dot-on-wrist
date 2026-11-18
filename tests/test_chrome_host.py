import json
import select
import subprocess
import sys
import tempfile
import time
import unittest
import uuid
from pathlib import Path

from call_control import CallControl

HOST = Path(__file__).resolve().parents[1] / 'mac' / 'chrome_host.py'


class ChromeHostTest(unittest.IsolatedAsyncioTestCase):
    async def test_gateway_text_updates_and_call_ownership(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / 'target'
            target.write_text('https://chatgpt.com/dots/test')
            control = CallControl(root / 'control')
            with subprocess.Popen(
                [sys.executable, str(HOST), str(control.directory), str(target)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            ) as host:
                def receive():
                    self.assertTrue(select.select([host.stdout], [], [], 3)[0],
                                    'Chrome native host did not answer')
                    size = int.from_bytes(host.stdout.read(4), sys.byteorder)
                    self.assertGreater(size, 0, 'Chrome native host did not answer')
                    return json.loads(host.stdout.read(size))

                def poll(**fields):
                    body = json.dumps({'available': True, **fields}).encode()
                    host.stdin.write(len(body).to_bytes(4, sys.byteorder) + body)
                    host.stdin.flush()
                    return receive().get('job')

                def request(action, owner=control, **fields):
                    path = control.directory / f'{uuid.uuid4()}.request'
                    path.write_text(json.dumps(dict(action=action, lease=owner.lease,
                                                    expires=time.time() + 10, **fields)))
                    return path

                try:
                    self.assertEqual(receive()['target'], target.read_text())
                    self.assertIsNone(poll())
                    self.assertTrue(control.ready())
                    chat = request('text', text='测试')
                    job = poll()
                    self.assertEqual(job['text'], '测试')
                    self.assertEqual(poll(result={'id': job['id'], 'ok': True,
                                                  'partial': True, 'reply': '请稍等'}), job)
                    self.assertTrue(json.loads(chat.with_suffix('.response').read_text())['partial'])
                    self.assertIsNone(poll(result={'id': job['id'], 'ok': True, 'reply': '完成'}))
                    chat.unlink()
                    dial = request('dial')
                    job = poll()
                    self.assertEqual(job['action'], 'dial')
                    self.assertIsNone(poll(result={'id': job['id'], 'ok': True, 'tabId': 17}))
                    dial.unlink()
                    hangup = request('hangup', CallControl(control.directory))
                    self.assertIsNone(poll())
                    self.assertTrue(json.loads(hangup.with_suffix('.response').read_text())['ok'])
                    hangup.unlink()
                    hangup = request('hangup')
                    job = poll()
                    self.assertEqual(job['action'], 'hangup')
                    self.assertEqual(job['tabId'], 17)
                    self.assertIsNone(poll(result={'id': job['id'], 'ok': True}))
                    hangup.unlink()
                finally:
                    host.stdin.close()
                    host.wait(timeout=4)
                self.assertFalse(control.ready())
                self.assertEqual(host.returncode, 0)
