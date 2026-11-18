"""HTTPS transport for watchOS, backed by the same ANNA voice service."""
import asyncio
import base64
import hmac
import math
import secrets
from time import monotonic

from aiohttp import web

from gateway import diagnostic

FRAME = 640
MAX_RECORDING = 750 * FRAME
LEASE_SECONDS = 12


class WatchSession:
    def __init__(self, gateway, mode):
        self.id = secrets.token_urlsafe(24)
        self.gateway = gateway
        self.mode = mode
        self.events = []
        self.output = bytearray()
        self.last_seen = monotonic()
        self.released = False
        self.failed = False
        self.backend = gateway.factory(self.emit, mode)
        self.opening = asyncio.create_task(self.start())
        self.finishing = None

    async def emit(self, event, data=None):
        if event == 'audio':
            self.output.extend(data)
            if len(self.output) > MAX_RECORDING:
                del self.output[:-MAX_RECORDING]
        elif len(self.events) < 256:
            if event == 'error':
                self.failed = True
            self.events.append(event if data is None else f'{event}:{data}')

    async def start(self):
        try:
            await asyncio.wait_for(self.backend.start(), 38)
        except Exception as exc:
            diagnostic('apple_watch_start_failed', reason=type(exc).__name__)
            await self.emit('error', '语音服务未接通')

    async def finish(self):
        try:
            await self.opening
            if not self.failed:
                finishing = self.backend.release()
                if finishing is not None:
                    await finishing
        except Exception as exc:
            diagnostic('apple_watch_reply_failed', reason=type(exc).__name__)
            await self.emit('error', '语音处理失败，请重试')

    async def close(self):
        tasks = [task for task in (self.opening, self.finishing) if task]
        for task in tasks:
            task.cancel()
        try:
            await asyncio.gather(*tasks, return_exceptions=True)
            await self.backend.close()
        finally:
            if self.gateway.owner is self:
                self.gateway.owner = None


def make_app(gateway, token):
    session = None
    authorization = ('Bearer ' + token).encode()

    @web.middleware
    async def authenticate(request, handler):
        headers = request.headers.getall('Authorization', [])
        if len(headers) != 1 or not hmac.compare_digest(headers[0].encode(), authorization):
            raise web.HTTPUnauthorized()
        return await handler(request)

    async def health(request):
        return web.json_response({'connected': True})

    async def notice(request):
        if gateway.inbox is None:
            return web.Response(status=204)
        value = request.query.get('since')
        try:
            since = float(value) if value is not None else None
        except ValueError:
            raise web.HTTPBadRequest()
        if since is not None and (not math.isfinite(since) or since < 0):
            raise web.HTTPBadRequest()
        pending = gateway.inbox.pending(apple_watch=True, since=since)
        if pending is None:
            return web.Response(status=204)
        key, text = pending
        return web.json_response({'id': key, 'text': text})

    async def acknowledge_notice(request):
        key = request.match_info['id']
        if not key.isascii() or not key.isdecimal() or len(key) > 18:
            raise web.HTTPBadRequest()
        if gateway.inbox is not None:
            gateway.inbox.ack(int(key), apple_watch=True)
        return web.Response(status=204)

    async def start(request):
        nonlocal session
        try:
            mode = (await request.json())['mode']
        except (ValueError, KeyError, TypeError):
            raise web.HTTPBadRequest()
        if mode not in ('single', 'call'):
            raise web.HTTPBadRequest()
        if gateway.owner is not None:
            raise web.HTTPConflict()
        session = WatchSession(gateway, mode)
        gateway.owner = session
        diagnostic('apple_watch_voice_start', mode=mode)
        return web.json_response({'id': session.id}, status=201)

    def active(request):
        if session is None or request.match_info['id'] != session.id:
            raise web.HTTPNotFound()
        return session

    async def exchange(request):
        current = active(request)
        data = await request.read()
        if current is not session:
            raise web.HTTPNotFound()
        release = request.query.get('release') == '1'
        if (len(data) % FRAME or (release and current.mode != 'single')
                or (current.mode == 'single' and data and not release)):
            raise web.HTTPBadRequest()
        current.last_seen = monotonic()
        if not current.released:
            for offset in range(0, len(data), FRAME):
                current.backend.audio(data[offset:offset + FRAME])
            if release:
                current.released = True
                current.finishing = asyncio.create_task(current.finish())
        events, current.events = current.events, []
        audio = [base64.b64encode(current.output).decode('ascii')] if current.output else []
        current.output.clear()
        return web.json_response({'events': events, 'audio': audio})

    async def close_session():
        nonlocal session
        current, session = session, None
        if current:
            await current.close()

    async def stop(request):
        active(request)
        await close_session()
        return web.Response(status=204)

    async def lifetime(app):
        async def expire():
            while True:
                await asyncio.sleep(1)
                if session and monotonic() - session.last_seen > LEASE_SECONDS:
                    await close_session()

        reaper = asyncio.create_task(expire())
        yield
        reaper.cancel()
        await asyncio.gather(reaper, return_exceptions=True)
        await close_session()

    app = web.Application(middlewares=[authenticate], client_max_size=MAX_RECORDING + 1)
    app.cleanup_ctx.append(lifetime)
    app.router.add_get('/watch/health', health)
    app.router.add_get('/watch/notices', notice)
    app.router.add_delete('/watch/notices/{id}', acknowledge_notice)
    app.router.add_post('/watch/sessions', start)
    app.router.add_post('/watch/sessions/{id}', exchange)
    app.router.add_delete('/watch/sessions/{id}', stop)
    return app


async def serve_apple_watch(gateway, token, host, port, tls):
    runner = web.AppRunner(make_app(gateway, token), access_log=None)
    await runner.setup()
    try:
        await web.TCPSite(runner, host, port, ssl_context=tls).start()
    except BaseException:
        await runner.cleanup()
        raise
    return runner
