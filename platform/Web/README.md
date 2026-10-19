# NotBlood Chosen in a web browser

The whole game compiled to WebAssembly: the same code as the other versions, including bots, all match types, mutators, co-op saves and online play. It runs in current Safari (iPad, iPhone, Mac), Chrome, Edge and Firefox. Every relay server (`server/relay.py`, see [SERVER.md](../../SERVER.md)) serves it at **`/play/`** and keeps it up to date from the newest release.

## For players

1. Open `http://<server>/play/` (or `https://`).
2. Install it:
   - **iPad/iPhone:** Share → **Add to Home Screen**.
   - **Chrome or Edge:** the install button or the menu → **Install**.

   It then has its own icon, opens full screen and **works without internet**.
3. Tap **Choose files** and pick all files of your Blood folder at once, or a `.zip` of it.
   - The files are copied into the web app's own storage in the browser, on this device only. Nothing is uploaded.
   - On a computer, **Choose a folder** works too.
   - Blood's demo recordings (`*.DEM`), programs and CD images are skipped.
4. **Play.**

What carries over:

- **Settings and saves:** kept in the browser. **Download settings & saves** makes a backup zip; choosing that zip with **Choose files** restores it.
- **Online:**
  1. Go to MULTIPLAYER → ONLINE.
  2. Leave SERVER empty to use the server the page came from, and enter its key.
  3. Choose HOST A LOBBY or FIND LOBBIES.

  The lobbies are the same ones the PC, Mac, Android and iPad versions use. Bots work too: the host's browser plays them.
- **No LAN games:** browsers can't send the UDP packets they need.
- **Controls:**
  - **Keyboard and mouse:** click the game to capture the mouse; the default keys are W/A/S/D.
  - **Game controllers.**
  - **Touch controls:** on touch screens, the same as the iPad app.

## How the port works

- **Build:** `platform/Web/CMakeLists.txt` uses Emscripten and its SDL2, Ogg and Vorbis ports. `NOTBLOOD_WEB` turns on the iPad code paths (`EDUKE32_IOS` in `compat.h`): touch controls, and gl4es for the OpenGL renderer, here on a WebGL 2 context. `source/build/src/webbits.cpp` implements `iosbits.h` for the browser.
- **The game loop:** the engine's loops never return, so the program is built with ASYNCIFY. `web_yield()` gives control back to the browser after every frame (waiting for the next animation frame), and every 12 ms while a loop only waits.
- **Files:** the game folder `/notblood` is an IndexedDB file system that the page mounts before the game starts. Settings and saves are written back after each save (`web_filesChanged()`), every 15 s, and when the page is hidden. `notblood.pk3` is packed into `notblood.data` (`/app`).
- **Network:**
  - ENet's sockets and the relay tunnel's local ends are in-memory datagrams (`webnet.cpp`, the `__EMSCRIPTEN__` block in `enet.h`).
  - The relay connection is the browser's own WebSocket (the `__EMSCRIPTEN__` part of `netrelay.cpp`).
  - A secure (https) page uses `wss://`.
- **The page:**
  - `shell/` holds the launcher (`index.html`, `app.js`), the offline cache (`sw.js`, renewed with every build), the app manifest and icons.
  - `build.sh` builds it, puts the pieces together, gzips the big files (the relay sends them compressed) and makes `NotBlood-Chosen-<version>-web.zip`. `release.yml` adds that zip to the release.

## Building

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 4.0.15 && ~/emsdk/emsdk activate 4.0.15
EMSDK=~/emsdk bash platform/Web/build.sh      # needs cmake, curl, patch, zip
```

The site is in `build-web/site`. Any web server works, as long as it sends `.wasm` as `application/wasm`. Service workers and installing need `https://`, except on `localhost`.
