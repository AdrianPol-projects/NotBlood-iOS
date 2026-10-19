# NotBlood Chosen 1.2

**NotBlood Chosen** is [NotBlood](https://github.com/clipmove/NotBlood) (the Blood source port) with computer players (bots), new match types, co-op saves, internet play without port forwarding, a live game dashboard, a MODS menu and a LAN game finder. It runs on **Windows, Linux, macOS, Android and iPad/iPhone**, and all of them play together.

You need your own Blood files (One Unit Whole Blood: CD, GOG or Steam). The game files are not included.

## Downloads

| File | For |
| --- | --- |
| `NotBlood-Chosen-1.2-win64.zip` | Windows 10/11 (64-bit) |
| `NotBlood-Chosen-1.2-linux-x64.zip` | Linux x86_64 (needs SDL2) |
| `NotBlood-Chosen-1.2-macos.zip` | macOS 11 or newer, Apple Silicon and Intel |
| `NotBlood-Chosen-1.2-android.apk` | Android 5 or newer (phones, tablets, Chromebooks) |
| `NotBlood-Chosen-1.2-ios.ipa` | iPad/iPhone with iOS 15 or newer (unsigned: sideload it with Signulous, Sideloadly or AltStore) |
| `NotBlood-Chosen-1.2-web.zip` | the web version; every NotBlood server installs it by itself and serves it at `/play/` ([SERVER.md](https://github.com/AdrianPol-projects/NotBlood-Chosen/blob/main/SERVER.md)) |

**No install at all:** if you or a friend runs a NotBlood server, open its address followed by `/play/` in Safari, Chrome, Edge or Firefox. Pick your Blood files once, add it to the home screen and it works offline, with everything below except LAN games ([more](https://github.com/AdrianPol-projects/NotBlood-Chosen/blob/main/platform/Web/README.md)).

The iPad/iPhone IPA is the same build that's in [NotBlood-iOS](https://github.com/AdrianPol-projects/NotBlood-iOS/releases/latest), which has iOS install notes.

Everybody in a network game needs **1.2**. Older versions can't join a 1.2 game, and 1.2 can't join theirs.

## Setup

**Windows / Linux**
1. Unzip into a folder of its own, for example `NotBlood`.
2. Copy your Blood files next to the game: `BLOOD.INI`, `BLOOD.RFF`, `GUI.RFF`, `SOUNDS.RFF`, `SURFACE.DAT`, `TABLES.DAT`, `VOXEL.DAT` and `TILES000.ART` to `TILES017.ART`.
3. Start `notblood.exe` (Windows) or `./notblood` (Linux; if SDL2 is missing: `sudo apt install libsdl2-2.0-0`).

**macOS**
1. Unzip and put the `NotBlood` folder anywhere in your home folder, for example in Documents. You don't need an administrator.
2. Copy your Blood files into the folder, next to `NotBlood.app`.
3. The app isn't signed by Apple, so macOS blocks it the first time. Open Terminal and type `xattr -dr com.apple.quarantine ` with a space at the end. Drag the NotBlood folder onto the Terminal window and press Return.
4. Double-click `NotBlood.app`.

**Android**
1. Install the APK and start it once. Allow it "access to all files".
2. Copy your Blood files into the `NotBlood` folder of the main storage (next to `Download`) and start the game again.
3. 1.2 installs over NotBlood Chosen 1.0 and over the bots test builds, and keeps your files and settings.

**iPad / iPhone**
1. Sign and install the IPA with Signulous, Sideloadly or AltStore, and start it once.
2. Copy your Blood files to Files > On My iPad > NotBlood.

**Optional extras** (all platforms, in the game folder)
- `movie/`: the cutscenes from the Blood CD's MOVIE folder.
- `addons/`: add-on episodes, one folder each (for example Cryptic Passage in `addons/cryptic`).
- `mods/`: mods as folders, `.zip`, `.grp` or `.pk3` files. Pick them in the main menu under MODS.
- CD music: save the CD audio tracks as `blood02.ogg`, `blood03.ogg`, and so on, then turn on OPTIONS > SOUND > REDBOOK AUDIO.

## What NotBlood Chosen adds to NotBlood

**Bots (new in 1.2)**
- Computer players that play like people. They hide, jump, chase, flee, ambush you at doorways, flank, take the high ground, ride lifts and open doors. They only know what a player could know: what they see and hear, and where they last saw you.
- 4 difficulties (easy, normal, hard, nightmare) and 6 personalities (balanced, rusher, camper, trickster, coward, jumper), or a mix of them.
- FINE TUNING for all bots: aim, reaction time, aggression, jumping, ambushes, flanking, camping, high ground, taunts, the cultist look, and how much damage they take. GRUDGES and ADAPTIVE DIFFICULTY are optional.
- **SET EACH BOT** (new in 1.2): give each bot its own name, difficulty, personality, team, damage and fine tuning.
- **Offline:** MULTIPLAYER > PLAY WITH BOTS, plus a 10-match CAREER and WATCH BOTS (only bots play while you watch).
- **In LAN and online games** (new in 1.2): the host adds bots in the MULTIPLAYER setup under BOTS. The host's computer plays them, so everybody sees the same bots.
- **BOT ORDERS** (ESC): follow me, hold this spot, go ahead. These are for the bots on your side in co-op and teams.

**Match types and mutators**
- Bloodbath: normal, gun game, king of the hill, juggernaut, last man standing.
- Teams (new in 1.2: all four modes): team bloodbath, capture the flag, king of the hill, last team standing.
- Co-op: normal, survival (waves of monsters).
- **SET TEAMS** (new in 1.2): put every player and bot on AUTO, BLUE or RED. Uneven teams are allowed.
- Mutators: low or moon gravity, vampire, one-hit kills, random or single weapon.
- MATCH STATS: frags, deaths, aim, best streak, favourite weapon, nemesis.

**Co-op saves**
- 8 slots, saved on every player's machine.
- Load *where you saved* (positions, keys, enemies) or *restart the level with your gear*.
- Players get their saves by name. New players start with the pitchfork. When somebody leaves, the game saves itself.

**Online and LAN**
- Online lobbies through your own relay server ([how to run it: SERVER.md](https://github.com/AdrianPol-projects/NotBlood-Chosen/blob/main/SERVER.md)). They are protected by a key and need no port forwarding. There is no default server: enter your own server's address and key.
- A live dashboard in the browser: lobbies, players' health, armor, weapons and keys, a live level map and an event list.
- FIND LAN GAMES. The host screen shows your address, and you get clear messages when versions or mods differ.

**Everything else**
- MODS menu for add-on episodes and mods. A mod that breaks the start is switched off again.
- Cutscenes from the CD's `movie` folder in any upper or lower case.
- Touch controls, gyro aiming and render scale on Android and iPad. The touch buttons hide while a controller is connected.
- A macOS app (Apple Silicon and Intel) that needs no administrator. Android keeps everything in the `NotBlood` folder.
- Settings are saved whenever a menu closes. On Android there is a crash report in `notblood.log`.

## New in 1.2 compared to 1.0
- Bots, the match types, mutators and match stats (all of these were only in the bots test builds before).
- Bots in LAN and online games, SET EACH BOT and SET TEAMS.
- Team bloodbath without flags, and last team standing.
- MODE now sits right under GAME in GAME SETTINGS.
- No default online server.
- Android: the black sky, explosions turning the world black, settings not being kept and slow drawing are fixed.
- macOS: faster drawing on Apple GPUs.

## Quick start

**You against bots:** MULTIPLAYER > PLAY WITH BOTS. Set BOTS, DIFFICULTY and PERSONALITY, then START MATCH. Change the map, mode and limits in MATCH SETTINGS.

**2 players on a LAN plus 2 bots**
1. Player 1: MULTIPLAYER > HOST A GAME.
2. In the setup, set BOTS (under GAME) to 2.
3. Player 2: MULTIPLAYER > FIND LAN GAMES and join.
4. Player 1: once player 2 has joined, START GAME.

**Teams:** GAME SETTINGS > GAME: TEAMS. Pick the MODE, then use SET TEAMS to choose who plays on which side.

**Online:** set up your own server once ([SERVER.md](https://github.com/AdrianPol-projects/NotBlood-Chosen/blob/main/SERVER.md): Docker on a Raspberry Pi, a home PC or a VPS). Then MULTIPLAYER > ONLINE: enter the server address and key, then HOST A LOBBY or FIND LOBBIES. Bots work the same way as on a LAN.

## Game modes

| Mode | What it is |
| --- | --- |
| Bloodbath: normal | Everyone for themselves, most frags wins |
| Bloodbath: gun game | Every kill gives you the next of 10 weapons. A pitchfork kill with the last one wins |
| Bloodbath: king of the hill | Score while you're alone in the flag-marked area. It moves every 90 seconds |
| Bloodbath: juggernaut | Kill the juggernaut to become it (200 health, armor, double guns) |
| Bloodbath: last man standing | Limited lives, and the last one alive wins |
| Teams: team bloodbath | Blue against red, frags only |
| Teams: capture the flag | Bring the enemy flag to your base |
| Teams: king of the hill | Your team scores while only your side holds the hill |
| Teams: last team standing | Limited lives, and the last team with someone left wins |
| Co-op: normal | The episodes together, against the monsters |
| Co-op: survival | Growing waves of monsters, shared lives, your best wave is kept |

Every mode works with only people, only bots, or both. The README in the download explains every menu in more detail, and `README - Bots.txt` covers the bots.

## Known issues
- **Blood's demo files close the game:** if the game folder has Blood's demo files (`*.DEM`, the gameplay behind the main menu), the game closes when a match with bots starts (seen on Windows). Remove the `.DEM` files from the game folder or start the game with `-nodemo`.
- **Co-op saves:** they are off in games with bots.
- **Broadcast games:** bots can't be added to games started with `-broadcast`.
- **Platform testing:** Linux and Windows are tested most. Cross-platform games and the iPad build are less tested, so please report problems with `notblood.log` from the game folder.

NotBlood Chosen is GPL 2, like NotBlood. Source: [AdrianPol-projects/NotBlood-Chosen](https://github.com/AdrianPol-projects/NotBlood-Chosen).
