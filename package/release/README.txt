NotBlood Chosen
===============

Version 1.2. NotBlood (a Blood source port) with bots, new match types, co-op saves,
online lobbies, a live game dashboard, a MODS menu and a LAN game finder. Every player in a
network game needs the same version - PC, Mac, iPad and Android builds of one version play
together. The bots are explained in detail in "README - Bots.txt".


SETUP

1. Copy your Blood files into this NotBlood folder, next to the game:
   BLOOD.INI, BLOOD.RFF, GUI.RFF, SOUNDS.RFF, SURFACE.DAT, TABLES.DAT, VOXEL.DAT and
   TILES000.ART to TILES017.ART (from One Unit Whole Blood: CD, GOG or Steam).
2. Start the game:
   Windows:  notblood.exe
   Linux:    ./notblood   (needs SDL2: sudo apt install libsdl2-2.0-0)
   macOS:    see "macOS" below


FOLDERS

  movie/    the cutscenes: copy the files from the MOVIE folder of the Blood CD here
            (CS1.SMK ... CS6.SMK with their .WAV files, GTI.SMK, LOGO.SMK)
  addons/   add-on episodes, one folder each. Cryptic Passage: addons/cryptic with
            CRYPTIC.INI, its CP*.MAP maps and CPART07.AR_ / CPART15.AR_
  mods/     mods: a folder each (maps, .def texture packs, voxels, ...) or .zip/.grp/.pk3 files

  Pick add-ons and mods in the main menu: MODS (the game restarts with it).
  CD music: save the CD audio tracks as blood02.ogg, blood03.ogg, ... in this folder and turn on
  OPTIONS > SOUND > REDBOOK AUDIO.


BOTS

  On your own:  MULTIPLAYER > PLAY WITH BOTS
    BOTS          how many (1-7)
    DIFFICULTY    easy, normal, hard, nightmare
    PERSONALITY   balanced, rusher, camper, trickster, coward, jumper, or mixed
    SET EACH BOT  one bot's own name, difficulty, personality, team, damage and tuning
    FINE TUNING   aim, reaction, aggression, jumping, ambushes, ... for all bots
    SET TEAMS     who plays for BLUE and who for RED (teams games)
    MATCH SETTINGS  map, mode, frag and time limits, weapons, mutators
    START MATCH   ESC pauses; NEW GAME there sets up the next match
  Also in that menu: CAREER (10 matches against harder and harder named bots) and
  WATCH BOTS (only bots play, you watch).

  In LAN and online games: the host sets BOTS in the MULTIPLAYER setup (right under GAME;
  0 = none, the default). People and bots together are 8 at most. The host's computer plays
  the bots, everybody sees the same ones. During the game the host has ESC > BOT ORDERS.

  Example - 2 players on a LAN and 2 bots:
    1. Player 1: MULTIPLAYER > HOST A GAME, set BOTS to 2.
    2. Player 2: MULTIPLAYER > FIND LAN GAMES and join.
    3. Player 1: START GAME.


GAME MODES

  GAME SETTINGS > GAME (bloodbath, teams or co-op), then MODE right underneath:
    Bloodbath   normal, gun game, king of the hill, juggernaut, last man standing
    Teams       team bloodbath, capture the flag, king of the hill, last team standing
    Co-op       normal, survival (waves of monsters)
  LIVES EACH is for last man/team standing and survival. SET MUTATORS > MORE MUTATORS:
  low or moon gravity, vampire, one-hit kills, random weapon or one weapon only.
  Teams: SET TEAMS puts every player and bot on AUTO, BLUE or RED; uneven teams are fine -
  give the weaker side the stronger bot (SET EACH BOT).
  Every mode works with people, bots or both. ESC > MATCH STATS shows the scores.


CO-OP SAVES

  During a co-op game: Esc > SAVE CO-OP > pick a slot. Every player's machine saves.
  To continue, host a game, let everybody join, then NEW GAME > LOAD CO-OP (during a game the
  host also has Esc > LOAD CO-OP):
    CONTINUE WHERE YOU SAVED      everything as it was: positions, keys, enemies
    RESTART LEVEL WITH YOUR GEAR  the level starts over, everybody keeps weapons and items
  Players get their saves by their name; players who weren't in the save start with the
  pitchfork. When somebody leaves, the game is saved (slot "PLAYER LEFT").
  The saves are in the coopsaves folder: carryover/ and midlevel/.
  Games with bots make no co-op saves.


ONLINE

  MULTIPLAYER > ONLINE: the server address and its key (ask whoever runs the server - there
  is no default server), then HOST A LOBBY or FIND LOBBIES. Nobody needs to forward ports.
  Running your own server (Docker, e.g. on a Raspberry Pi): see SERVER.md in the source code.
  Live stats of the games: open the server's address in a web browser.


LAN

  MULTIPLAYER > HOST A GAME shows your address. The others use FIND LAN GAMES
  (or JOIN A GAME with that address).


macOS

  1. Put this NotBlood folder anywhere in your home folder - no administrator needed. For
     example in "Applications" inside your home folder, or in Documents.
  2. Copy your Blood files into the NotBlood folder, next to NotBlood.app.
  3. The app isn't signed by Apple, so macOS blocks it at first. Open Terminal, type
        xattr -dr com.apple.quarantine 
     (with a space at the end), drag the NotBlood folder onto the Terminal window and press
     Return. This needs no administrator either.
  4. Double-click NotBlood.app. Settings and saves are kept in the same folder.


KNOWN ISSUES

  If the game folder has Blood's demo files (*.DEM), the game closes when a match with bots
  starts. Remove the .DEM files or start the game with -nodemo.
  Problems: send notblood.log from the game folder (Android: the NotBlood folder).


Source code and new versions: https://github.com/AdrianPol-projects/NotBlood-Chosen
NotBlood: https://github.com/clipmove/NotBlood - NotBlood Chosen is GPL 2 like NotBlood.
