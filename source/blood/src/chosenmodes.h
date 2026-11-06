//-------------------------------------------------------------------------
/*
This file is part of NotBlood.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------
#pragma once

// NotBlood Chosen match types and mutators (gun game, king of the hill, juggernaut, last man
// standing, survival; gravity, vampire, one-hit kills, weapon sets) and match statistics.
// Everything that changes the game runs in the game's own frame logic and uses only the
// game's random numbers, so it plays the same on every machine of a network game.

#include "build.h"

struct PLAYER;

enum {
    kChosenModeNormal = 0,
    kChosenModeGunGame,
    kChosenModeKingOfTheHill,
    kChosenModeJuggernaut,
    kChosenModeLastManStanding,
    kChosenModeSurvival,
    kChosenModeCaptureFlag,  // teams with the flags (plain teams is team bloodbath)
    kChosenModeHideSeek,     // hiders look like the map's decorations, seekers hunt them
    kChosenModeMax
};

enum {
    kChosenWeaponsNormal = 0,
    kChosenWeaponsRandom,   // a random weapon at every spawn
    kChosenWeaponsOnlyBase, // + weapon number (flare 2 ... voodoo 10): only that weapon
};

// how hard survival's waves are: 0 is how they always were, so a host that doesn't send it gets that
enum {
    kChosenWavesNormal = 0,
    kChosenWavesEasy,
    kChosenWavesVeryEasy,
    kChosenWavesHard,
    kChosenWavesMax
};

struct CHOSENRULES
{
    uint8_t nMode;
    uint8_t nLives;    // last man standing, survival (0 = endless for survival)
    uint8_t nGravity;  // 0 normal, 1 low, 2 moon
    uint8_t nWeapons;
    uint8_t bVampire;
    uint8_t bOneHit;
    uint8_t nTeam[8];  // teams picked in the menu (0: as before, 1 blue, 2 red), by player number
    uint8_t nWaves;    // survival: kChosenWaves*
    uint8_t nSize;     // player size: 0 normal, 1 tiny, 2 big
    uint8_t nBarrels;  // TNT barrels everywhere: 0 off, 1 some, 2 lots
    uint8_t bAkimbo;   // guns akimbo for everybody, all the time (and a stronger pitchfork)
    uint8_t nRounds;   // rounds in a match (0 or 1: one)
    uint8_t nRound;    // which round this is (1 ...)
    uint8_t nRoundWins[8]; // rounds won so far, by player (teams: by team)
    uint8_t bVote;     // vote for the next map when the match is over
    uint8_t bHostOnly; // only the host starts new games
};

extern CHOSENRULES gChosenRules;

// match type choices for a game type, for the menu (count returned)
int modesForGameType(int nGameType, const char ***pppzNames, const int **ppnModes);
const char *modesName(int nMode);

void modesSetFromPacket(void);   // StartLevel: a new network/bot match
void modesClear(void);           // single player
void modesStartLevel(void);      // after everybody spawned
void modesPlayerSpawned(PLAYER *pPlayer);
void modesFrag(PLAYER *pKiller, PLAYER *pVictim);
void modesFrame(void);
int modesDamage(int nSource, spritetype *pTarget, int nDamage);
void modesDamageDone(PLAYER *pSource, spritetype *pTarget, int nDamage);
void modesShot(PLAYER *pPlayer);
bool modesCanRespawn(PLAYER *pPlayer);
bool modesWeaponPickup(PLAYER *pPlayer, int nWeapon);
int modesGravity(spritetype *pSprite, int nGravity);
bool modesOutOfLives(int nPlayer);
void modesDrawHud(void);
void modesDrawStats(int nTop);
bool modesShowStats(void);       // the round is over: show the stats
bool modesHill(int *px, int *py, int *pRadius); // king of the hill: where it is
int modesJuggernaut(void);       // player index or -1
int modesGunLevel(int nPlayer);
int modesWinner(void);           // who a match type declared the winner (-1 nobody/a team/not over)
int modesDeaths(int nPlayer);
int modesBestStreak(int nPlayer);
bool modesIsEnemy(PLAYER *pMe, PLAYER *pOther); // bots: who to go after (hide and seek)
bool modesDrawPlayer(tspritetype *pTSprite, PLAYER *pPlayer); // look of a player (true: drawn as something else, no extras)
bool modesHideName(PLAYER *pPlayer);   // don't show its name when aimed at
bool modesAkimboPitchfork(PLAYER *pPlayer);

// rounds and the map vote
#define kChosenVoteChoices 3
bool modesVoting(void);                // the map vote is on
const char *modesVoteText(int nChoice);
void modesVote(int nChoice);           // this player's vote (sent to everybody)
void modesReceiveVote(int nPlayer, int nChoice);
int modesMyVote(void);

// only the host starts new games (a network game's host sets it, everybody gets it)
extern int gNetHostOnly;               // what the host said
extern int gHostOnlyPref;              // the host's setting (saved)
bool modesMayStartGame(void);          // this player may start a new game
void modesSendHostOnly(void);          // host: tell everybody
void modesReceiveHostOnly(int nValue);
