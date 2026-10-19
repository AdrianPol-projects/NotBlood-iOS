# Blood for iPad and iPhone: NotBlood Chosen 1.2

**NotBlood Chosen** runs Blood (Monolith, 1997) on **iPad and iPhone**. It is based on the [NotBlood](https://github.com/clipmove/NotBlood) source port and adds touch controls, controller and keyboard/mouse support, bots, new match types, co-op saves, and LAN and online multiplayer with PC, Mac and Android players.

The game files are **not** included. You need your own copy of Blood: *One Unit Whole Blood* (GOG or Steam), *Fresh Supply* or the CD.

## Install (sideloading, no jailbreak)

1. Download `NotBlood.ipa` below. It's an **unsigned** IPA, which is what the sideloading services expect.
2. Sign and install it with **Signulous**, **Sideloadly**, **AltStore/SideStore** or your own Apple developer account. Free Apple IDs need to re-sign every 7 days; that's an Apple limit.
3. Start NotBlood once. It creates its folder and asks for the game files.
4. Open **Files → On My iPad (On My iPhone) → NotBlood** and copy everything from your Blood folder into it: `BLOOD.INI`, `BLOOD.RFF`, `GUI.RFF`, `SOUNDS.RFF`, `SURFACE.DAT`, `TABLES.DAT`, `VOXEL.DAT`, `TILES000.ART`-`TILES017.ART`.
5. Go back to NotBlood and tap **Check again**.

Optional, in the same folder:
- `movie/` with the cutscenes from the CD
- `addons/` for add-on episodes like Cryptic Passage
- `mods/` for mods
- CD music as `blood02.ogg`, `blood03.ogg`, and so on

**Requirements:** iPadOS/iOS 15 or newer. Any iPad from the last several years runs it well in the OpenGL renderer.

**No sideloading?** If you or a friend runs a NotBlood server ([SERVER.md](https://github.com/AdrianPol-projects/NotBlood-Chosen/blob/main/SERVER.md)), open its address followed by `/play/` in Safari and choose Share → **Add to Home Screen**. The same game runs as a web app: no signing, no 7-day limit, and it works offline. LAN games don't work there; online games do.

## What you get on iOS

- **Touch controls:** a floating move stick, drag to look, and fire, jump, duck, use and weapon buttons, plus a weapon picker. There's a layout editor (move, resize and hide every button) and gyro aiming. Render scale goes from 100% down to 25% for older devices.
- **Keyboard, trackpad and mouse:** Magic Keyboard and Bluetooth mice give PC-style mouse look. The touch buttons hide while you use them.
- **Controllers:** Xbox, PlayStation and MFi controllers work, and the touch buttons hide while one is connected.
- **Two renderers:** OpenGL (Polymost, true 3D look) or the classic 8-bit software renderer.
- **Bots:** computer players that hide, ambush, flank and take the high ground. You can set their difficulty and personality, and fine-tune all bots or each one.
  - Offline: MULTIPLAYER → PLAY WITH BOTS, plus a 10-match career and a watch-only mode.
  - In LAN and online games: the host adds them under BOTS.
- **Match types:**
  - Bloodbath: gun game, king of the hill, juggernaut, last man standing.
  - Teams: team bloodbath, capture the flag, king of the hill, last team standing (SET TEAMS for any mix of players and bots).
  - Co-op: survival waves.
  - Mutators: low or moon gravity, vampire, one-hit kills, weapon sets.
- **Multiplayer with every platform:** the iPad plays together with the Windows, Linux, macOS and Android builds of the same version.
  - LAN: FIND LAN GAMES.
  - Online: lobbies through a relay server, with no port forwarding.
  - Co-op saves.
- **MODS menu** for add-on episodes and mods.
- Settings, saves and the touch layout stay in the NotBlood folder, so you can back them up from the Files app.

## Tips

- **Local Network permission:** iOS asks for it the first time you host or join. Allow it, or later go to Settings → Privacy & Security → Local Network → NotBlood.
- **Same version for everyone:** every player in a network game needs NotBlood Chosen **1.2**.
- **If OpenGL crashes at startup**, the next launch switches to the classic renderer and creates `DISABLE_OPENGL.txt`. Delete that file to try OpenGL again.
- **The Magic Keyboard has no Esc key:** use the on-screen MENU button.

## Known issues

- **Blood's demo files close the game:** with Blood's demo files (`*.DEM`) in the NotBlood folder, the game can close when a match with bots starts. Remove the `.DEM` files.
- **Co-op saves:** they are off in games with bots.

## Links

- **The full guide:** [platform/iOS/README.md](https://github.com/AdrianPol-projects/NotBlood-iOS/blob/ios/platform/iOS/README.md) covers the touch controls, building it yourself and the port overview.
- **Other platforms:** the Windows, Linux, macOS and Android downloads of the same version are in [NotBlood-Chosen](https://github.com/AdrianPol-projects/NotBlood-Chosen/releases/latest).

NotBlood Chosen is GPL 2, like NotBlood. Blood is © Monolith Productions; this project contains no game data.
