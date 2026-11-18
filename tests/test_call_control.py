import asyncio
import json
import tempfile
import time
import unittest
from pathlib import Path

from call_control import CallControl


class CallControlTest(unittest.IsolatedAsyncioTestCase):
    async def test_request_is_scoped_to_lease_and_cancel_removes_request(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            control = CallControl(directory)
            self.assertFalse(control.ready())
            (directory / 'ready.json').write_text(json.dumps({'at': time.time()}))
            self.assertTrue(control.ready())
            dial = asyncio.create_task(control.send('dial', timeout=2))
            await asyncio.sleep(.05)
            request_path, = directory.glob('*.request')
            request = json.loads(request_path.read_text())
            self.assertEqual(request['lease'], control.lease)
            request_path.with_suffix('.response').write_text('{"ok": true}')
            await dial
            self.assertFalse(request_path.exists())

            hangup = asyncio.create_task(control.send('hangup', timeout=2))
            await asyncio.sleep(.05)
            request_path, = directory.glob('*.request')
            self.assertEqual(json.loads(request_path.read_text())['action'], 'hangup')
            hangup.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await hangup
            self.assertFalse(request_path.exists())
