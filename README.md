# NotBlood Chosen

[NotBlood](https://github.com/clipmove/NotBlood) (a Blood source port) with **bots**, new match types, co-op saves, internet play without port forwarding, a live game dashboard, a MODS menu and a LAN game finder, for **Windows, Linux, macOS, Android and iPad**. Every player in a network game needs the same version; all platforms of one version play together.

**Downloads**
* Windows, Linux, macOS, Android: [NotBlood-Chosen releases](https://github.com/AdrianPol-projects/NotBlood-Chosen/releases)
* iPad (unsigned IPA for sideloading): [NotBlood-iOS releases](https://github.com/AdrianPol-projects/NotBlood-iOS/releases)
* Web browsers (iPad, phones, computers, no install): every NotBlood server serves it at `/play/` ([how it works](platform/Web/README.md))

Unzip, copy your Blood files (One Unit Whole Blood: CD, GOG or Steam) next to the game and start it. The README in each download explains the rest.

**What's new compared to NotBlood** (version 1.2, [release notes](package/release/NOTES.md))
* **Bots** that play like people (hide, ambush, flank, take the high ground, ride lifts), 4 difficulties, 6 personalities, fine tuning for all bots and for each one. Offline (PLAY WITH BOTS, CAREER, WATCH BOTS) and in LAN and online games, where the host's PC plays them. Details: [BOTS.md](package/release/BOTS.md).
* **Match types**: gun game, king of the hill, juggernaut, last man standing; teams: team bloodbath, capture the flag, king of the hill, last team standing; co-op survival. **SET TEAMS** for any mix of players and bots, **mutators** (gravity, vampire, one-hit kills, weapon sets) and **match stats**.
* **Co-op saves**, 8 slots, saved on every player's machine. Load *where you saved* (positions, keys, enemies) or *restart the level with your gear*. Players get their saves by name, new players start with the pitchfork, missing players leave the level. The game saves itself when a player leaves.
* **Online lobbies** through your own relay server ([how to set it up: SERVER.md](SERVER.md)): host or find lobbies, protected by a server key, no port forwarding.
* **Live dashboard** in the browser: lobbies, players' health, armor, weapons, keys, a live level map, an event list, big-screen mode, and you choose what it shows.
* **MODS menu**: add-on episodes (`addons/`, e.g. Cryptic Passage) and mods (`mods/` folders, `.zip`, `.grp`, `.pk3`). A mod that breaks the start is switched off again.
* **FIND LAN GAMES**, your address on the host screen, clear messages when versions or mods differ.
* Cutscenes from the CD's `movie` folder in any upper/lower case.
* **Touch devices**: on-screen controls, gyro aiming, resolutions from 100 % down to 25 %, touch buttons hide while a controller is connected.
* **macOS** app for Apple Silicon and Intel that needs no administrator; **Android** keeps everything in the `NotBlood` folder of the main storage.

Building: `release.yml` (desktop and Android), `platform/Android`, `platform/iOS`; the relay server is in `server/` ([SERVER.md](SERVER.md)).

---

<p align="center">
  <!-- logo -->
  <a href="https://github.com/clipmove/NotBlood" target="_blank"><img src="https://raw.githubusercontent.com/clipmove/NotBlood/master/.github/workflows/logo.png"></a>
  <br>
  <!-- primary badges -------------------------------------->
  <a href="https://github.com/clipmove/NotBlood/actions/workflows/build.yml" target"_blank"><img src="https://github.com/clipmove/NotBlood/actions/workflows/build.yml/badge.svg?style=flat-square" alt="Build Status"></a>
  <a href="https://github.com/clipmove/NotBlood/releases" target"_blank"><img src="https://raw.githubusercontent.com/clipmove/NotBlood/master/.github/workflows/download.svg?style=flat-square" alt="Github Download"></a>
</p><h1></h1>

### Overview
**NotBlood** is a fork of [NBlood](https://github.com/nukeykt/NBlood) with gameplay options, optional mutators and multiplayer features, while retaining NBlood mod support

### Downloads
Download for Windows/Linux can be found on [https://github.com/clipmove/NotBlood/releases](https://github.com/clipmove/NotBlood/releases)

### Features
* Switch to last active weapon if TNT/spray can is active when entering water
* Basic room over room support for positional audio ([before](https://clipmove.github.io/notbloodvids/roomoverroomaudio_before.mp4)/[after](https://clipmove.github.io/notbloodvids/roomoverroomaudio_after.mp4))
* Autosaving support for collecting keys and start of level
* BloodGDX style difficulty options for singleplayer
* Set item box selection to activated item
* Ability to record DOS compatible demos
* Drag and drop folder mod support ([demo](https://clipmove.github.io/notbloodvids/draganddropsupport.mp4))
* Customizable palette adjustment
* New singleplayer cheats
* Weapon selection bar ([demo](https://clipmove.github.io/notbloodvids/weaponselectbar.mp4))
* Mirror mode
* Auto crouch

### Multiplayer Features
* Cloak powerup hides player weapon icon
* Improved spawning randomization logic
* Colored player names for messages
* Adjustable spawn weapon option
* Adjustable spawn protection
* UT99 style multi kill alerts
* Uneven teams support

### Mutators (Optional)
* Difficulty based invulnerability timer for player damage ([demo](https://clipmove.github.io/notbloodvids/invultimer.mp4))
* Raymarching collision testing for player projectiles ([before](https://clipmove.github.io/notbloodvids/raymarchbefore.mp4)/[after](https://clipmove.github.io/notbloodvids/raymarchafter.mp4))
* Fix blood/bullet casings not being dragged with sectors ([before](https://clipmove.github.io/notbloodvids/decalsbefore.mp4)/[after](https://clipmove.github.io/notbloodvids/decalsafter.mp4))
* Fixed missiles colliding with water sector edges ([before](https://clipmove.github.io/notbloodvids/sectorbefore.mp4)/[after](https://clipmove.github.io/notbloodvids/sectorafter.mp4))
* Smaller hitboxes for player projectiles ([before](https://clipmove.github.io/notbloodvids/projectilesizebefore.mp4)/[after](https://clipmove.github.io/notbloodvids/projectilesizeafter.mp4))
* NotBlood balance mod for weapons (see README.txt for details)
* Randomize mode for enemies and pickups (multiplayer supported)
* Quad damage replacement for guns akimbo powerup
* Respawning enemies option for singleplayer
* Bullet projectiles for hitscan enemies
* Fixed bullet casings clipping into walls
* Lower gravity of bullet casings and gibs underwater
* Allow particle sprites to traverse through room over room sectors
* Increased blood splatter duration and improved floor collision accuracy
* Make blood splatter/flare gun glow effect slope on sloped surfaces
</details>

### Installing
1. Extract NotBlood to a new directory
2. Copy the following files from Blood (v1.21) to NotBlood folder:
   * BLOOD.INI
   * BLOOD.RFF
   * BLOOD000.DEM, ..., BLOOD003.DEM (optional)
   * CP01.MAP, ..., CP09.MAP (optional, Cryptic Passage)
   * CPART07.AR_ (optional, Cryptic Passage)
   * CPART15.AR_ (optional, Cryptic Passage)
   * CPBB01.MAP, ..., CPBB04.MAP (optional, Cryptic Passage)
   * CPSL.MAP (optional, Cryptic Passage)
   * CRYPTIC.INI (optional, Cryptic Passage)
   * CRYPTIC.SMK (optional, Cryptic Passage)
   * CRYPTIC.WAV (optional, Cryptic Passage)
   * GUI.RFF
   * SOUNDS.RFF
   * SURFACE.DAT
   * TILES000.ART, ..., TILES017.ART
   * VOXEL.DAT

3. Optionally, if you want to use CD audio tracks instead of MIDI, provide FLAC/OGG recordings in following format: bloodXX.flac/ogg, where XX is track number. Make sure to enable Redbook audio option in sound menu.
4. Optionally, if you want cutscenes and you have the original CD, copy the `movie` folder into NotBlood's folder (the folder itself too).
If you have the GOG version of the game, do the following:
   * make a copy of `game.ins` (or `game.inst`) named `game.cue`
   * mount the `.cue` as a virtual CD (for example with `WinCDEmu`)
   * copy the `movie` folder from the mounted CD into NotBlood's folder
5. Launch NotBlood (on Linux, to play Cryptic Passage, launch with the `-ini CRYPTIC.INI` parameter)
</details>

### Building from source
See: https://wiki.eduke32.com/wiki/Main_Page

### Acknowledgments
  See AUTHORS.md