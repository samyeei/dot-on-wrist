import tempfile
import unittest
from pathlib import Path

from inbox import Inbox


def row(text, outgoing=False, key=''):
    return [key, outgoing, text]


class InboxTest(unittest.TestCase):
    def test_loaded_empty_thread_can_receive_its_first_message(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            inbox.observe([])
            inbox.observe([row('第一条')])
            self.assertEqual(inbox.pending()[1], '第一条')

    def test_history_covered_replies_and_later_identical_messages(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            history = [row('旧消息')]
            inbox.observe(history)
            self.assertIsNone(inbox.pending())
            history += [row('问题', True), row('正在查'), row('已完成')]
            inbox.observe(history, covered={2})
            end, text = inbox.pending()
            self.assertEqual(text, '已完成')
            inbox = Inbox(Path(directory))
            inbox.observe(history)
            self.assertEqual(inbox.pending(), (end, text))
            history += [row('已完成')]
            inbox.observe(history)
            inbox.ack(end)
            self.assertEqual(inbox.pending()[1], '已完成')

    def test_stale_tab_and_edit_do_not_repeat_notification(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            old = row('旧消息', key='one')
            inbox.observe([old])
            inbox.observe([old, row('草稿内容', key='two')])
            end, _ = inbox.pending()
            inbox.ack(end)
            inbox.observe([old])
            inbox.observe([old, row('修订内容', key='two')])
            self.assertIsNone(inbox.pending())
            inbox.observe([row('修订内容', key='two'), row('新消息', key='three')])
            self.assertEqual(inbox.pending()[1], '新消息')

    def test_apple_watch_can_ack_without_consuming_m5stack_messages(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = Inbox(Path(directory))
            inbox.observe([row('旧消息')])
            inbox.enable_apple_watch()
            inbox.observe([row('旧消息'), row('新消息')])
            key, text = inbox.pending(apple_watch=True)
            self.assertEqual(text, '新消息')
            inbox.ack(key, apple_watch=True)
            self.assertIsNone(inbox.pending(apple_watch=True))
            self.assertEqual(inbox.pending()[1], '新消息')
