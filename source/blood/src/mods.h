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

// MODS menu: add-on episodes and mods, loaded when the game starts.
//
//   addons/<folder>/   add-on episodes, e.g. addons/cryptic with CRYPTIC.INI and its maps and art
//   mods/<folder>/     mods as loose files (maps, .def/texture packs, voxels, an episode .ini, ...)
//   mods/<file>.zip    mods packed in a .zip, .grp or .pk3
//   <name>.ini         episode .ini files next to the game files
//
// The choice is stored in mods.cfg and applied at the next start, so choosing restarts the game.

#pragma once

enum
{
    kModNone = 0,  // the original game
    kModFolder,
    kModGroup,
    kModIni,
    kModExtraCrispy,  // Blood Extra Crispy's folder (gorykill.cpp): its art, sounds and Gory Kills
};

#define kMaxModEntries 12

struct MODENTRY
{
    int nKind;
    char szName[40];
    char szPath[BMAX_PATH];  // folder or file
    char szIni[BMAX_PATH];   // episode .ini to play, "" for the normal one
};

// name of the loaded mod or add-on ("" for none); network games need the same on every machine
const char *modCurrentName(void);

// startup: load the mod chosen in the menu (unless the command line chose game files)
void modApplySelection(bool bCommandLine);

// the game started fine with the chosen mod (a mod that stops the game from starting is
// switched off at the next start)
void modStartupFinished(void);

// menu: find the mods, then list them
int modScan(void);
MODENTRY const *modEntry(int nIndex);
bool modIsActive(MODENTRY const *pMod);
// remembers the choice for the next start
bool modSelect(int nIndex);
// starts the game again (asks to reopen the app where that isn't possible); doesn't return
void modRestart(const char *pzName);
