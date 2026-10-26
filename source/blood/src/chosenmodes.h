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
