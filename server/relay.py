#!/usr/bin/env python3
"""NotBlood online relay and live game dashboard.

Games can't reach each other directly over the internet (routers, NAT), so every player opens one
WebSocket to this server and the game's normal network traffic is passed through it:

  host game  <-- ws -->  relay  <-- ws -->  joining games

The game still runs its usual ENet (UDP) connection, only tunnelled: each WebSocket binary frame
carries one UDP datagram. Frames from the host start with the id of the player they are meant
for; frames to the host start with the id of the player they came from.

The same connection carries live game stats (players, health, weapons, positions, the level's
walls) which the dashboard at / shows in a browser.

Everything needs the server key: game and browser prove they know it by answering a random
challenge with HMAC-SHA256(key, challenge), so the key itself never crosses the network.
"""

import asyncio
import hashlib
import hmac
import json
import logging
import os
import re
import secrets
import struct
import time
from collections import deque
from pathlib import Path

from aiohttp import WSMsgType, web

PROTOCOL = 1

KEY = os.environ.get("BLOOD_KEY", "")
PORT = int(os.environ.get("PORT", "8080"))
DATA_DIR = Path(os.environ.get("DATA_DIR", "/data"))
STATIC_DIR = Path(__file__).resolve().parent / "static"

MAX_CONNECTIONS = 200
MAX_CONNECTIONS_PER_IP = 24
MAX_LOBBIES = 50
MAX_GAME_FRAME = 64 * 1024
MAX_STATS = 256 * 1024
MAX_MAP = 4 * 1024 * 1024
AUTH_TIMEOUT = 15
HISTORY_KEEP = 100
DASH_INTERVAL = 0.2
PING_INTERVAL = 2.0     # WebSocket ping to every game: measures its ping and notices dead connections
DEAD_AFTER = 45         # no pong for this long: the game is gone (ENet itself gives up after ~30 s)
SEND_QUEUE_MAX = 4000   # game packets waiting for one player; beyond that they are dropped (ENet resends)
PAUSE_LOG = 2.0         # a player whose packets stop for this long during a game gets a log line
# Behind Cloudflare (and Caddy) every connection comes from 127.0.0.1; with TRUST_PROXY=1 the real
# address is taken from CF-Connecting-IP, which Cloudflare sets itself. X-Forwarded-For is never
# used: its first entry is whatever the client put there.
TRUST_PROXY = os.environ.get("TRUST_PROXY", "0") == "1"

log = logging.getLogger("relay")

NAME_RE = re.compile(r"^[A-Za-z0-9 _.\-]{1,24}$")
WALL_CHARS = str.maketrans("", "", "0123456789,- ")


def clean_name(s, limit=24):
    s = "".join(c for c in s if c.isprintable() and c not in "\t\r\n")
    return s.strip()[:limit]


def client_ip(request):
    if TRUST_PROXY:
        ip = request.headers.get("CF-Connecting-IP", "").strip()
        if ip:
            return ip
    return request.remote or "?"


def fmt_duration(sec):
    sec = int(sec)
    return "%dh%02dm" % (sec // 3600, sec % 3600 // 60) if sec >= 3600 else "%dm%02ds" % (sec // 60, sec % 60)


class Guard:
    """Connection counting and failed-key lockout, per IP."""

    def __init__(self):
        self.conns = {}
        self.total = 0
        self.failures = {}

    def locked(self, ip):
        now = time.time()
        f = [t for t in self.failures.get(ip, []) if now - t < 600]
        self.failures[ip] = f
        return len(f) >= 10

    def fail(self, ip):
        self.failures.setdefault(ip, []).append(time.time())

    def enter(self, ip):
        if self.total >= MAX_CONNECTIONS or self.conns.get(ip, 0) >= MAX_CONNECTIONS_PER_IP:
            return False
        self.total += 1
        self.conns[ip] = self.conns.get(ip, 0) + 1
        return True

    def leave(self, ip):
        self.total -= 1
        self.conns[ip] -= 1
        if not self.conns[ip]:
            del self.conns[ip]


guard = Guard()


class Lobby:
    def __init__(self, name, kind, host, max_players, host_name, mod, version):
        self.name = name
        self.kind = kind  # "online": joinable through the relay; "lan": stats only
        self.host = host
        self.max_players = max_players
        self.host_name = host_name
        self.mod = mod
        self.version = version
        self.clients = {}  # id -> Conn
        self.next_id = 1
        self.state = "waiting"
        self.created = time.time()
        self.started = None
        self.stats = None
        self.stats_time = 0
        self.map = None
        self.map_id = None
        self.levels = []
        self.player_names = set()
        self.net_log = {}  # player name -> connection summary of players who left (for the history)

    def conns(self):
        return ([self.host] if self.host else []) + list(self.clients.values())

    def net(self):
        """Ping of each player to this server, for the dashboard (online lobbies only)."""
        if self.kind != "online":
            return None
        return {c.player_name: c.net_summary() for c in self.conns() if c.rtt_n}

    def summary(self):
        return {
            "name": self.name,
            "kind": self.kind,
            "host": self.host_name,
            "players": [self.host_name] + [c.player_name for c in self.clients.values()],
            "max": self.max_players,
            "state": self.state,
            "mod": self.mod,
            "version": self.version,
            "created": self.created,
            "started": self.started,
            "map_id": self.map_id,
            "stats": self.stats,
            "stats_age": round(time.time() - self.stats_time, 1) if self.stats_time else None,
            "net": self.net(),
        }


class Relay:
    def __init__(self):
        self.lobbies = {}  # lower case name -> Lobby
        self.dashboards = set()
        self.history = []
        self.history_file = DATA_DIR / "history.json"
        try:
            self.history = json.loads(self.history_file.read_text())[-HISTORY_KEEP:]
        except Exception:
            self.history = []

    def save_history(self):
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            tmp = self.history_file.with_suffix(".tmp")
            tmp.write_text(json.dumps(self.history))
            tmp.replace(self.history_file)
        except Exception as e:
            log.warning("could not save history: %s", e)

    def close_lobby(self, lobby):
        if self.lobbies.get(lobby.name.lower()) is lobby:
            del self.lobbies[lobby.name.lower()]
        if lobby.started:
            players = sorted(lobby.player_names)
            net = dict(lobby.net_log)
            if lobby.kind == "online":
                for c in lobby.conns():
                    net.setdefault(c.player_name, c.net_summary(final=True))
            self.history.append({
                "name": lobby.name, "kind": lobby.kind, "host": lobby.host_name, "players": players,
                "mod": lobby.mod, "started": lobby.started, "ended": time.time(),
                "levels": lobby.levels[-20:],
                "final": lobby.stats,
                "net": net or None,
            })
            self.history = self.history[-HISTORY_KEEP:]
            self.save_history()
        log.info("lobby %r closed", lobby.name)

    def snapshot(self):
        return {
            "op": "state",
            "time": time.time(),
            "lobbies": [l.summary() for l in self.lobbies.values()],
        }


relay = Relay()


class Conn:
    """One game's WebSocket.

    Everything sent to a game goes through its own queue and writer task, so a slow or stalled
    player never holds up the loop that reads (and passes on) another player's packets."""

    def __init__(self, ws, ip):
        self.ws = ws
        self.ip = ip
        self.lobby = None
        self.lobby_name = None
        self.peer_id = 0
        self.player_name = ""
        self.is_host = False
        self.queue = deque()
        self.wake = asyncio.Event()
        self.closing = False
        self.writer = asyncio.create_task(self.write_loop())
        # connection quality, logged when the game leaves and kept in the game history
        self.opened = time.time()
        self.reason = "connection closed"
        self.frames_in = self.frames_out = self.bytes_in = self.bytes_out = 0
        self.dropped = 0
        self.last_frame = None
        self.max_pause = 0.0
        self.pauses = 0
        self.rtt = 0.0
        self.rtt_sum = 0.0
        self.rtt_n = 0
        self.rtt_max = 0.0
        self.last_pong = time.monotonic()

    def push(self, item):
        if not self.closing:
            self.queue.append(item)
            self.wake.set()

    async def text(self, *fields):
        self.push("\t".join(str(f) for f in fields))

    async def binary(self, data):
        if len(self.queue) >= SEND_QUEUE_MAX:
            self.dropped += 1  # this game stopped reading; ENet sends lost packets again
            return
        self.frames_out += 1
        self.bytes_out += len(data)
        self.push(data)

    async def write_loop(self):
        try:
            while True:
                while not self.queue:
                    self.wake.clear()
                    if self.closing:
                        return
                    await self.wake.wait()
                item = self.queue.popleft()
                if isinstance(item, (bytes, bytearray)):
                    await self.ws.send_bytes(item)
                else:
                    await self.ws.send_str(item)
        except Exception:
            pass  # the connection is gone; its reader notices

    async def finish(self):
        """Send what is still queued (like host_left), then stop the writer."""
        self.closing = True
        self.wake.set()
        try:
            await asyncio.wait_for(asyncio.shield(self.writer), 5)
        except Exception:
            self.writer.cancel()

    def got_frame(self, size, playing):
        now = time.monotonic()
        self.frames_in += 1
        self.bytes_in += size
        if playing and self.last_frame is not None:
            pause = now - self.last_frame
            if pause >= 1.0:
                self.pauses += 1
                if pause > self.max_pause:
                    self.max_pause = pause
                if pause >= PAUSE_LOG:
                    log.info("%s: no game packets for %.1f s (lobby %r)", self.player_name, pause, self.lobby_name)
        self.last_frame = now

    def got_pong(self, data):
        now = time.monotonic()
        self.last_pong = now
        if len(data) == 8:
            rtt = (now - struct.unpack("!d", data)[0]) * 1000
            if 0 <= rtt < 60000:
                self.rtt = rtt
                self.rtt_sum += rtt
                self.rtt_n += 1
                self.rtt_max = max(self.rtt_max, rtt)

    def net_summary(self, final=False):
        d = {"ping": round(self.rtt), "avg": round(self.rtt_sum / self.rtt_n) if self.rtt_n else None,
             "max": round(self.rtt_max), "pauses": self.pauses, "dropped": self.dropped}
        if final:
            d.update(reason=self.reason, online=round(time.time() - self.opened),
                     longest_pause=round(self.max_pause, 1), packets_in=self.frames_in, packets_out=self.frames_out)
        return d

    def describe(self):
        parts = ["online %s" % fmt_duration(time.time() - self.opened)]
        if self.rtt_n:
            parts.append("ping avg %d ms, max %d ms" % (self.rtt_sum / self.rtt_n, self.rtt_max))
        parts.append("%d packets in, %d out" % (self.frames_in, self.frames_out))
        if self.pauses:
            parts.append("%d pauses over 1 s, longest %.1f s" % (self.pauses, self.max_pause))
        if self.dropped:
            parts.append("%d packets dropped because the game stopped reading" % self.dropped)
        return ", ".join(parts)


async def ping_loop(conn):
    while not conn.ws.closed:
        await asyncio.sleep(PING_INTERVAL)
        if time.monotonic() - conn.last_pong > DEAD_AFTER:
            conn.reason = "no answer for %d s" % DEAD_AFTER
            await conn.ws.close(code=1001, message=b"no answer")
            return
        try:
            await conn.ws.ping(struct.pack("!d", time.monotonic()))
        except Exception:
            return


async def authenticate(ws, ip):
    """Challenge/response with the server key. Returns True when the peer knows the key."""
    nonce = secrets.token_hex(16)
    await ws.send_str("challenge\t" + nonce)
    try:
        msg = await ws.receive(timeout=AUTH_TIMEOUT)
    except asyncio.TimeoutError:
        return None
    if msg.type != WSMsgType.TEXT:
        return None
    fields = msg.data.split("\t")
    if len(fields) < 2 or fields[0] != "auth":
        return None
    want = hmac.new(KEY.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    if not KEY or not hmac.compare_digest(want, fields[1].strip().lower()):
        guard.fail(ip)
        await ws.send_str("error\tWrong server key")
        return None
    return fields[2:]


async def game_ws(request):
    ip = client_ip(request)
    if guard.locked(ip) or not guard.enter(ip):
        raise web.HTTPTooManyRequests()
    # autoping off: pongs are read below to measure each game's ping
    ws = web.WebSocketResponse(max_msg_size=MAX_MAP + 1024, heartbeat=None, autoping=False)
    await ws.prepare(request)
    conn = Conn(ws, ip)
    pinger = None
    try:
        extra = await authenticate(ws, ip)
        if extra is None:
            return ws
        proto = extra[0] if extra else ""
        if proto != str(PROTOCOL):
            await conn.text("error", "Server protocol %d, game protocol %s - update the game or the server" % (PROTOCOL, proto))
            return ws
        await conn.text("ok", PROTOCOL)
        pinger = asyncio.create_task(ping_loop(conn))
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                await on_game_binary(conn, msg.data)
            elif msg.type == WSMsgType.TEXT:
                if not await on_game_text(conn, msg.data):
                    conn.reason = "left the game"
                    break
            elif msg.type == WSMsgType.PONG:
                conn.got_pong(msg.data)
            elif msg.type == WSMsgType.PING:
                await ws.pong(msg.data)
            elif msg.type == WSMsgType.ERROR:
                conn.reason = "connection error: %s" % (ws.exception() or "unknown")
                break
            else:
                break
        if conn.reason == "connection closed" and ws.close_code is not None:
            conn.reason = ("closed by the game" if ws.close_code in (1000, 1001)
                           else "connection lost (code %s)" % ws.close_code)
    except Exception as e:
        conn.reason = "connection error: %s" % e
    finally:
        if pinger:
            pinger.cancel()
        await drop_conn(conn)
        await conn.finish()
        guard.leave(ip)
    return ws


async def drop_conn(conn):
    lobby = conn.lobby
    if not lobby:
        if conn.lobby_name:  # its lobby was closed by the host leaving
            log.info("%s disconnected from lobby %r: %s; %s", conn.player_name, conn.lobby_name, conn.reason, conn.describe())
        return
    conn.lobby = None
    if conn.is_host:
        log.info("%s (host) left lobby %r: %s; %s", conn.player_name, lobby.name, conn.reason, conn.describe())
        relay.close_lobby(lobby)
        for c in list(lobby.clients.values()):
            c.lobby = None
            c.reason = "the host left"
            await c.text("host_left")
            asyncio.create_task(close_after_flush(c))
    else:
        lobby.clients.pop(conn.peer_id, None)
        if lobby.kind == "online":
            lobby.net_log[conn.player_name] = conn.net_summary(final=True)
        if lobby.host:
            await lobby.host.text("peer_leave", conn.peer_id)
        log.info("%s left lobby %r: %s; %s", conn.player_name, lobby.name, conn.reason, conn.describe())


async def close_after_flush(conn):
    await conn.finish()
    await conn.ws.close()


async def on_game_binary(conn, data):
    lobby = conn.lobby
    if not lobby or lobby.kind != "online" or len(data) > MAX_GAME_FRAME:
        return
    conn.got_frame(len(data), lobby.state == "playing")
    if conn.is_host:
        if len(data) < 2:
            return
        target = lobby.clients.get(data[0])
        if target:
            await target.binary(data[1:])
    else:
        await lobby.host.binary(bytes([conn.peer_id]) + data)


async def on_game_text(conn, text):
    f = text.split("\t")
    cmd = f[0]
    lobby = conn.lobby

    if cmd == "list":
        lines = ["lobbies"]
        for l in relay.lobbies.values():
            if l.kind != "online":
                continue
            lines.append("\t".join(str(x) for x in (
                l.name, l.host_name, 1 + len(l.clients), l.max_players, l.state, l.mod, l.version)))
        conn.push("\n".join(lines))

    elif cmd in ("host", "report") and not lobby:
        # host <lobby> <max players> <player name> <mod> <game version>
        if len(f) < 6:
            await conn.text("error", "Bad request")
            return True
        name, player, mod, version = clean_name(f[1]), clean_name(f[3], 16), clean_name(f[4], 64), clean_name(f[5], 64)
        try:
            max_players = max(2, min(8, int(f[2])))
        except ValueError:
            max_players = 2
        if not NAME_RE.match(name):
            await conn.text("error", "Lobby names can use letters, digits, space, _ . -")
            return True
        if name.lower() in relay.lobbies:
            await conn.text("error", "A lobby called '%s' already exists" % name)
            return True
        if len(relay.lobbies) >= MAX_LOBBIES:
            await conn.text("error", "Too many lobbies on this server")
            return True
        lobby = Lobby(name, "online" if cmd == "host" else "lan", conn, max_players, player or "Host", mod, version)
        lobby.player_names.add(lobby.host_name)
        relay.lobbies[name.lower()] = lobby
        conn.lobby, conn.is_host, conn.player_name, conn.lobby_name = lobby, True, lobby.host_name, lobby.name
        log.info("%s %s lobby %r (%d players, mod %r, %s) from %s", lobby.host_name,
                 "hosts" if cmd == "host" else "reports", name, max_players, mod, version, conn.ip)
        await conn.text("hosted", name)

    elif cmd == "join" and not lobby:
        # join <lobby> <player name> <mod> <game version>
        if len(f) < 5:
            await conn.text("error", "Bad request")
            return True
        lobby = relay.lobbies.get(clean_name(f[1]).lower())
        player, mod, version = clean_name(f[2], 16) or "Player", clean_name(f[3], 64), clean_name(f[4], 64)
        if not lobby or lobby.kind != "online":
            await conn.text("error", "No lobby called '%s'" % clean_name(f[1]))
            return True
        if version != lobby.version:
            await conn.text("error", "The host runs %s, you run %s - everybody needs the same version" % (lobby.version, version))
            return True
        if mod != lobby.mod:
            await conn.text("error", "The host plays with mod '%s', you have '%s'" % (lobby.mod or "none", mod or "none"))
            return True
        if lobby.state != "waiting":
            await conn.text("error", "That game has already started")
            return True
        if 1 + len(lobby.clients) >= lobby.max_players:
            await conn.text("error", "That lobby is full")
            return True
        while lobby.next_id in lobby.clients or lobby.next_id == 0:
            lobby.next_id = lobby.next_id % 255 + 1
        conn.peer_id = lobby.next_id
        lobby.next_id = lobby.next_id % 255 + 1
        conn.lobby, conn.player_name, conn.lobby_name = lobby, player, lobby.name
        lobby.clients[conn.peer_id] = conn
        lobby.player_names.add(player)
        log.info("%s joined lobby %r as %d", player, lobby.name, conn.peer_id)
        await lobby.host.text("peer_join", conn.peer_id, player)
        await conn.text("joined", lobby.name, conn.peer_id, lobby.host_name)

    elif cmd == "state" and lobby and conn.is_host and len(f) >= 2:
        state = clean_name(f[1], 32)
        if state == "playing" and not lobby.started:
            lobby.started = time.time()
        lobby.state = state

    elif cmd == "stats" and lobby and conn.is_host and len(f) >= 2:
        if len(f[1]) <= MAX_STATS:
            try:
                stats = json.loads(f[1])
            except ValueError:
                return True
            if isinstance(stats, dict):
                lobby.stats = stats
                lobby.stats_time = time.time()
                if not lobby.started:
                    lobby.started = time.time()
                for p in stats.get("players", []):
                    if isinstance(p, dict) and isinstance(p.get("name"), str):
                        lobby.player_names.add(p["name"][:16])
                level = stats.get("level")
                if isinstance(level, str) and (not lobby.levels or lobby.levels[-1] != level):
                    lobby.levels.append(level[:64])

    elif cmd == "map" and lobby and conn.is_host and len(f) >= 3:
        # a flat list of numbers; checked without parsing it, which would hold up the packets
        # of every game for a moment on a big level
        walls = f[2]
        if len(walls) <= MAX_MAP and walls[:1] == "[" and walls[-1:] == "]" and not walls[1:-1].translate(WALL_CHARS):
            lobby.map = walls
            lobby.map_id = clean_name(f[1], 64)
            for d in list(relay.dashboards):
                if d.watching == lobby.name:
                    asyncio.create_task(d.send_map(lobby))

    elif cmd == "ping":
        await conn.text("pong", *f[1:2])

    elif cmd == "bye":
        return False

    return True


class Dash:
    def __init__(self, ws):
        self.ws = ws
        self.watching = None
        self.sent_map = None
        self.busy = False  # an update is still on its way to this browser

    async def send_str(self, text, timeout=10):
        if not self.ws.closed:
            try:
                await asyncio.wait_for(self.ws.send_str(text), timeout)
            except asyncio.TimeoutError:
                await self.ws.close()  # a browser that can't keep up reconnects on its own
            except Exception:
                pass

    async def send(self, obj):
        await self.send_str(json.dumps(obj, separators=(",", ":")))

    async def send_map(self, lobby):
        if lobby.map and self.sent_map != (lobby.name, lobby.map_id) and not self.ws.closed:
            self.sent_map = (lobby.name, lobby.map_id)
            await self.send_str('{"op":"map","lobby":%s,"map_id":%s,"walls":%s}' % (
                json.dumps(lobby.name), json.dumps(lobby.map_id), lobby.map), timeout=60)

    async def push(self, snap):
        try:
            await self.send_str(snap, timeout=5)
            if self.watching:
                lobby = relay.lobbies.get(str(self.watching).lower())
                if lobby:
                    await self.send_map(lobby)
        finally:
            self.busy = False


async def dash_ws(request):
    ip = client_ip(request)
    if guard.locked(ip) or not guard.enter(ip):
        raise web.HTTPTooManyRequests()
    ws = web.WebSocketResponse(max_msg_size=64 * 1024, heartbeat=30)
    await ws.prepare(request)
    dash = Dash(ws)
    try:
        if await authenticate(ws, ip) is None:
            return ws
        await dash.send({"op": "ok", "history": relay.history[-30:]})
        relay.dashboards.add(dash)
        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                continue
            try:
                req = json.loads(msg.data)
            except ValueError:
                continue
            if req.get("op") == "watch":
                dash.watching = req.get("lobby")
                dash.sent_map = None
                lobby = relay.lobbies.get(str(dash.watching).lower())
                if lobby:
                    await dash.send_map(lobby)
            elif req.get("op") == "history":
                await dash.send({"op": "history", "history": relay.history[-30:]})
    finally:
        relay.dashboards.discard(dash)
        guard.leave(ip)
    return ws


async def dash_pusher(app):
    async def loop():
        while True:
            await asyncio.sleep(DASH_INTERVAL)
            if not relay.dashboards:
                continue
            snap = json.dumps(relay.snapshot(), separators=(",", ":"))
            # each browser on its own: a slow one (a phone on a bad network) skips updates
            # instead of holding up the others
            for d in list(relay.dashboards):
                if not d.ws.closed and not d.busy:
                    d.busy = True
                    asyncio.create_task(d.push(snap))
    task = asyncio.create_task(loop())
    yield
    task.cancel()


def asset_version():
    """Changes with the dashboard files, so caches (Cloudflare keeps .js for hours) never serve old ones."""
    h = hashlib.sha256()
    for name in ("app.js", "style.css"):
        h.update((STATIC_DIR / name).read_bytes())
    return h.hexdigest()[:12]


INDEX_HTML = None


async def index(request):
    global INDEX_HTML
    if INDEX_HTML is None:
        v = asset_version()
        INDEX_HTML = ((STATIC_DIR / "index.html").read_text()
                      .replace('static/app.js"', 'static/app.js?v=%s"' % v)
                      .replace('static/style.css"', 'static/style.css?v=%s"' % v))
    return web.Response(text=INDEX_HTML, content_type="text/html", headers={"Cache-Control": "no-cache"})


async def health(request):
    return web.json_response({"ok": True, "lobbies": len(relay.lobbies), "protocol": PROTOCOL})


# ---- the web version of the game (/play) -----------------------------------------------------------
#
# The relay also serves NotBlood Chosen for web browsers at /play/: the page, the game (WebAssembly)
# and its offline cache. It downloads the web build itself, from the newest release of WEB_REPO on
# GitHub (the asset ending in -web.zip), when it starts and every WEB_UPDATE_HOURS, into
# WEB_DIR/current. Players bring their own Blood files; nothing of the game data is on the server.

WEB_DIR = Path(os.environ.get("WEB_DIR", str(DATA_DIR / "web")))
WEB_REPO = os.environ.get("WEB_REPO", "AdrianPol-projects/NotBlood-Chosen")
WEB_UPDATE_HOURS = float(os.environ.get("WEB_UPDATE_HOURS", "6"))  # 0: never download
WEB_MAX_ZIP = 200 * 1024 * 1024
WEB_TYPES = {
    ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm",
    ".data": "application/octet-stream", ".webmanifest": "application/manifest+json", ".png": "image/png",
    ".woff2": "font/woff2", ".txt": "text/plain",
}
WEB_MISSING_HTML = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>NotBlood Chosen</title><body style="background:#070202;color:#e8dcd6;font:18px system-ui;padding:2em">
<p>The web version isn't on this server yet. It is downloaded automatically; try again in a few minutes.</p>"""


async def play_redirect(request):
    raise web.HTTPFound("/play/")


async def play(request):
    name = request.match_info.get("path", "") or "index.html"
    root = (WEB_DIR / "current").resolve()
    try:
        path = (root / name).resolve()
        path.relative_to(root)
    except (ValueError, OSError):
        raise web.HTTPNotFound()
    if not path.is_file():
        if name == "index.html":
            return web.Response(text=WEB_MISSING_HTML, content_type="text/html", status=503,
                                headers={"Cache-Control": "no-cache"})
        raise web.HTTPNotFound()
    headers = {"Cache-Control": "no-cache"}  # always checked with the server; the page's own cache works offline
    ctype = WEB_TYPES.get(path.suffix)
    if ctype:
        headers["Content-Type"] = ctype
    # FileResponse sends notblood.wasm.gz etc. instead when the browser takes gzip
    return web.FileResponse(path, headers=headers)


def web_installed():
    try:
        return (WEB_DIR / "installed.txt").read_text().strip()
    except OSError:
        return ""


def web_unpack(zip_path, marker):
    """Unpacks a downloaded web build next to the current one and swaps them (runs in a thread)."""
    import shutil
    import zipfile
    new, cur, old = WEB_DIR / "new", WEB_DIR / "current", WEB_DIR / "old"
    for d in (new, old):
        shutil.rmtree(d, ignore_errors=True)
    new.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        if "index.html" not in names or "notblood.wasm" not in names:
            raise ValueError("not a NotBlood web build")
        for n in names:
            if n.startswith("/") or ".." in n.split("/"):
                raise ValueError("bad file name in the zip: %r" % n)
        z.extractall(new)
    if cur.exists():
        cur.rename(old)
    new.rename(cur)
    shutil.rmtree(old, ignore_errors=True)
    (WEB_DIR / "installed.txt").write_text(marker + "\n")
    zip_path.unlink()


async def web_update_once():
    import aiohttp
    WEB_DIR.mkdir(parents=True, exist_ok=True)
    gh = {"User-Agent": "notblood-relay", "Accept": "application/vnd.github+json"}
    timeout = aiohttp.ClientTimeout(total=900)
    async with aiohttp.ClientSession(timeout=timeout, headers=gh) as http:
        async with http.get("https://api.github.com/repos/%s/releases/latest" % WEB_REPO) as r:
            if r.status != 200:
                log.warning("web version: GitHub answered %d", r.status)
                return
            release = await r.json()
        asset = next((a for a in release.get("assets", []) if a.get("name", "").endswith("-web.zip")), None)
        if not asset:
            return
        marker = "%s %s %s" % (release.get("tag_name"), asset.get("id"), asset.get("updated_at"))
        if marker == web_installed() and (WEB_DIR / "current" / "index.html").is_file():
            return
        if asset.get("size", 0) > WEB_MAX_ZIP:
            log.warning("web version: %s is too big", asset.get("name"))
            return
        zip_path = WEB_DIR / "download.zip"
        got = 0
        async with http.get(asset["browser_download_url"], headers={"Accept": "application/octet-stream"}) as r:
            if r.status != 200:
                log.warning("web version: download answered %d", r.status)
                return
            with open(zip_path, "wb") as f:
                async for chunk in r.content.iter_chunked(1 << 16):
                    got += len(chunk)
                    if got > WEB_MAX_ZIP:
                        raise ValueError("download too big")
                    f.write(chunk)
    await asyncio.to_thread(web_unpack, zip_path, marker)
    log.info("web version: installed %s (%s, %d KB)", asset.get("name"), release.get("tag_name"), got // 1024)


async def web_updater(app):
    async def loop():
        await asyncio.sleep(3)
        while True:
            try:
                await web_update_once()
            except Exception as e:  # network trouble, GitHub limits: try again next time
                log.warning("web version: update failed: %s", e)
            # not there yet (the release is still being built): look again soon
            have = (WEB_DIR / "current" / "index.html").is_file()
            await asyncio.sleep(WEB_UPDATE_HOURS * 3600 if have else 600)
    task = asyncio.create_task(loop()) if WEB_UPDATE_HOURS > 0 else None
    yield
    if task:
        task.cancel()


@web.middleware
async def security_headers(request, handler):
    resp = await handler(request)
    resp.headers.setdefault("X-Content-Type-Options", "nosniff")
    resp.headers.setdefault("Referrer-Policy", "no-referrer")
    resp.headers.setdefault("X-Frame-Options", "DENY")
    if request.path.startswith("/static/"):
        # versioned by the page (?v=...), so they may be kept for long
        resp.headers["Cache-Control"] = "public, max-age=31536000, immutable" if request.query.get("v") else "no-cache"
    return resp


def make_app():
    app = web.Application(middlewares=[security_headers], client_max_size=64 * 1024)
    app.router.add_get("/", index)
    app.router.add_get("/ws", game_ws)
    app.router.add_get("/dash", dash_ws)
    app.router.add_get("/healthz", health)
    app.router.add_static("/static/", STATIC_DIR, show_index=False)
    app.router.add_get("/play", play_redirect)
    app.router.add_get("/play/{path:.*}", play)
    app.cleanup_ctx.append(dash_pusher)
    app.cleanup_ctx.append(web_updater)
    return app


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if not KEY:
        log.warning("BLOOD_KEY is not set - nobody can connect")
    web.run_app(make_app(), port=PORT, access_log=None)
