"""Authenticated watch transport. Audio frames are mono 16 kHz signed PCM."""
import asyncio
import hmac
import json
import re
import sqlite3
import time
from datetime import datetime, timedelta, timezone
from itertools import count
from websockets.exceptions import ConnectionClosed

def diagnostic(event, **fields):
    timestamp = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
    print('ANNA_DIAG ' + json.dumps({'at': timestamp, 'event': event, **fields},
                                   separators=(',', ':')), flush=True)


WATCH_LINK_EVENTS = {1: 'wifi_down', 2: 'mesh_node_changed', 3: 'gateway_down', 4: 'gateway_ready', 5: 'mesh_scan', 6: 'mesh_scan_failed'}
WATCH_LINK_FORMAT = re.compile(
    r'wifi-diag:([0-9]{1,10}),([0-9]{1,10}),([1-6]),([0-9]{1,3}),(-?[0-9]{1,3}),([0-9]{1,2}),([0-9]{1,10})'
)


class VoiceGateway:
    def __init__(self, factory, token, speaker_text=None, *, inbox=None, notice_audio=None):
        self.factory = factory
        self.token = 'Bearer ' + token
        self.watch_session = None
        self.owner = None
        self.speaker_text = speaker_text
        self.inbox, self.notice_audio = inbox, notice_audio
        self.connection_ids = count(1)
        self.last_watch_diagnostic = (None, 0)

    async def authenticate(self, connection, request, *, path='/voice'):
        headers = request.headers.get_all('Authorization')
        if request.path != path or request.headers.get('Origin'):
            diagnostic('upgrade_rejected', reason='unexpected_route_or_origin', status=404)
            return connection.respond(404, 'Not found')
        if len(headers) != 1 or not hmac.compare_digest(headers[0].encode(), self.token.encode()):
            diagnostic('upgrade_rejected', reason='authentication', status=401)
            return connection.respond(401, 'Unauthorized')

    async def handle(self, ws, *, speaker=False):
        session = next(self.connection_ids)
        device = 'speaker' if speaker else 'watch'
        opened_at = time.monotonic()
        close_reason = 'handler_ended'
        sent_code = received_code = None

        def record(event, **fields):
            diagnostic(event, session=session, device=device, **fields)

        record('connection_open')
        previous_watch = None
        notice_cutoff = self.inbox.latest_m5stack_id() if not speaker and self.inbox else 0
        drained = asyncio.Event()
        if not speaker:
            previous_watch = self.watch_session
            self.watch_session = (ws, drained)
            if previous_watch:
                record('connection_replaces_watch')
                previous_watch[0].transport.abort()
        backend = opening = finishing = None
        notice = notice_id = None
        notice_accepted = asyncio.Event()
        released = False
        received = sent = 0

        async def emit(event, data=None):
            nonlocal sent
            if event == 'audio':
                sent += len(data)
                await ws.send(data)
            elif data is None:
                await ws.send(event)
            else:
                payload = data.encode()[:1800].decode(errors='ignore')
                await ws.send(event + ':' + payload)
            if event in ('ready', 'connected', 'done', 'listening', 'idle', 'error'):
                record(event, input_bytes=received, output_bytes=sent)

        async def stop():
            nonlocal backend, opening, finishing, released
            tasks = [task for task in (opening, finishing, notice) if task and not task.done()]
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            try:
                if backend:
                    await backend.close()
            finally:
                if self.owner is ws:
                    self.owner = None
                backend = opening = finishing = None
                released = False

        async def notify():
            nonlocal notice_id
            speech = None
            try:
                pending = self.inbox.pending()
                if not pending or self.owner is not None:
                    return
                key, text = pending
                notice_id = key
                notice_accepted.clear()
                self.owner = ws
                record('notice_start')
                await emit('notice', str(key))
                speech = asyncio.create_task(self.notice_audio(text))
                await asyncio.wait_for(notice_accepted.wait(), 6)
                remaining = text.encode()
                while remaining:
                    chunk = remaining[:1800].decode(errors='ignore')
                    await emit('notice-text', chunk)
                    remaining = remaining[len(chunk.encode()):]
                try:
                    pcm = await speech
                except (RuntimeError, OSError, TimeoutError, ValueError):
                    pcm = b''
                from native_voice import play_audio
                await play_audio(emit, pcm, prefill_packets=75)
                await asyncio.sleep(12)  # Let the device drain its final buffered audio.
            except (TimeoutError, ConnectionClosed, sqlite3.Error) as exc:
                record('notice_interrupted', reason=type(exc).__name__)
            finally:
                if speech is not None:
                    speech.cancel()
                    await asyncio.gather(speech, return_exceptions=True)
                if notice_id is not None and self.owner is ws:
                    self.owner = None
                notice_id = None

        async def ask(text):
            try:
                reply = await self.speaker_text(text)
                await emit('reply', reply)
                await emit('done')
            except (RuntimeError, OSError) as exc:
                await emit('error', str(exc))

        async def start():
            try:
                await asyncio.wait_for(backend.start(), 38)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                record('voice_start_failed', reason=type(exc).__name__)
                await emit('error', '语音服务未接通')

        try:
            if notice_cutoff:
                self.inbox.ack(notice_cutoff)
            await emit('ready')
            if previous_watch:
                await previous_watch[1].wait()
                record('previous_watch_released')
            while True:
                message = await ws.recv()
                if not speaker and isinstance(message, str) and message.startswith('wifi-diag:'):
                    match = WATCH_LINK_FORMAT.fullmatch(message)
                    if not match:
                        continue
                    boot, sequence, kind, reason, rssi, channel, age_ms = map(int, match.groups())
                    if (boot > 0xffffffff or not 0 < sequence <= 0xffffffff or reason > 255
                            or not -127 <= rssi <= 0 or channel > 14 or age_ms > 0xffffffff):
                        continue
                    last_boot, last_sequence = self.last_watch_diagnostic
                    if boot != last_boot or sequence > last_sequence:
                        occurred = datetime.now(timezone.utc) - timedelta(milliseconds=age_ms)
                        record('watch_link', boot=boot, sequence=sequence, kind=WATCH_LINK_EVENTS[kind],
                               wifi_reason=reason if kind == 1 else None,
                               alternatives=reason if kind == 5 else None,
                               scan_status=-reason if kind == 6 else None,
                               rssi_dbm=rssi or None,
                               channel=channel or None, age_ms=age_ms,
                               occurred_at=occurred.isoformat(timespec='milliseconds'))
                        self.last_watch_diagnostic = (boot, sequence)
                    await ws.send(f'wifi-diag-ack:{sequence}')
                elif message == 'call' or (not speaker and message == 'single'):
                    if notice_id is not None and not notice_accepted.is_set():
                        await stop()
                    if self.owner is not None:
                        await emit('error', 'ANNA 正在另一台设备上使用')
                        continue
                    received = sent = 0
                    record('voice_start', mode=message)
                    self.owner = ws
                    backend = self.factory(emit, message)
                    opening = asyncio.create_task(start())
                elif (speaker and self.speaker_text and isinstance(message, str)
                      and message.startswith('text:')):
                    if self.owner is not None:
                        await emit('error', 'ANNA 正在另一台设备上使用')
                        continue
                    prompt = message[5:].strip()
                    if not prompt or len(prompt.encode()) > 1800:
                        await emit('error', '问题为空或太长')
                        continue
                    self.owner = ws
                    finishing = asyncio.create_task(ask(prompt))
                elif (not speaker and message == 'notice-ready' and self.inbox is not None
                      and self.owner is None and (notice is None or notice.done())):
                    notice = asyncio.create_task(notify())
                elif not speaker and notice_id is not None and message == f'notice-accept:{notice_id}':
                    record('notice_accepted')
                    notice_accepted.set()
                elif not speaker and notice_id is not None and message == f'notice-done:{notice_id}':
                    record('notice_played', output_bytes=sent)
                    self.inbox.ack(notice_id)
                    await stop()
                elif message == 'hangup':
                    await stop()
                    await emit('idle')
                elif isinstance(message, bytes) and backend and not released:
                    if len(message) != 640:
                        close_reason = 'invalid_audio_frame'
                        sent_code = 1003
                        await ws.close(1003, 'Invalid PCM frame')
                        break
                    received += len(message)
                    backend.audio(message)
                elif message == 'release' and backend and backend.mode == 'single' and not released:
                    released = True
                    finishing = backend.release()
        except ConnectionClosed as exc:
            sent_code = exc.sent.code if exc.sent else None
            received_code = exc.rcvd.code if exc.rcvd else None
            if exc.sent and exc.sent.code == 1011 and exc.sent.reason == 'keepalive ping timeout':
                close_reason = 'keepalive_timeout'
            elif exc.rcvd:
                close_reason = 'peer_close'
            else:
                close_reason = 'transport_lost'
        except asyncio.CancelledError:
            close_reason = 'handler_cancelled'
            raise
        finally:
            record('connection_close', reason=close_reason, sent_code=sent_code,
                   received_code=received_code, age_ms=round((time.monotonic()-opened_at)*1000),
                   input_bytes=received, output_bytes=sent)
            try:
                await stop()
            finally:
                if previous_watch:
                    await previous_watch[1].wait()
                drained.set()
                if self.watch_session and self.watch_session[0] is ws:
                    self.watch_session = None
