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

// The weapon switch list (OPTIONS > GAME SETUP > WEAPON SWITCH LIST): when the weapon changes, the
// weapons Caleb has show for a moment in a column on the right side, the new one highlighted, as
// the KEX remasters (Fresh Supply) do.

#pragma once

struct PLAYER;

extern int32_t gWeaponSwitchList;  // the option (off by default)

// once a drawn frame, after the weapon: notices a change and draws the list while it shows
void weaponListDraw(PLAYER *pPlayer);
