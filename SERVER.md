# Running your own NotBlood Chosen server

NotBlood Chosen can play over the internet **without port forwarding** on the players' side. For that you run a small server, the **relay**, on any machine that's always on and reachable: a Raspberry Pi, a home server, a NAS with Docker, or a cheap VPS. The same server also hosts a **live dashboard** that shows the running games in a web browser.

There is no public default server. Everybody who plays enters your server's address and key in the game.

## How it works

```
 host's game  ──WebSocket──►  relay  ◄──WebSocket──  joining players' games
                               │
                               └──►  dashboard in a web browser (same key)
```

- **Lobbies:**
  - The host's game opens a WebSocket connection to the relay and creates a **lobby**.
  - The other games ask the relay for the lobby list and join one.
  - Every game makes only an outgoing connection, so no router needs open ports except the server's own.
- **Game traffic:**
  - The game still uses its normal network code (ENet, the same as LAN games), but every UDP packet is wrapped in a WebSocket message and passed through the relay.
  - Packets from the host carry the number of the player they're meant for. Packets to the host carry the number of the player they came from. The relay only forwards them and doesn't run the game.
- **Bots:** in online games the host's computer plays the bots, and their moves travel with the host's own. The server doesn't know or care about bots.
- **The key:**
  - Everything needs the **server key**.
  - The game and the browser prove they know it by answering a random challenge with HMAC-SHA256(key, challenge), so the key itself is never sent.
  - After 10 wrong keys in 10 minutes, that address is locked out until the 10 minutes are over.
- **The dashboard:**
  - Games send small stats messages: players, health, armor, weapons, keys, positions and the level's walls.
  - The dashboard draws lobbies, player cards, a live map (like the in-game Tab map) and an event list.
  - It has a big-screen mode and a "Blood" look.
  - Finished games go into a history.
- **LAN games on the dashboard:** a LAN game can show up on the dashboard too. In MULTIPLAYER > ONLINE, turn on SHOW MY LAN GAMES ON THE DASHBOARD on the host's machine.

The relay is one Python file (`server/relay.py`, using aiohttp) and the dashboard is three static files (`server/static/`). It needs very little memory and CPU: a Raspberry Pi 3 or 4 is plenty.

## What you need

- A machine that stays on: Linux, macOS, Windows with WSL, or a Raspberry Pi.
- **Docker** with the compose plugin (recommended), or Python 3.10+ with `pip install aiohttp`.
- A way for players to reach it. Pick one:
  - **Only your home network:** nothing more is needed.
  - **Internet, port forwarding on the server's router:** forward one TCP port to the server.
  - **Internet, no port forwarding at all:** a Cloudflare Tunnel (free) with your own domain.
  - **A VPS:** it already has a public address.

The game talks plain `ws://` (no TLS), so the game connection has to be reachable over plain HTTP. The dashboard works over both http and https.

## Setup with Docker (recommended)

1. Get the server files:
   ```sh
   git clone --depth 1 https://github.com/AdrianPol-projects/NotBlood-Chosen.git
   cd NotBlood-Chosen/server
   ```
2. Make a key. Anything works; short is easier to type on a phone:
   ```sh
   echo "BLOOD_KEY=$(openssl rand -hex 6)" > server.env
   cat server.env
   ```
3. Choose who can reach it. `docker-compose.yml` publishes the relay on `127.0.0.1:8099`, which only works for a reverse proxy on the same machine (see the internet options below). For a home network without a proxy, change the `ports:` line to:
   ```yaml
       ports:
         - "8099:8080"
   ```
4. Start it:
   ```sh
   docker compose up -d --build
   docker logs -f notblood-relay
   ```
   It restarts by itself after a reboot (`restart: unless-stopped`). The history of games is kept in the Docker volume `relay-data`.
5. Check it: open `http://<server-address>:8099/` in a browser and enter the key. You should see the empty dashboard.

### Without Docker

```sh
cd NotBlood-Chosen/server
python3 -m pip install aiohttp
mkdir -p data
BLOOD_KEY=yourkey PORT=8099 DATA_DIR=./data python3 relay.py
```

To keep it running, use a systemd service, `tmux`, or the Windows task scheduler.

## Connecting from the game

MULTIPLAYER > ONLINE:

| Field | What to enter |
| --- | --- |
| SERVER | `192.168.1.20:8099` (home network), `my.domain.com` (proxy or tunnel on port 80) or `ws://host:port/ws`. Without a port the game uses 80. |
| SERVER KEY | the `BLOOD_KEY` from `server.env` |
| LOBBY NAME | any name for your game |
| PLAYERS | how many people the lobby waits for |

The host picks **HOST A LOBBY** and sets up the game as usual: map, mode, BOTS, teams. The others pick **FIND LOBBIES** or **JOIN LOBBY BY NAME**. The game remembers the server and key.

Everybody needs the same NotBlood Chosen version (the lobby list shows each lobby's version and mod), the same Blood files, and the same add-ons and mods.

## Reaching it from the internet

**Option A: port forwarding on the server's router.**
1. Publish the port on all addresses (`"8099:8080"`, as in step 3 above).
2. Forward TCP 8099 on the router to the server.
3. Players enter `your-public-ip:8099`, or a dynamic DNS name such as DuckDNS.

**Option B: a reverse proxy (Caddy or nginx) on the same machine.** Keep `127.0.0.1:8099` and pass a hostname through. WebSockets must be passed through, and `/ws` has to work over plain http.

Caddy (`http://` in front keeps it on plain HTTP; Caddy passes WebSockets through by itself):
```
http://blood.example.com {
    reverse_proxy 127.0.0.1:8099
}
```

nginx:
```nginx
server {
    listen 80;
    server_name blood.example.com;
    location / {
        proxy_pass http://127.0.0.1:8099;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 120s;
    }
}
```

**Option C: Cloudflare Tunnel, no open ports at all.** This is how the author's Raspberry Pi runs it.
1. Install `cloudflared` and create a tunnel: `cloudflared tunnel create blood`.
2. Add an ingress rule (`hostname: blood.example.com`, `service: http://127.0.0.1:8099`) and route the DNS name: `cloudflared tunnel route dns blood blood.example.com`.
3. In the Cloudflare dashboard, leave **Always Use HTTPS** off for that hostname, because the game uses plain `ws://`.
4. Add `TRUST_PROXY=1` to `server.env` and run `docker compose up -d`. Behind the tunnel every connection comes from 127.0.0.1, and with this setting the relay uses Cloudflare's `CF-Connecting-IP` header for the per-address limits and the wrong-key lockout. **Only** set it when the relay can't be reached except through Cloudflare, or anyone could fake their address.

## Settings

In `server.env`:

| Variable | Default | Meaning |
| --- | --- | --- |
| `BLOOD_KEY` | (none, required) | the server key for games and the dashboard |
| `TRUST_PROXY` | `0` | `1`: use `CF-Connecting-IP` (Cloudflare only) |
| `PORT` | `8080` | the port inside the container (or of `relay.py`) |
| `DATA_DIR` | `/data` | where the game history is kept |

Built-in limits: 200 connections, 24 per address, 50 lobbies. A player who doesn't answer for 45 seconds is dropped.

To change the key, edit `server.env` and run `docker compose up -d`. Players then enter the new key.

## Updating

```sh
cd NotBlood-Chosen && git pull && cd server && docker compose up -d --build
```

You can automate this with a cron job or systemd timer that runs the same commands every few minutes. Rebuild only when `git pull` brought changes in `server/`.

## Logs and troubleshooting

- `docker logs notblood-relay` has a line for every lobby and player. When a player leaves, it says why (left, connection lost, no answer for 45 s, the host left), how long they played, their ping (average and worst), and pauses longer than a second.
- `http://<server>/healthz` answers `{"ok": true, ...}` with the number of lobbies when the relay runs.
- **"Connecting..." never finishes:** check the address and port, and that `/ws` is reachable over plain http (not forced to https).
- **Wrong key:** after 10 wrong tries in 10 minutes that address is locked out for the rest of the 10 minutes. Wait, or restart the container.
- **Versions differ:** everybody needs the same NotBlood Chosen version.
- **Lag:** the relay adds the trip to the server and back. A server close to the players (same country) keeps it low, and the dashboard can show each player's ping (settings: "Ping to the server").
- **Testing:** `server/test_relay.py` tests the protocol against a running server, and `server/simulate.py` sends a fake game so the dashboard has something to show.

## Security notes

- The container runs as an unprivileged user with a read-only file system, no Linux capabilities and 256 MB of memory at most.
- Only people with the key can create or join lobbies or see the dashboard. Treat the key like a password for your friends.
- The game connection isn't encrypted (plain WebSocket). Nothing secret is sent: the key itself never crosses the network, only the answer to a one-time challenge.
