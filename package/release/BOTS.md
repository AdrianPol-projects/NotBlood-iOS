**NotBlood Chosen 1.2 - bots.** Setup and everything else NotBlood Chosen adds: see the release notes and the README in the download.


### Playing against bots

MULTIPLAYER > **PLAY WITH BOTS**

- **BOTS**: how many (1-7)
- **DIFFICULTY**: easy, normal, hard, nightmare - how well they aim, how fast they react, how far they hear and how often they use tactics
- **PERSONALITY**:
  - *rusher* charges in
  - *camper* waits in good spots
  - *trickster* ambushes you at doorways and sneaks round to come from the other side
  - *coward* backs off and goes for health
  - *jumper* hops around in fights
  - *balanced* a bit of everything
  - *mixed* gives every bot a different one
- **SET EACH BOT**: pick a bot with BOT:, then give it its own name (empty: a random one), difficulty, personality, team, how much damage it takes and its own fine tuning (aim, reaction time, aggression, jumping, ambushes, flanking, camping, high ground in percent of FINE TUNING for all bots; 100% is the same). RESET THIS BOT puts it back.
- **FINE TUNING (ALL BOTS)**: aim, reaction time, aggression, jumping, ambushes, flanking, camping and high ground in percent of the difficulty's/personality's own value; taunts; cultist look; how much damage bots take (shown next to their names when not normal); showing what every bot is doing
- **SET TEAMS** (teams games): see below
- **MATCH SETTINGS**: the multiplayer setup - bloodbath or teams, map, frag and time limits, weapons, items, mutators
- **START MATCH**. ESC pauses the match; NEW GAME there sets up the next bot match (bots and match settings); END GAME goes back to normal single player.

The difficulties and personalities include the tuning that played best in testing (slower reactions, more aggression, jumping and ambushes, less camping); the FINE TUNING sliders start again at 100% once.

Before each decision a bot sizes up the situation like a player: its health, armor and weapon against the enemies it knows are close (their weapons, powerups, how hurt they must be from the hits it saw land, whether they have seen it), the score and how close the frag limit is, and whether it just got a kill or just got hurt. Then it weighs fighting, getting away, getting up high, going round, ambushing, chasing, collecting and camping against each other. When two others fight each other and neither has noticed it, it may wait and then finish off whoever is left.

The bots only know what a player could: they see what's in front of them and not behind walls, hear shots, jumps and running, and remember where they last saw you. They take a moment to react, their aim wobbles and settles while they track you, and they pause their shooting now and then. On easy and normal they don't use autoaim. They open doors and sliding walls (from the side the button is on), never press a level exit, and when they're lost they look around for a way out. They ride lifts: they call them, ride them up and down and walk over to a lift's switch when that's what works it, so they climb out of pits too.

On every map they work out the good high places: how much you can see from there, how far you look down on it, and weapons close by. Depending on their personality and the HIGH GROUND setting they go up there, hold it, don't step off ledges, shoot down at you instead of jumping after you, and go for splash weapons (napalm, TNT, flares) when you have the high ground.

Console: `bot_match [frag limit] [time limit]` starts a match with the menu's settings; all settings are `bot_*` variables (type `bot_` and press TAB).

### How the bots work

**A bot is a player.** Every bot takes a normal player slot. The game treats it like a human: same health, weapons, pickups, frags and respawns. The only difference is where its controls come from. Each game tick the bot code fills in the same input a keyboard and mouse would give: move forward or back, strafe, turn, look up or down, jump, crouch, fire, alt-fire, use, switch weapon. So bots can't do anything a player can't, and they don't cheat on speed, damage or ammo.

**What a bot knows.**
- **Sight:** it sees only what's in its field of view and not behind walls (a line-of-sight check like the game's own).
- **Hearing:** it hears shots, explosions, jumps and running nearby.
- **Memory:** it remembers where it last saw or heard you and how hurt you looked from the hits it saw land.
- **Reaction:** it needs a moment to react (shorter on harder difficulties).
- **Aim:** its aim wobbles and settles while it follows you.

**How a bot decides.** Several times a second a bot sizes up its situation:
- its own health, armor and weapons
- the enemies it knows about and what they carry
- the score and the time left
- whether it just got a kill or got hurt

It then gives each option a score and picks the best one: fight, flee to health, get to high ground, flank round, ambush at a doorway, chase, collect weapons and items, or camp. The **personality** shifts these scores (a rusher likes fighting, a coward likes fleeing), and the **difficulty** sets how good its aim, reaction, hearing and tactics are. **FINE TUNING** multiplies each of these, for all bots and for each bot. In the match types the objective is part of the choice too: the flag, the hill, the juggernaut, the next wave.

**How a bot moves.**
- **Pathfinding:** when a level starts, the bots read its layout from the map itself: rooms (sectors), the openings between them, doors, lifts, switches, ledges and pits. They find a way to their goal with an A* search over the rooms.
- **Obstacles:** they open doors from the side the button is on, ride lifts (and walk over to a lift's switch when that's what works it), and jump gaps and steps. They watch for ledges and pits.
- **High ground:** they score every spot on the map for how much it overlooks and how close weapons are.
- **Getting unstuck:** when a bot is stuck it tries another way, and when it's lost it looks around for an exit.
- **Level exits:** they never press one.

**In network games** the bots run only on the **host's** computer.
- The host makes their input together with its own and sends it to everybody with all the other players' moves. Every machine then runs exactly the same game, so all players see the same bots.
- Before the start, the host sends the bots' names, looks and teams.
- Their taunts go to everybody as chat.
- The other players need nothing extra, only the same version.

### Bots in LAN and online games (new in 1.2)

Host the game as usual (HOST A GAME or an online lobby). In the MULTIPLAYER setup, right under GAME, **BOTS** opens the same bots menu: how many (0, the default, is none; people and bots together are 8 at most), difficulty, personality, SET EACH BOT, FINE TUNING and SET TEAMS. START GAME starts it with them.

- The host's PC plays the bots and sends what they do along with everybody's moves, so every player sees the same bots. Their taunts reach everybody too.
- In co-op they stay with the closest player on their side; after the host's BOT ORDERS (ESC) "follow me" means the host.
- Everybody needs 1.2. Co-op saves aren't made in games with bots (yet).

### Teams

GAME SETTINGS > GAME: TEAMS, then MODE (now right under GAME): **team bloodbath** (frags only, no flags), **capture the flag**, **king of the hill**, **last team standing** (LIVES EACH; the last team with somebody left wins).

**SET TEAMS** (in GAME SETTINGS and in the bots menu) lists everybody: the players who joined and the bots. Each one is AUTO, BLUE or RED; AUTO shows where they'll end up (players as they chose in their own options or by the game's AUTO TEAMS, bots fill the smaller team), and the totals of both teams are shown underneath. Teams can be uneven (for example 2 players against 1 player and 2 bots). To make it fair, give the weaker player's team the stronger bot: e.g. one player with a HARD bot against a better player with an EASY bot (difficulty and fine tuning per bot in SET EACH BOT).

### Match types and mutators (with bots, real players, or both)

MULTIPLAYER match settings (or MATCH SETTINGS in the bots menu) > GAME > **MODE**:

- **Bloodbath**: normal, *gun game* (every kill moves you on to the next of 10 weapons; a pitchfork kill with the last one wins), *king of the hill* (a point a second for holding the flag-marked area alone; it moves every 90 seconds), *juggernaut* (whoever kills the juggernaut becomes it: 200 health, armor, double guns; only kills by or of the juggernaut score), *last man standing* (LIVES EACH; the last one with lives left wins)
- **Teams**: team bloodbath, capture the flag (the bots carry, return and defend flags), king of the hill, last team standing
- **Co-op**: normal, *survival* (waves of monsters that get bigger and nastier, ammo between waves, shared lives (0: endless), your best wave per map is kept)

SET MUTATORS > **MORE MUTATORS**: low or moon gravity, vampire (hits heal you), one-hit kills, and weapon sets (a random weapon at every spawn, or one weapon only).

During a match ESC shows **MATCH STATS** (frags, deaths, aim, best streak, favourite weapon, your nemesis and favourite victim; also shown when a match ends). Everybody in a network game needs the same version (1.2).

### More with bots

- **BOT ORDERS** (ESC during a bot match, or for the host of a network game with bots): follow me, hold this spot, go ahead - for the bots on your side in co-op and teams. In co-op they follow you by default and fight the monsters with you through the episodes.
- **CAREER** (bots menu): a ladder of 10 matches against named bots, getting harder and mixing the match types; win a stage to unlock the next. Your record, kills, deaths, best streak and nemesis are kept.
- **WATCH BOTS** (bots menu): a match of bots only; the camera follows the action (the see-co-op-view key switches bots).
- FINE TUNING: **GRUDGES** (a bot you kill a lot hunts you and taunts you when it gets you back) and **ADAPTIVE DIFFICULTY** (bots slowly get better or worse to keep it close).

### Feedback

Set FINE TUNING > SHOW WHAT BOTS DO, or type `bot_debug 2` in the console: every bot's plan is shown on screen, and every 2 seconds it is written to `notblood.log` in the game folder, with places where bots got stuck (in a network game on the host's PC). Send that file along with what felt wrong. The log also lists who plays in which team, and "OUT OF SYNC" if network players ever see different games.

**Known issue:** if the game folder has Blood's demo files (`*.DEM`, the gameplay that plays behind the main menu), the game closes when a bot match starts (seen on Windows). Remove the `.DEM` files from the game folder (or start the game with `-nodemo`) and it works. Not fixed yet.

Not yet: co-op saves in games with bots; bots in games started with `-broadcast`.
