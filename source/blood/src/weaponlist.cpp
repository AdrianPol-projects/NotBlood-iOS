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

// The weapon switch list (see weaponlist.h). Pictures: each weapon's pickup tile (gWeaponItemData);
// the pitchfork has none and shows its name.

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "blood.h"
#include "globals.h"
#include "player.h"
#include "view.h"
#include "weaponlist.h"

int32_t gWeaponSwitchList = 0;

namespace
{

struct WEAPONROW
{
    char const *pzName;
    int nWeapon[3];  // the TNT row stands for its three kinds
};

WEAPONROW const gRows[] = {
    { "PITCHFORK", { kWeaponPitchfork, -1, -1 } },
    { "FLARE GUN", { kWeaponFlare, -1, -1 } },
    { "SHOTGUN", { kWeaponShotgun, -1, -1 } },
    { "TOMMY GUN", { kWeaponTommy, -1, -1 } },
    { "NAPALM", { kWeaponNapalm, -1, -1 } },
    { "TNT", { kWeaponTNT, kWeaponProxyTNT, kWeaponRemoteTNT } },
    { "SPRAY CAN", { kWeaponSprayCan, -1, -1 } },
    { "TESLA", { kWeaponTesla, -1, -1 } },
    { "LIFE LEECH", { kWeaponLifeLeech, -1, -1 } },
    { "VOODOO DOLL", { kWeaponVoodoo, -1, -1 } },
};

enum { kShowTime = 2 * 120, kRowHeight = 15, kIconW = 36, kIconH = 13, kRight = 316 };

int nLastWeapon = -1;
int nShowUntil;

int pictureFor(int nWeapon)
{
    for (int i = 0; i < kItemWeaponMax - kItemWeaponBase; i++)
        if (gWeaponItemData[i].type == nWeapon && gWeaponItemData[i].picnum > 0)
            return gWeaponItemData[i].picnum;
    if (nWeapon == kWeaponTNT)
        return 3440;  // the TNT bundle
    return -1;
}

bool rowHas(PLAYER *pPlayer, WEAPONROW const &r)
{
    for (int w : r.nWeapon)
        if (w > 0 && w < kWeaponMax && pPlayer->hasWeapon[w])
            return true;
    return false;
}

bool rowIs(WEAPONROW const &r, int nWeapon)
{
    for (int w : r.nWeapon)
        if (w > 0 && w == nWeapon)
            return true;
    return false;
}

} // namespace

void weaponListDraw(PLAYER *pPlayer)
{
    if (!pPlayer || !pPlayer->pXSprite)
        return;
    // (the weapon is 0 while the old one goes down and the new one comes up)
    int const nWeapon = pPlayer->curWeapon;
    if (nWeapon > 0 && nWeapon != nLastWeapon)
    {
        // the first weapon of a level (or a loaded game) isn't a switch
        if (nLastWeapon > 0 && (int)totalclock > 120)
            nShowUntil = (int)totalclock + kShowTime;
        nLastWeapon = nWeapon;
    }
    if (!gWeaponSwitchList || pPlayer->pXSprite->health <= 0 || (int)totalclock >= nShowUntil || (int)totalclock < nShowUntil - kShowTime)
        return;

    int nRows = 0;
    for (auto const &r : gRows)
        nRows += rowHas(pPlayer, r);
    int y = 86 - nRows * kRowHeight / 2 + kRowHeight / 2;  // above the status bar
    for (auto const &r : gRows)
    {
        if (!rowHas(pPlayer, r))
            continue;
        bool const bSelected = rowIs(r, nWeapon);
        int const nPic = pictureFor(r.nWeapon[0]);
        int const nShade = bSelected ? -16 : 12;  // the new one bright and bigger, the others dimmer
        int const nStat = RS_AUTO;
        if (nPic > 0 && tilesiz[nPic].x > 0 && tilesiz[nPic].y > 0)
        {
            int nScale = min(divscale16(kIconW, tilesiz[nPic].x), divscale16(kIconH, tilesiz[nPic].y));
            if (bSelected)
                nScale += nScale >> 2;
            // centred on the icon box, whatever the tile's own offsets
            int const xo = picanm[nPic].xofs, yo = picanm[nPic].yofs;
            int const x = (kRight - kIconW / 2) * 65536 + mulscale16(xo * 65536, nScale);
            int const yy = y * 65536 + mulscale16(yo * 65536, nScale);
            rotatesprite_fs(x, yy, nScale, 0, nPic, nShade, 0, nStat);
        }
        else
            viewDrawText(3, r.pzName, kRight, y - 3, nShade, 0, 2, 1);
        if (bSelected && nPic > 0)  // the new weapon's name next to it
            viewDrawText(3, r.pzName, kRight - kIconW - 4, y - 3, 0, 0, 2, 1);
        y += kRowHeight;
    }
}
