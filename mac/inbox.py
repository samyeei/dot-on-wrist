"""The last observed ANNA message window and the watch's durable inbox."""
import hashlib
import json
import sqlite3
from contextlib import closing


def _generated_hash_key(key):
    return len(key) == 64 and not key.strip('0123456789abcdef')


def end_of_history(previous, current):
    if not previous:
        return 0
    # Most observations share the previous tail. If a message was edited while
    # streaming and has no DOM ID, count the edited rows as part of history.
    for edited in range(min(2, len(previous) - 1) + 1):
        if edited and not all(_generated_hash_key(key) for key in previous[len(previous) - edited:]):
            continue
        tail = previous[:len(previous) - edited]
        for length in range(min(len(current), len(tail)), 0, -1):
            for offset in range(len(current) - length + 1):
                if current[offset:offset + length] == tail[-length:]:
                    return min(len(current), offset + length + edited)
    shared = 0
    for previous_key, current_key in zip(previous, current):
        if previous_key != current_key:
            break
        shared += 1
    if shared:
        return shared
    return None


class Inbox:
    def __init__(self, directory):
        self.path = directory / 'watch-inbox.sqlite3'
        with closing(self.connect()) as db, db:
            db.execute('CREATE TABLE IF NOT EXISTS cursor (id INTEGER PRIMARY KEY, rows TEXT)')
            db.execute('CREATE TABLE IF NOT EXISTS pending (id INTEGER PRIMARY KEY AUTOINCREMENT, text TEXT)')

    def enable_apple_watch(self):
        with closing(self.connect()) as db, db:
            db.execute('CREATE TABLE IF NOT EXISTS apple_pending ('
                       'id INTEGER PRIMARY KEY AUTOINCREMENT, text TEXT)')
            columns = {row[1] for row in db.execute('PRAGMA table_info(apple_pending)')}
            if 'received_at' not in columns:
                db.execute('ALTER TABLE apple_pending ADD COLUMN received_at REAL')
            db.execute("""
                CREATE TRIGGER IF NOT EXISTS timestamp_apple_notice AFTER INSERT ON apple_pending
                WHEN NEW.received_at IS NULL
                BEGIN
                    UPDATE apple_pending
                    SET received_at = (julianday('now') - 2440587.5) * 86400
                    WHERE id = NEW.id;
                END
            """)
            db.execute("""
                CREATE TRIGGER IF NOT EXISTS copy_to_apple_watch AFTER INSERT ON pending
                BEGIN
                    INSERT INTO apple_pending(text) VALUES (NEW.text);
                END
            """)

    def connect(self):
        return sqlite3.connect(self.path, timeout=1)

    def observe(self, rows, covered=()):
        if not isinstance(rows, list) or len(rows) > 20:
            return
        if not all(isinstance(row, list) and len(row) == 3 and isinstance(row[0], str)
                   and type(row[1]) is bool and isinstance(row[2], str) for row in rows):
            return
        keys = [key or hashlib.sha256(f'{outgoing}:{text}'.encode()).hexdigest()
                for key, outgoing, text in rows]
        with closing(self.connect()) as db, db:
            db.execute('BEGIN IMMEDIATE')
            cursor = db.execute('SELECT rows FROM cursor WHERE id=1').fetchone()
            if cursor:
                if not keys:
                    return
                previous = json.loads(cursor[0])
                start = end_of_history(previous, keys)
                if start is None:
                    # The visible window may have jumped after a reload. Accept a
                    # second consistent view as the baseline without replaying it.
                    candidate = db.execute('SELECT rows FROM cursor WHERE id=2').fetchone()
                    db.execute('INSERT OR REPLACE INTO cursor VALUES (2, ?)', (json.dumps(keys),))
                    start = end_of_history(json.loads(candidate[0]), keys) if candidate else None
                    if start is None:
                        return
                elif start == len(keys):
                    db.execute('DELETE FROM cursor WHERE id=2')
                    return
                covered = set(covered) if isinstance(covered, (list, set, tuple)) else set()
                db.executemany('INSERT INTO pending(text) VALUES(?)', [
                    (text,) for index, (_, outgoing, text) in enumerate(rows)
                    if index >= start and index not in covered and not outgoing and text.strip()
                ])
            db.execute('DELETE FROM cursor WHERE id=2')
            db.execute('INSERT OR REPLACE INTO cursor VALUES (1, ?)', (json.dumps(keys),))

    def latest_m5stack_id(self):
        with closing(self.connect()) as db:
            return db.execute('SELECT COALESCE(MAX(id), 0) FROM pending').fetchone()[0]

    def pending(self, *, apple_watch=False, since=None):
        table = 'apple_pending' if apple_watch else 'pending'
        with closing(self.connect()) as db:
            if apple_watch and since is not None:
                rows = db.execute(
                    'SELECT id, text FROM apple_pending WHERE received_at >= ? ORDER BY id LIMIT 8',
                    (since,)).fetchall()
            else:
                rows = db.execute(f'SELECT id, text FROM {table} ORDER BY id LIMIT 8').fetchall()
        if not rows:
            return None
        parts = []
        size = 0
        for key, text in rows:
            amount = len(text.encode()) + 2
            if parts and size + amount > 12000:
                break
            end = key
            parts.append(text)
            size += amount
        return end, '\n\n'.join(parts)

    def ack(self, end, *, apple_watch=False):
        table = 'apple_pending' if apple_watch else 'pending'
        with closing(self.connect()) as db, db:
            db.execute(f'DELETE FROM {table} WHERE id <= ?', (end,))
