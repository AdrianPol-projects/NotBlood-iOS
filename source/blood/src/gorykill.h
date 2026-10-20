//-------------------------------------------------------------------------
/*
This file is part of NotBlood Chosen.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// Blood Extra Crispy (the mod by VGames for Refreshed Supply) as a mod for NotBlood: chosen in the MODS
// menu, its art, sounds and Gory Kills are used (walk up to a nearly dead enemy, press USE, and Caleb
// finishes it in first person). Everything comes from the player's copy of the mod, its BEC_1.2
// folder in mods/ or the game folder; nothing of the mod is part of NotBlood. See gorykill.cpp.

#pragma once

struct PLAYER;

// the MODS menu chose the mod: its folder, from the game folder (before the game's files load)
void goryKillSetModDir(const char *pzDir);

// a file of the chosen mod's folder (any upper/lower case), NULL without the mod or the file
const char *goryKillModFile(const char *pzName);

// startup, after the game's RFF files: opens the mod's EXTRAC.RFF and takes what the kills need
void goryKillInit(void);

// the mod is on and its files are there
bool goryKillAvailable(void);

// a kill is playing for this player: no moving, firing or damage
bool goryKillActive(PLAYER *pPlayer);

// a kill or a quick kick is playing: the weapon waits (a kick doesn't stop Caleb)
bool goryKillBusy(PLAYER *pPlayer);

// USE was pressed: starts a kill on a nearly dead enemy in front, or kicks one that isn't
bool goryKillTryStart(PLAYER *pPlayer);

// once a game frame: the kills' timelines
void goryKillFrame(void);

// draws the kill instead of the weapon (true when it did), and the blood on the screen after it
bool goryKillDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal);
void goryKillDrawScreenBlood(PLAYER *pPlayer);

// a new level or a loaded game: nothing is playing
void goryKillReset(void);
