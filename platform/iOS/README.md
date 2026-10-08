# NotBlood for iPad / iPhone

An iOS/iPadOS build of [NotBlood](https://github.com/clipmove/NotBlood) with touch controls, Magic Keyboard trackpad/mouse support and LAN multiplayer.

## Getting the IPA

Every push to the `ios` branch builds an **unsigned** IPA on a GitHub macOS runner:

* **Releases → `ios-latest`** – always the newest build (open this on the iPad to download directly), or
* **Actions → iOS IPA → latest run → Artifacts**.

Sign and install it with Signulous, Sideloadly, AltStore/SideStore or your own developer account. Unsigned IPAs are what these services expect.

## Adding the game data

1. Launch NotBlood once. It creates its folder and then asks for the game files.
2. Open **Files → On My iPad → NotBlood**.
3. Copy **everything** from your Blood folder into it. That's `BLOOD.RFF`, `GUI.RFF`, `SOUNDS.RFF`, `SURFACE.DAT`, `TILES000-017.ART`, `VOXEL.DAT`, `BLOOD.INI` and so on, from GOG/Steam *One Unit Whole Blood*, *Fresh Supply* or the CD.
   * Cryptic Passage: include `CRYPTIC.INI`, `CP*.MAP` and the `CP*` art/sequence files.
   * CD music: copy the music tracks (`*.ogg`) too.
   * Cutscenes: copy the `movie/` folder (`*.SMK`).
4. Go back to NotBlood and tap **Check again**.

Config (`notblood.cfg`), savegames and `touchcontrols.cfg` live in the same folder, so you can back them up from Files.

## Touch controls

| Area | Action |
|---|---|
| Left side of the screen | Floating move stick. It appears where your thumb lands. |
| Anywhere else | Drag to turn and look up/down. |
| FIRE / ALT | Fire. Dragging while holding also aims (can be turned off). |
| JUMP, DUCK, USE | Jump, crouch (toggle by default), open/use |
| WPN◄ / WPN► / WPNS | Previous/next weapon, or a weapon picker (tap or slide onto a slot) |
| ◄ ITEM ► | Inventory left/use/right |
| MAP, MENU | Automap, game menu |

Optional buttons can be switched on under **Touch settings → Buttons**: 180° turn, run, center view, quick save/load, medkit, and keyboard.

**In menus:** tap menu entries directly. **BACK** = Escape, the arrow pad and **OK** navigate, **KEYB** opens the on-screen keyboard (savegame names, the IP address field, console), and **TOUCH** opens the touch settings.

### Touch settings (menu → TOUCH)

* Look speed X/Y, invert Y, stick dead zone, opacity, button size
* Floating or fixed stick, always run, drag-fire-to-aim, auto-hide when a keyboard/mouse is used
* **Edit layout:** drag any button or the stick to move it, pinch (or −/+) to resize, HIDE to remove one. **Buttons** tab: show or hide each button. **Reset all** restores the defaults.

## Keyboard, trackpad and mouse

A Magic Keyboard or Bluetooth keyboard/mouse works like on PC: mouse-look with the pointer locked, plus all key bindings from the game's options. The touch overlay hides itself while you use them (configurable) and comes back when you touch the screen. The Magic Keyboard has no Esc key, so use the on-screen **MENU** button or bind another key.

Bluetooth game controllers (Xbox, PlayStation, MFi) work through SDL as well.

## LAN multiplayer

Uses NotBlood's normal **Multiplayer → Host / Join** menu (ENet over UDP, port 23513 by default).

* All players must run **the same NotBlood version**. This build is pinned to upstream commit `4aa93cb` (the `latest` NotBlood release from 2026-09-16). Use that build on Windows/Linux, and rebuild the iPad version when you update the PCs.
* The first time you host or join, iOS asks for **Local Network** permission. Allow it. If you denied it, go to Settings → Privacy & Security → Local Network → NotBlood.
* To join, tap the IP address field and use **KEYB** to type the host's IP.
* Everyone needs the same game data (and Cryptic Passage files if you play those maps).

## Video

* **Polymost (default):** NotBlood's OpenGL renderer gives the true 3D look when you aim up or down, the same as on PC. It runs on the iPad's GPU through [gl4es](https://github.com/ptitSeb/gl4es), which translates desktop OpenGL 2.1 into the OpenGL ES 2.0 that iPadOS provides.
* **Classic:** the original 8-bit software renderer. It has DOS-style y-shearing when you look up or down, and it's heavier on the CPU. Switch between the two under **Options → Video → Renderer**.
* Rendering is capped at the display refresh rate, and the game sleeps between frames instead of busy-waiting, which keeps the iPad cooler.
* If the OpenGL renderer ever crashes during startup, the next launch falls back to the software renderer and creates `DISABLE_OPENGL.txt` in the NotBlood folder. Delete that file to try OpenGL again.

## Building yourself

On a Mac with Xcode, CMake and an SDL 2.32 source tree:

```sh
cmake -S platform/iOS -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DSDL2_SOURCE_DIR=/path/to/SDL2-2.32.10 -DGL4ES_SOURCE_DIR=/path/to/gl4es
cmake --build build-ios --config Release --target NotBlood -- -sdk iphoneos CODE_SIGNING_ALLOWED=NO
```

You can also open `build-ios/NotBlood.xcodeproj`, set a signing team and run it on a device.

## Port overview

* `source/build/src/iosbits.mm`: entry point, Documents/Files folder, bundle paths, screen size, game-data check
* `source/blood/src/touchcontrols.cpp`: touch overlay, weapon picker, settings screen and layout editor
* `source/build/src/sdlayer.cpp`: iOS video paths (OpenGL ES + gl4es; SDL_Renderer/Metal fallback), mouse/trackpad, app lifecycle (`EDUKE32_IOS`)
* `platform/iOS/gl4es/`: small shims to build gl4es with Apple's toolchain
* `platform/iOS/`: CMake project, Info.plist, app icon
