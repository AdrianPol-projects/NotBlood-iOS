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

// Match types and mutators of NotBlood Chosen, and match statistics. See chosenmodes.h.

#include "build.h"
#include "compat.h"
#include "mmulti.h"
#include "common_game.h"
#include "actor.h"
#include "ai.h"
#include "blood.h"
#include "db.h"
#include "globals.h"
#include "levels.h"
#include "network.h"
#include "player.h"
#include "sfx.h"
#include "sound.h"
#include "trig.h"
#include "view.h"
#include "warp.h"
#include "weapon.h"
#include "chosenmodes.h"

CHOSENRULES gChosenRules;

static const char *pzModeNames[kChosenModeMax] = { "NORMAL", "GUN GAME", "KING OF THE HILL", "JUGGERNAUT", "LAST MAN STANDING", "SURVIVAL", "CAPTURE THE FLAG" };

static const char *pzBloodbathModes[] = { "NORMAL", "GUN GAME", "KING OF THE HILL", "JUGGERNAUT", "LAST MAN STANDING" };
static const int nBloodbathModes[] = { kChosenModeNormal, kChosenModeGunGame, kChosenModeKingOfTheHill, kChosenModeJuggernaut, kChosenModeLastManStanding };
static const char *pzTeamsModes[] = { "TEAM BLOODBATH", "CAPTURE THE FLAG", "KING OF THE HILL", "LAST TEAM STANDING" };
static const int nTeamsModes[] = { kChosenModeNormal, kChosenModeCaptureFlag, kChosenModeKingOfTheHill, kChosenModeLastManStanding };
static const char *pzCoopModes[] = { "NORMAL", "SURVIVAL" };
static const int nCoopModes[] = { kChosenModeNormal, kChosenModeSurvival };

int modesForGameType(int nGameType, const char ***pppzNames, const int **ppnModes)
{
    switch (nGameType)
    {
    case kGameTypeCoop:
        *pppzNames = pzCoopModes, *ppnModes = nCoopModes;
        return ARRAY_SSIZE(pzCoopModes);
    case kGameTypeTeams:
        *pppzNames = pzTeamsModes, *ppnModes = nTeamsModes;
        return ARRAY_SSIZE(pzTeamsModes);
    default:
        *pppzNames = pzBloodbathModes, *ppnModes = nBloodbathModes;
        return ARRAY_SSIZE(pzBloodbathModes);
    }
}

const char *modesName(int nMode)
{
    return pzModeNames[ClipRange(nMode, 0, kChosenModeMax-1)];
}

//
// state of the running match (rebuilt at every level start, the same on every machine)
//

struct PLAYERSTATS
{
    int nShots, nHits, nLastShot, nLastHit;
    int nDeaths, nStreak, nBestStreak;
    int nKillsWith[kWeaponMax];
};

static PLAYERSTATS stats[kMaxPlayers];
static int nGunLevel[kMaxPlayers];
static int nLives[kMaxPlayers];
static bool bWasAlive[kMaxPlayers];
static int nJuggernaut = -1;
static int nHillX, nHillY, nHillZ, nHillSect = -1, nHillSprite = -1;
static int nHillCandidates[3], nHillCount, nHillIndex, nHillMoveAt;
static int nHillPoints[kMaxPlayers];
static int nSurvivalWave, nSurvivalBreakUntil, nSurvivalKills, nSurvivalLives, nSurvivalAlive, nSurvivalBest;
static bool bSurvivalOver;
static bool bMatchOver;
static int nDeclaredWinner = -1;

// the gun game: every kill moves the killer on to the next weapon, the last is the pitchfork
static const int nGunWeapons[] = {
    kWeaponTommy, kWeaponShotgun, kWeaponFlare, kWeaponTesla, kWeaponNapalm,
    kWeaponSprayCan, kWeaponTNT, kWeaponLifeLeech, kWeaponVoodoo, kWeaponPitchfork,
};

static int WeaponItemType(int nWeapon)
{
    switch (nWeapon)
    {
    case kWeaponFlare: return kItemWeaponFlarePistol;
    case kWeaponShotgun: return kItemWeaponSawedoff;
    case kWeaponTommy: return kItemWeaponTommygun;
    case kWeaponNapalm: return kItemWeaponNapalmLauncher;
    case kWeaponTNT: return kItemWeaponTNT;
    case kWeaponSprayCan: return kItemWeaponSprayCan;
    case kWeaponTesla: return kItemWeaponTeslaCannon;
    case kWeaponLifeLeech: return kItemWeaponLifeLeech;
    case kWeaponVoodoo: return kItemWeaponVoodooDoll;
    }
    return 0;
}

// add a weapon (keeping the others) with this much of its ammo, in percent of the most it can carry
static void GiveWeapon(PLAYER *pPlayer, int nWeapon, int nPercent)
{
    const int nItem = WeaponItemType(nWeapon);
    if (!nItem)
        return;
    pPlayer->hasWeapon[nWeapon] = 1;
    const int nAmmo = gWeaponItemData[nItem - kItemWeaponBase].ammoType;
    if (nAmmo >= 0 && nAmmo < 12)
        pPlayer->ammoCount[nAmmo] = ClipHigh(pPlayer->ammoCount[nAmmo] + gAmmoInfo[nAmmo].max * nPercent / 100, gAmmoInfo[nAmmo].max);
}

// take everything but the pitchfork away and hand over one weapon, fully loaded
static void GiveOnly(PLAYER *pPlayer, int nWeapon)
{
    for (int i = kWeaponFlare; i < kWeaponMax; i++)
        pPlayer->hasWeapon[i] = 0;
    for (int i = 0; i < 12; i++)
        pPlayer->ammoCount[i] = 0;
    pPlayer->hasWeapon[kWeaponPitchfork] = 1;
    const int nItem = WeaponItemType(nWeapon);
    if (nItem)
    {
        pPlayer->hasWeapon[nWeapon] = 1;
        const int nAmmo = gWeaponItemData[nItem - kItemWeaponBase].ammoType;
        if (nAmmo >= 0 && nAmmo < 12)
            pPlayer->ammoCount[nAmmo] = gAmmoInfo[nAmmo].max;
    }
    pPlayer->input.newWeapon = nWeapon;
}

static bool OnlyWeapon(int *pnWeapon)
{
    if (gChosenRules.nMode == kChosenModeGunGame)
        return false;
    if (gChosenRules.nWeapons >= kChosenWeaponsOnlyBase)
    {
        *pnWeapon = gChosenRules.nWeapons - kChosenWeaponsOnlyBase + kWeaponFlare;
        return *pnWeapon >= kWeaponFlare && *pnWeapon <= kWeaponVoodoo;
    }
    return false;
}

void modesClear(void)
{
    Bmemset(&gChosenRules, 0, sizeof(gChosenRules));
}

void modesSetFromPacket(void)
{
    gChosenRules.nMode = gPacketStartGame.nChosenMode;
    gChosenRules.nLives = gPacketStartGame.nChosenLives;
    gChosenRules.nGravity = gPacketStartGame.nChosenGravity;
    gChosenRules.nWeapons = gPacketStartGame.nChosenWeapons;
    gChosenRules.bVampire = gPacketStartGame.bChosenVampire;
    gChosenRules.bOneHit = gPacketStartGame.bChosenOneHit;
    gChosenRules.nWaves = (uint8_t)gPacketStartGame.nChosenWaves < kChosenWavesMax ? gPacketStartGame.nChosenWaves : kChosenWavesNormal;
    for (int i = 0; i < kMaxPlayers; i++)
        gChosenRules.nTeam[i] = gGameOptions.nGameType == kGameTypeTeams ? ClipRange(gPacketStartGame.nChosenTeam[i], 0, 2) : 0;
    // modes that don't fit the game type count as normal
    const char **ppzNames;
    const int *pnModes;
    const int nCount = modesForGameType(gGameOptions.nGameType, &ppzNames, &pnModes);
    bool bFits = false;
    for (int i = 0; i < nCount; i++)
        bFits |= pnModes[i] == gChosenRules.nMode;
    if (!bFits)
        gChosenRules.nMode = kChosenModeNormal;
}

//
// king of the hill: the best connected open places of the map, one after another
//

static void PickHills(void)
{
    nHillCount = 0;
    int nScores[3] = { INT32_MIN, INT32_MIN, INT32_MIN };
    // the middle of where players start: hills near it are central and everybody can get there
    int cx = 0, cy = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        cx += gStartZone[i].x / kMaxPlayers, cy += gStartZone[i].y / kMaxPlayers;
    for (int s = 0; s < numsectors; s++)
    {
        int x = 0, y = 0;
        const int nWalls = sector[s].wallnum;
        if (nWalls < 3)
            continue;
        int nPortals = 0;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + nWalls; w++)
        {
            x += wall[w].x, y += wall[w].y;
            if (wall[w].nextsector >= 0 && !(wall[w].cstat & 1))
                nPortals++;
        }
        x /= nWalls, y /= nWalls;
        if (!inside(x, y, s) || nPortals < 2)
            continue;
        if (sector[s].extra > 0 && (xsector[sector[s].extra].Underwater || xsector[sector[s].extra].damageType))
            continue;
        const int zFloor = getflorzofslope(s, x, y);
        if (zFloor - getceilzofslope(s, x, y) < (72<<8))
            continue;
        // roomy and open: walls a good way off all round
        int nMinWall = 0x7fffffff;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + nWalls; w++)
            nMinWall = min(nMinWall, approxDist(wall[w].x - x, wall[w].y - y));
        if (nMinWall < 500)
            continue;
        const int nScore = nPortals * 100 + ClipHigh(nMinWall, 3000) / 10 - approxDist(x - cx, y - cy) / 20;
        for (int i = 0; i < 3; i++)
        {
            if (nScore > nScores[i])
            {
                for (int j = 2; j > i; j--)
                    nScores[j] = nScores[j-1], nHillCandidates[j] = nHillCandidates[j-1];
                nScores[i] = nScore, nHillCandidates[i] = s;
                break;
            }
        }
    }
    for (int i = 0; i < 3; i++)
        if (nScores[i] != INT32_MIN)
            nHillCount = i + 1;
}

static void PlaceHill(int nIndex)
{
    if (nHillSprite >= 0)
    {
        DeleteSprite(nHillSprite);
        nHillSprite = -1;
    }
    if (!nHillCount)
    {
        nHillSect = -1;
        return;
    }
    nHillIndex = nIndex % nHillCount;
    const int s = nHillCandidates[nHillIndex];
    int x = 0, y = 0;
    for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
        x += wall[w].x, y += wall[w].y;
    nHillX = x / sector[s].wallnum, nHillY = y / sector[s].wallnum;
    nHillZ = getflorzofslope(s, nHillX, nHillY);
    nHillSect = s;
    LOG_F(INFO, "King of the hill: sector %d at %d,%d", s, nHillX, nHillY);
    // a flag on a pole marks it (just a picture, nothing to pick up)
    spritetype *pMarker = actSpawnSprite(s, nHillX, nHillY, nHillZ, kStatDecoration, false);
    if (pMarker)
    {
        const ITEMDATA *pData = &gItemData[kItemFlagA - kItemBase];
        pMarker->picnum = pData->picnum;
        pMarker->xrepeat = pData->xrepeat, pMarker->yrepeat = pData->yrepeat;
        pMarker->pal = 0;
        pMarker->shade = -8;
        pMarker->cstat = 0;
        nHillSprite = pMarker->index;
    }
}

static bool OnHill(PLAYER *pPlayer)
{
    if (nHillSect < 0 || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        return false;
    spritetype *pSprite = pPlayer->pSprite;
    if (approxDist(pSprite->x - nHillX, pSprite->y - nHillY) > 700 || klabs(pSprite->z - nHillZ) > (64<<8))
        return false;
    return cansee(nHillX, nHillY, nHillZ - (32<<8), nHillSect, pSprite->x, pSprite->y, pSprite->z - (32<<8), pSprite->sectnum);
}

bool modesHill(int *px, int *py, int *pRadius)
{
    if (gChosenRules.nMode != kChosenModeKingOfTheHill || nHillSect < 0)
        return false;
    *px = nHillX, *py = nHillY, *pRadius = 700;
    return true;
}

int modesJuggernaut(void)
{
    return gChosenRules.nMode == kChosenModeJuggernaut ? nJuggernaut : -1;
}

int modesGunLevel(int nPlayer)
{
    return nGunLevel[nPlayer];
}

static void MakeJuggernaut(int nPlayer)
{
    nJuggernaut = nPlayer;
    if (nPlayer < 0)
        return;
    PLAYER *pPlayer = &gPlayer[nPlayer];
    if (pPlayer->pXSprite && pPlayer->pXSprite->health > 0)
    {
        pPlayer->pXSprite->health = 200<<4;
        for (int i = 0; i < 3; i++)
            pPlayer->armor[i] = 200<<4;
    }
    char buffer[80];
    sprintf(buffer, "\r%s\r is the JUGGERNAUT!", gProfile[nPlayer].name);
    viewSetMessageColor(buffer, 0, MESSAGE_PRIORITY_NORMAL, gColorMsg && !VanillaMode() ? playerColorPalMessage(pPlayer->teamId) : 0);
    sndStartSample("NOTBLOOD2", 128, -1, 22050);
}

static void DeclareWinner(int nPlayer, const char *pzReason)
{
    if (gPlayerRoundEnding)
        return;
    char buffer[96];
    if (nPlayer >= 0)
        sprintf(buffer, "\r%s\r %s!", gProfile[nPlayer].name, pzReason);
    else
        sprintf(buffer, "%s", pzReason);
    const int nPal = nPlayer >= 0 && gColorMsg && !VanillaMode() ? playerColorPalMessage(gPlayer[nPlayer].teamId) : 0;
    viewDrawWinner(buffer, nPal);
    viewSetMessageColor(buffer, 0, MESSAGE_PRIORITY_NORMAL, nPal);
    gPlayerRoundEnding = 1;
    gPacketStartGame.uNetGameFlags |= kNetGameFlagNoLevelExit;
    bMatchOver = true;
    nDeclaredWinner = nPlayer;
}

int modesWinner(void)
{
    return nDeclaredWinner;
}

int modesDeaths(int nPlayer)
{
    return stats[nPlayer].nDeaths;
}

int modesBestStreak(int nPlayer)
{
    return stats[nPlayer].nBestStreak;
}

//
// survival: waves of monsters, each a bit bigger and nastier
//

static void LoadSurvivalBest(void)
{
    nSurvivalBest = 0;
    FILE *fp = fopen("notblood_survival.txt", "r");
    if (!fp)
        return;
    char line[160];
    const char *pzMap = levelGetTitle();
    while (fgets(line, sizeof(line), fp))
    {
        int nWave = 0;
        char *pTab = strchr(line, '\t');
        if (!pTab)
            continue;
        *pTab = 0;
        nWave = atoi(pTab + 1);
        if (pzMap && !Bstrcasecmp(line, pzMap))
            nSurvivalBest = nWave;
    }
    fclose(fp);
}

static void SaveSurvivalBest(int nWave)
{
    if (nWave <= nSurvivalBest)
        return;
    nSurvivalBest = nWave;
    // keep everybody else's maps
    char lines[64][160];
    int nLines = 0;
    const char *pzMap = levelGetTitle();
    if (!pzMap)
        return;
    FILE *fp = fopen("notblood_survival.txt", "r");
    if (fp)
    {
        char line[160];
        while (nLines < 63 && fgets(line, sizeof(line), fp))
        {
            char *pTab = strchr(line, '\t');
            if (!pTab || (pTab - line == (ptrdiff_t)strlen(pzMap) && !Bstrncasecmp(line, pzMap, strlen(pzMap))))
                continue;
            Bstrncpyz(lines[nLines++], line, sizeof(lines[0]));
        }
        fclose(fp);
    }
    FILE *out = fopen("notblood_survival.txt", "w");
    if (!out)
        return;
    for (int i = 0; i < nLines; i++)
        fputs(lines[i], out);
    fprintf(out, "%s\t%d\n", pzMap, nWave);
    fclose(out);
}

static const short nWaveMonsters[][4] = {
    { kDudeZombieAxeNormal, kDudeCultistShotgun, kDudeZombieAxeNormal, kDudeCultistTommy },  // waves 1-2
    { kDudeCultistTommy, kDudeCultistShotgun, kDudeZombieButcher, kDudeHellHound },          // 3-4
    { kDudeGargoyleFlesh, kDudeCultistTNT, kDudePhantasm, kDudeHellHound },                  // 5-6
    { kDudeCultistTesla, kDudeGargoyleFlesh, kDudeGillBeast, kDudePhantasm },                // 7-8
    { kDudeGargoyleStone, kDudeCultistTesla, kDudeCultistBeast, kDudeHellHound },            // 9-10
    { kDudeBeast, kDudeGargoyleStone, kDudeCerberusTwoHead, kDudeCultistTesla },             // 11+
};

// the WAVES setting: how many come, how soon the nastier ones do, how long the breaks are, what the players get
struct WAVESETTINGS
{
    int nBase, nHalvesPerWave, nMax;  // monsters in wave n: nBase + n * nHalvesPerWave / 2, at most nMax
    int nWavesPerTier, nMixChance;    // a stronger tier every so many waves; percent from the tier above
    int nBreak;                       // seconds between waves
    int nAmmo;                        // percent of the normal ammo handed out
    int nHealth, nArmor;              // given every wave, up to 100
};

static const WAVESETTINGS waveSettings[kChosenWavesMax] = {
    { 3, 4, 40, 2, 20,  8, 100,  0,  0 }, // normal: as it always was
    { 3, 3, 30, 3, 15, 15, 150, 25,  0 }, // easy
    { 2, 2, 20, 4, 10, 20, 200, 50, 50 }, // very easy
    { 5, 5, 50, 2, 35,  5, 100,  0,  0 }, // hard
};

static const WAVESETTINGS *Waves(void)
{
    return &waveSettings[gChosenRules.nWaves < kChosenWavesMax ? gChosenRules.nWaves : kChosenWavesNormal];
}

// a map made for survival marks where monsters come in with dude spawn markers nothing triggers
static short nSpawnSpots[64];
static int nSpawnSpotCount;

static void FindSpawnSpots(void)
{
    nSpawnSpotCount = 0;
    for (int i = 0; i < kMaxSprites && nSpawnSpotCount < ARRAY_SSIZE(nSpawnSpots); i++)
    {
        spritetype *pSprite = &sprite[i];
        if (pSprite->statnum >= kMaxStatus || pSprite->type != kMarkerDudeSpawn || pSprite->sectnum < 0)
            continue;
        if (pSprite->extra > 0 && (xsprite[pSprite->extra].rxID || xsprite[pSprite->extra].txID))
            continue; // the map's own spawner
        nSpawnSpots[nSpawnSpotCount++] = i;
    }
    if (nSpawnSpotCount)
        LOG_F(INFO, "Survival: %d monster spawn spots in this map", nSpawnSpotCount);
}

// where monsters come in: the map's spawn spots, or else where players start and where items lie (places everybody can get to)
static bool SpawnPoint(int *px, int *py, int *pz, int *pSector)
{
    int nItems = 0;
    for (int i = headspritestat[kStatItem]; i >= 0; i = nextspritestat[i])
        nItems++;
    for (int nTry = 0; nTry < 40; nTry++)
    {
        int x, y, s;
        const int nPick = nSpawnSpotCount ? 0 : Random(kMaxPlayers + nItems);
        if (nSpawnSpotCount)
        {
            const spritetype *pSpot = &sprite[nSpawnSpots[Random(nSpawnSpotCount)]];
            x = pSpot->x, y = pSpot->y, s = pSpot->sectnum;
        }
        else if (nPick < kMaxPlayers)
            x = gStartZone[nPick].x, y = gStartZone[nPick].y, s = gStartZone[nPick].sectnum;
        else
        {
            int i = headspritestat[kStatItem];
            for (int n = nPick - kMaxPlayers; n > 0 && i >= 0; n--)
                i = nextspritestat[i];
            if (i < 0)
                continue;
            x = sprite[i].x, y = sprite[i].y, s = sprite[i].sectnum;
        }
        if (s < 0 || s >= numsectors)
            continue;
        if (sector[s].extra > 0 && (xsector[sector[s].extra].Underwater || xsector[sector[s].extra].damageType))
            continue;
        const int z = getflorzofslope(s, x, y);
        // not right next to anybody, not in plain sight if it can help it, but near enough to find them
        bool bBad = false;
        int nNearest = 0x7fffffff;
        for (int p = connecthead; p >= 0 && !bBad; p = connectpoint2[p])
        {
            spritetype *pPlayerSprite = gPlayer[p].pSprite;
            if (!pPlayerSprite || !gPlayer[p].pXSprite || gPlayer[p].pXSprite->health <= 0)
                continue;
            const int nDist = approxDist(pPlayerSprite->x - x, pPlayerSprite->y - y);
            nNearest = min(nNearest, nDist);
            bBad = nDist < 2000 || (nTry < 25 && nDist < 8000 && cansee(x, y, z - (40<<8), s, pPlayerSprite->x, pPlayerSprite->y, pPlayerSprite->z - (40<<8), pPlayerSprite->sectnum));
        }
        if (bBad || (nTry < 30 && nNearest > 7000 && !nSpawnSpotCount))
            continue;
        *px = x, *py = y, *pz = z, *pSector = s;
        return true;
    }
    return false;
}

static int nWaveStart;

// a wave that drags on: the stragglers come for the players, and in the end they're cleared away
static void HurryStragglers(bool bRemove)
{
    for (int i = headspritestat[kStatDude]; i >= 0; )
    {
        const int nNext = nextspritestat[i];
        spritetype *pSprite = &sprite[i];
        if (!IsPlayerSprite(pSprite) && pSprite->extra > 0 && xsprite[pSprite->extra].health > 0)
        {
            if (bRemove)
                actPostSprite(i, kStatFree);
            else
            {
                // lost far away: bring it in closer (out of sight)
                int nNearest = 0x7fffffff;
                for (int p = connecthead; p >= 0; p = connectpoint2[p])
                    if (gPlayer[p].pXSprite && gPlayer[p].pXSprite->health > 0)
                        nNearest = min(nNearest, approxDist(gPlayer[p].pSprite->x - pSprite->x, gPlayer[p].pSprite->y - pSprite->y));
                int x, y, z, nSector;
                if (nNearest > 7000 && gLevelTime - nWaveStart > kTicsPerSec * 25 && xsprite[pSprite->extra].target < 0 && SpawnPoint(&x, &y, &z, &nSector))
                {
                    int top, bottom;
                    GetSpriteExtents(pSprite, &top, &bottom);
                    vec3_t pos = { x, y, z - (bottom - pSprite->z) };
                    setsprite(i, &pos);
                }
                int nTarget = -1, nBest = 0x7fffffff;
                for (int p = connecthead; p >= 0; p = connectpoint2[p])
                {
                    PLAYER *pPlayer = &gPlayer[p];
                    if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
                        continue;
                    const int nDist = approxDist(pPlayer->pSprite->x - pSprite->x, pPlayer->pSprite->y - pSprite->y);
                    if (nDist < nBest)
                        nBest = nDist, nTarget = pPlayer->nSprite;
                }
                XSPRITE *pXSprite = &xsprite[pSprite->extra];
                if (nTarget >= 0 && pXSprite->target != nTarget)
                {
                    // head for it (they find it on the way), then fight it
                    aiSetTarget(pXSprite, nTarget);
                    aiActivateDude(pSprite, pXSprite);
                }
            }
        }
        i = nNext;
    }
}

static void StartWave(void)
{
    const WAVESETTINGS *pWaves = Waves();
    nSurvivalWave++;
    nWaveStart = gLevelTime;
    // ammo for everybody still standing, more the further it goes
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
            continue;
        GiveWeapon(pPlayer, kWeaponShotgun, 20 * pWaves->nAmmo / 100);
        GiveWeapon(pPlayer, kWeaponTommy, 20 * pWaves->nAmmo / 100);
        if (nSurvivalWave >= 3)
            GiveWeapon(pPlayer, kWeaponNapalm, 15 * pWaves->nAmmo / 100);
        if (nSurvivalWave >= 5)
            GiveWeapon(pPlayer, kWeaponTesla, 15 * pWaves->nAmmo / 100);
        if (nSurvivalWave >= 7)
            GiveWeapon(pPlayer, kWeaponLifeLeech, 15 * pWaves->nAmmo / 100);
        if (pWaves->nHealth && pPlayer->pXSprite->health < (100 << 4))
            pPlayer->pXSprite->health = ClipHigh(pPlayer->pXSprite->health + (pWaves->nHealth << 4), 100 << 4);
        if (pWaves->nArmor && pPlayer->armor[1] < (100 << 4))
            pPlayer->armor[1] = ClipHigh(pPlayer->armor[1] + (pWaves->nArmor << 4), 100 << 4);
    }
    const int nTier = ClipHigh((nSurvivalWave - 1) / pWaves->nWavesPerTier, ARRAY_SSIZE(nWaveMonsters) - 1);
    const int nCount = ClipHigh(pWaves->nBase + nSurvivalWave * pWaves->nHalvesPerWave / 2, pWaves->nMax);
    for (int i = 0; i < nCount; i++)
    {
        int x, y, z, nSector;
        if (!SpawnPoint(&x, &y, &z, &nSector))
            break;
        // stronger ones mix in from the tier above as the waves go on
        int nRow = nTier;
        if (nRow + 1 < ARRAY_SSIZE(nWaveMonsters) && Random(100) < pWaves->nMixChance)
            nRow++;
        const int nType = nWaveMonsters[nRow][Random(4)];
        spritetype *pSpawner = actSpawnSprite(nSector, x, y, z, kStatDecoration, true);
        if (!pSpawner)
            break;
        pSpawner->ang = Random(2048);
        spritetype *pDude = actSpawnDude(pSpawner, nType, -1, 0);
        DeleteSprite(pSpawner->index);
        if (!pDude)
            continue;
        aiInitSprite(pDude);
        // they know where the players are: it's a horde
        int nTarget = -1, nBest = 0x7fffffff;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            PLAYER *pPlayer = &gPlayer[p];
            if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
                continue;
            const int nDist = approxDist(pPlayer->pSprite->x - x, pPlayer->pSprite->y - y);
            if (nDist < nBest)
                nBest = nDist, nTarget = pPlayer->nSprite;
        }
        if (nTarget >= 0 && pDude->extra > 0)
        {
            aiSetTarget(&xsprite[pDude->extra], nTarget);
            aiActivateDude(pDude, &xsprite[pDude->extra]);
        }
    }
    char buffer[64];
    sprintf(buffer, "Wave %d!", nSurvivalWave);
    viewSetMessage(buffer, 8, MESSAGE_PRIORITY_NORMAL);
    sndStartSample("NOTBLOOD3", 64, -1, 11025);
}

static int CountMonsters(void)
{
    int nCount = 0;
    for (int i = headspritestat[kStatDude]; i >= 0; i = nextspritestat[i])
    {
        spritetype *pSprite = &sprite[i];
        if (IsPlayerSprite(pSprite) || pSprite->extra <= 0 || xsprite[pSprite->extra].health <= 0)
            continue;
        if (pSprite->type == kDudeBat || pSprite->type == kDudeRat || pSprite->type == kDudeInnocent)
            continue;
        nCount++;
    }
    return nCount;
}

//
// level start, spawning, frags, every frame
//

void modesStartLevel(void)
{
    if (gGameOptions.nGameType == kGameTypeTeams) // who plays where, for the log
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            LOG_F(INFO, "Player %d %s: %s Team", p + 1, gProfile[p].name, (gPlayer[p].teamId & 1) ? "Red" : "Blue");
    Bmemset(stats, 0, sizeof(stats));
    Bmemset(nGunLevel, 0, sizeof(nGunLevel));
    Bmemset(nHillPoints, 0, sizeof(nHillPoints));
    nJuggernaut = -1;
    nHillSect = -1;
    nHillSprite = -1;
    bMatchOver = false;
    nDeclaredWinner = -1;
    bSurvivalOver = false;
    nSurvivalWave = nSurvivalKills = 0;
    nSurvivalBreakUntil = kTicsPerSec * 5;
    nSurvivalLives = gChosenRules.nLives;
    nSurvivalAlive = 0;
    for (int p = 0; p < kMaxPlayers; p++)
    {
        nLives[p] = gChosenRules.nLives ? gChosenRules.nLives : 3;
        bWasAlive[p] = true;
    }
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return;
    // weapon sets: the other weapons lying about would only confuse
    int nOnly = 0;
    const bool bOnly = OnlyWeapon(&nOnly);
    if (bOnly || gChosenRules.nMode == kChosenModeGunGame)
    {
        for (int i = headspritestat[kStatItem]; i >= 0; )
        {
            const int nNext = nextspritestat[i];
            const int nType = sprite[i].type;
            if (nType >= kItemWeaponBase && nType < kItemWeaponMax && !(bOnly && nType == WeaponItemType(nOnly)))
                DeleteSprite(i);
            i = nNext;
        }
    }
    if (gChosenRules.nMode == kChosenModeKingOfTheHill)
    {
        PickHills();
        PlaceHill(0);
        nHillMoveAt = kTicsPerSec * 90;
    }
    if (gChosenRules.nMode == kChosenModeSurvival)
    {
        LoadSurvivalBest();
        FindSpawnSpots();
        LOG_F(INFO, "Survival: waves setting %d", gChosenRules.nWaves);
        // with the match's monsters set to none the game never got monsters ready to take damage
        for (int i = 0; i < kDudeMax - kDudeBase; i++)
            for (int j = 0; j < kDamageMax; j++)
                dudeInfo[i].curDamage[j] = mulscale8(DudeDifficulty[ClipRange(gGameOptions.nEnemyHealth, 0, 4)], dudeInfo[i].startDamage[j]);
    }
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        modesPlayerSpawned(&gPlayer[p]);
}

void modesPlayerSpawned(PLAYER *pPlayer)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer || !pPlayer->pXSprite)
        return;
    bWasAlive[pPlayer->nPlayer] = true;
    stats[pPlayer->nPlayer].nStreak = 0;
    int nOnly = 0;
    if (gChosenRules.nMode == kChosenModeGunGame)
        GiveOnly(pPlayer, nGunWeapons[ClipRange(nGunLevel[pPlayer->nPlayer], 0, ARRAY_SSIZE(nGunWeapons) - 1)]);
    else if (OnlyWeapon(&nOnly))
        GiveOnly(pPlayer, nOnly);
    else if (gChosenRules.nWeapons == kChosenWeaponsRandom)
        GiveOnly(pPlayer, kWeaponFlare + Random(kWeaponVoodoo - kWeaponFlare + 1));
    else if (gChosenRules.nMode == kChosenModeSurvival)
    {
        // a horde needs guns
        GiveWeapon(pPlayer, kWeaponFlare, 30 * Waves()->nAmmo / 100);
        GiveWeapon(pPlayer, kWeaponShotgun, 40 * Waves()->nAmmo / 100);
        GiveWeapon(pPlayer, kWeaponTommy, 40 * Waves()->nAmmo / 100);
        pPlayer->input.newWeapon = kWeaponShotgun;
    }
    if (gChosenRules.nMode == kChosenModeJuggernaut && nJuggernaut == pPlayer->nPlayer)
        nJuggernaut = -1; // it died: whoever kills next takes it on
}

void modesFrag(PLAYER *pKiller, PLAYER *pVictim)
{
    const int nKiller = pKiller->nPlayer, nVictim = pVictim->nPlayer;
    if (nKiller != nVictim)
    {
        PLAYERSTATS *pStats = &stats[nKiller];
        pStats->nKillsWith[ClipRange(pKiller->curWeapon, 0, kWeaponMax-1)]++;
        if (pKiller->pXSprite && pKiller->pXSprite->health > 0)
        {
            pStats->nStreak++;
            pStats->nBestStreak = max(pStats->nBestStreak, pStats->nStreak);
        }
    }
    if (gPlayerRoundEnding)
        return;
    const bool bEnemy = nKiller != nVictim && (gGameOptions.nGameType != kGameTypeTeams || pKiller->teamId != pVictim->teamId);
    switch (gChosenRules.nMode)
    {
    case kChosenModeGunGame:
        if (bEnemy)
        {
            nGunLevel[nKiller]++;
            if (nGunLevel[nKiller] >= ARRAY_SSIZE(nGunWeapons))
            {
                DeclareWinner(nKiller, "won the gun game");
                break;
            }
            if (pKiller->pXSprite && pKiller->pXSprite->health > 0)
                GiveOnly(pKiller, nGunWeapons[nGunLevel[nKiller]]);
            if (pKiller == gMe)
            {
                char buffer[64];
                sprintf(buffer, "Level %d of %d", nGunLevel[nKiller] + 1, (int)ARRAY_SSIZE(nGunWeapons));
                viewSetMessage(buffer, 8, MESSAGE_PRIORITY_NORMAL);
            }
        }
        else if (nKiller == nVictim && nGunLevel[nKiller] > 0)
            nGunLevel[nKiller]--; // killed itself: back one weapon
        break;
    case kChosenModeKingOfTheHill:
        // kills don't score, the hill does
        if (gGameOptions.nGameType == kGameTypeTeams)
        {
            if (bEnemy)
                gPlayerScores[pKiller->teamId]--;
            else
                gPlayerScores[pKiller->teamId]++;
        }
        else
            pKiller->fragCount = nHillPoints[nKiller];
        break;
    case kChosenModeJuggernaut:
        if (!bEnemy)
            break;
        if (nJuggernaut < 0 || nVictim == nJuggernaut)
        {
            if (nVictim == nJuggernaut && pKiller != gMe && pVictim == gMe)
                viewSetMessage("You lost the juggernaut", 8, MESSAGE_PRIORITY_NORMAL);
            MakeJuggernaut(nKiller);
        }
        else if (nKiller != nJuggernaut)
            pKiller->fragCount--; // only the juggernaut's kills and kills of the juggernaut score
        break;
    }
}

static void PlayerDied(int nPlayer)
{
    stats[nPlayer].nDeaths++;
    stats[nPlayer].nStreak = 0;
    if (gChosenRules.nMode == kChosenModeLastManStanding)
    {
        if (nLives[nPlayer] > 0)
            nLives[nPlayer]--;
        if (!nLives[nPlayer])
        {
            char buffer[64];
            sprintf(buffer, "\r%s\r is out!", gProfile[nPlayer].name);
            viewSetMessageColor(buffer, 0, MESSAGE_PRIORITY_NORMAL, gColorMsg && !VanillaMode() ? playerColorPalMessage(gPlayer[nPlayer].teamId) : 0);
        }
    }
    if (gChosenRules.nMode == kChosenModeSurvival && gChosenRules.nLives && nSurvivalLives > 0)
        nSurvivalLives--;
}

void modesFrame(void)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return;
    // deaths, however they happened
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        const bool bAlive = pPlayer->pXSprite && pPlayer->pXSprite->health > 0;
        if (bWasAlive[p] && !bAlive)
            PlayerDied(p);
        bWasAlive[p] = bAlive;
    }
    if (gPlayerRoundEnding)
        return;
    switch (gChosenRules.nMode)
    {
    case kChosenModeLastManStanding:
    {
        if (gGameOptions.nGameType == kGameTypeTeams)
        {
            // last team standing: the last team with somebody who has lives left wins
            bool bHasPlayers[2] = { false, false }, bIn[2] = { false, false };
            for (int p = connecthead; p >= 0; p = connectpoint2[p])
            {
                const int nTeam = gPlayer[p].teamId & 1;
                bHasPlayers[nTeam] = true;
                if (nLives[p] > 0)
                    bIn[nTeam] = true;
            }
            if (bHasPlayers[0] && bHasPlayers[1] && bIn[0] != bIn[1])
                DeclareWinner(-1, bIn[0] ? "Blue Team is the last team standing!" : "Red Team is the last team standing!");
            break;
        }
        // the last one with lives left wins
        int nIn = 0, nLast = -1;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (nLives[p] > 0)
                nIn++, nLast = p;
        if (nIn <= 1 && gNetPlayers >= 2)
            DeclareWinner(nLast, "is the last one standing");
        break;
    }
    case kChosenModeKingOfTheHill:
    {
        if (gLevelTime >= nHillMoveAt && nHillCount > 1)
        {
            PlaceHill(nHillIndex + 1);
            nHillMoveAt = gLevelTime + kTicsPerSec * 90;
            viewSetMessage("The hill moved!", 8, MESSAGE_PRIORITY_NORMAL);
        }
        if (gLevelTime % kTicsPerSec)
            break;
        // a point a second for holding it alone (or only with team mates)
        int nOn = -1, nTeam = -1;
        bool bContested = false;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            if (!OnHill(&gPlayer[p]))
                continue;
            const int nWho = gGameOptions.nGameType == kGameTypeTeams ? gPlayer[p].teamId : p;
            if (nTeam >= 0 && nWho != nTeam)
                bContested = true;
            nTeam = nWho, nOn = p;
        }
        if (nOn < 0 || bContested)
            break;
        if (gGameOptions.nGameType == kGameTypeTeams)
        {
            gPlayerScores[nTeam]++;
            gPlayerScoreTicks[nTeam] += 30;
        }
        else
        {
            nHillPoints[nOn]++;
            gPlayer[nOn].fragCount = nHillPoints[nOn];
        }
        if (gPlayerRoundScoreLimit)
            playerProcessRoundCheck(&gPlayer[nOn]);
        else if ((gGameOptions.nGameType == kGameTypeTeams ? gPlayerScores[nTeam] : nHillPoints[nOn]) >= 120)
            DeclareWinner(gGameOptions.nGameType == kGameTypeTeams ? -1 : nOn, gGameOptions.nGameType == kGameTypeTeams ? (nTeam ? "Red Team holds the hill!" : "Blue Team holds the hill!") : "is the king of the hill");
        break;
    }
    case kChosenModeJuggernaut:
        if (nJuggernaut >= 0)
        {
            PLAYER *pJuggernaut = &gPlayer[nJuggernaut];
            if (pJuggernaut->pXSprite && pJuggernaut->pXSprite->health > 0)
                pJuggernaut->pwUpTime[kPwUpTwoGuns] = ClipLow(pJuggernaut->pwUpTime[kPwUpTwoGuns], kTicsPerSec * 4);
        }
        break;
    case kChosenModeSurvival:
    {
        if (bSurvivalOver)
            break;
        const int nAlive = CountMonsters();
        if (nAlive != nSurvivalAlive)
            LOG_F(INFO, "Survival: wave %d, %d monsters left", nSurvivalWave, nAlive);
        if (nAlive < nSurvivalAlive)
            nSurvivalKills += nSurvivalAlive - nAlive;
        nSurvivalAlive = nAlive;
        if (nSurvivalBreakUntil)
        {
            if (gLevelTime >= nSurvivalBreakUntil)
            {
                nSurvivalBreakUntil = 0;
                StartWave();
                nSurvivalAlive = CountMonsters();
            }
        }
        else if (nAlive && !(gLevelTime % (kTicsPerSec * 2)))
            HurryStragglers(false); // a horde: they all come for the players
        else if (nAlive && gLevelTime - nWaveStart > kTicsPerSec * 150)
        {
            HurryStragglers(true);
            nSurvivalAlive = 0;
        }
        else if (!nAlive)
        {
            nSurvivalBreakUntil = gLevelTime + kTicsPerSec * Waves()->nBreak; // a breather between waves
            SaveSurvivalBest(nSurvivalWave);
        }
        // everybody down and no lives left: over
        bool bAnyAlive = false;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            bAnyAlive |= gPlayer[p].pXSprite && gPlayer[p].pXSprite->health > 0;
        if (!bAnyAlive && gChosenRules.nLives && nSurvivalLives <= 0)
        {
            bSurvivalOver = true;
            SaveSurvivalBest(nSurvivalWave - 1);
            char buffer[96];
            sprintf(buffer, "Overrun at wave %d (%d kills)", nSurvivalWave, nSurvivalKills);
            DeclareWinner(-1, buffer);
        }
        break;
    }
    }
}

bool modesCanRespawn(PLAYER *pPlayer)
{
    if (gChosenRules.nMode == kChosenModeLastManStanding)
        return nLives[pPlayer->nPlayer] > 0;
    if (gChosenRules.nMode == kChosenModeSurvival && gChosenRules.nLives)
        return nSurvivalLives > 0;
    return true;
}

bool modesOutOfLives(int nPlayer)
{
    return gChosenRules.nMode == kChosenModeLastManStanding && nLives[nPlayer] <= 0;
}

bool modesWeaponPickup(PLAYER *pPlayer, int nWeapon)
{
    UNREFERENCED_PARAMETER(pPlayer);
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return true;
    if (gChosenRules.nMode == kChosenModeGunGame)
        return false;
    int nOnly;
    if (OnlyWeapon(&nOnly))
        return nWeapon == nOnly;
    return true;
}

int modesDamage(int nSource, spritetype *pTarget, int nDamage)
{
    if (!gChosenRules.bOneHit || gGameOptions.nGameType == kGameTypeSinglePlayer || nSource < 0 || nDamage <= 0)
        return nDamage;
    if (IsPlayerSprite(pTarget) && IsPlayerSprite(&sprite[nSource]) && &sprite[nSource] != pTarget)
        return 2000<<4; // one hit and it's over
    return nDamage;
}

void modesDamageDone(PLAYER *pSource, spritetype *pTarget, int nDamage)
{
    if (!pSource || pSource->pSprite == pTarget || gGameOptions.nGameType == kGameTypeSinglePlayer)
        return;
    PLAYERSTATS *pStats = &stats[pSource->nPlayer];
    if (pStats->nLastHit != gFrame)
        pStats->nHits++, pStats->nLastHit = gFrame;
    if (gChosenRules.bVampire && IsPlayerSprite(pTarget) && pSource->pXSprite && pSource->pXSprite->health > 0)
        pSource->pXSprite->health = ClipHigh(pSource->pXSprite->health + nDamage / 2, 200<<4);
}

void modesShot(PLAYER *pPlayer)
{
    PLAYERSTATS *pStats = &stats[pPlayer->nPlayer];
    if (pStats->nLastShot != gFrame)
        pStats->nShots++, pStats->nLastShot = gFrame;
}

int modesGravity(spritetype *pSprite, int nGravity)
{
    if (!gChosenRules.nGravity || gGameOptions.nGameType == kGameTypeSinglePlayer || !IsPlayerSprite(pSprite))
        return nGravity;
    return gChosenRules.nGravity == 1 ? nGravity / 2 : nGravity / 5;
}

//
// on the screen
//

static const char *pzWeaponNames[kWeaponMax] = { "-", "PITCHFORK", "FLARE GUN", "SHOTGUN", "TOMMY GUN", "NAPALM", "TNT", "SPRAY CAN", "TESLA", "LIFE LEECH", "VOODOO DOLL", "PROXIMITY TNT", "REMOTE TNT", "BEAST" };

void modesDrawHud(void)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer || !gView || gChosenRules.nMode == kChosenModeNormal)
        return;
    char buffer[96] = "";
    const int nView = gView->nPlayer;
    switch (gChosenRules.nMode)
    {
    case kChosenModeGunGame:
        sprintf(buffer, "GUN GAME %d/%d: %s", nGunLevel[nView] + 1, (int)ARRAY_SSIZE(nGunWeapons), pzWeaponNames[nGunWeapons[ClipRange(nGunLevel[nView], 0, ARRAY_SSIZE(nGunWeapons) - 1)]]);
        break;
    case kChosenModeKingOfTheHill:
        if (nHillSect >= 0)
        {
            const int nDist = approxDist(gView->pSprite->x - nHillX, gView->pSprite->y - nHillY) / 16;
            if (OnHill(gView))
                sprintf(buffer, "ON THE HILL!");
            else
                sprintf(buffer, "HILL: %d AWAY", nDist);
        }
        break;
    case kChosenModeJuggernaut:
        if (nJuggernaut < 0)
            sprintf(buffer, "JUGGERNAUT: FIRST KILL TAKES IT");
        else if (nJuggernaut == nView)
            sprintf(buffer, "YOU ARE THE JUGGERNAUT");
        else
            sprintf(buffer, "JUGGERNAUT: %s", gProfile[nJuggernaut].name);
        break;
    case kChosenModeLastManStanding:
        sprintf(buffer, nLives[nView] ? "LIVES: %d" : "OUT - WATCHING", nLives[nView]);
        break;
    case kChosenModeSurvival:
        if (nSurvivalBreakUntil)
            sprintf(buffer, "WAVE %d IN %d", nSurvivalWave + 1, ClipLow((nSurvivalBreakUntil - gLevelTime) / kTicsPerSec + 1, 0));
        else
            sprintf(buffer, "WAVE %d: %d LEFT", nSurvivalWave, nSurvivalAlive);
        if (gChosenRules.nLives)
            sprintf(buffer + strlen(buffer), "  LIVES %d", nSurvivalLives);
        if (nSurvivalBest)
            sprintf(buffer + strlen(buffer), "  BEST %d", nSurvivalBest);
        break;
    }
    if (buffer[0])
        viewDrawText(3, buffer, 160, 22, -128, 0, 1, true);
}

bool modesShowStats(void)
{
    return gGameOptions.nGameType != kGameTypeSinglePlayer && gPlayerRoundEnding;
}

void modesDrawStats(int nTop)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return;
    // everybody, best first
    int nOrder[kMaxPlayers], nCount = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        nOrder[nCount++] = p;
    for (int i = 1; i < nCount; i++)
        for (int j = i; j > 0 && gPlayer[nOrder[j]].fragCount > gPlayer[nOrder[j-1]].fragCount; j--)
            swap(&nOrder[j], &nOrder[j-1]);
    char buffer[128];
    int y = nTop;
    // columns: name, frags, deaths, aim, best streak, favourite weapon
    static const int nCols[] = { 24, 150, 182, 212, 244, 252 };
    static const char *pzHeads[] = { "PLAYER", "FRAGS", "DEATHS", "AIM", "STREAK", "BEST WEAPON" };
    for (int c = 0; c < 6; c++)
        viewDrawText(3, pzHeads[c], nCols[c], y, -128, 0, c == 0 || c == 5 ? 0 : 2, true);
    y += 10;
    for (int i = 0; i < nCount; i++)
    {
        const int p = nOrder[i];
        PLAYERSTATS *pStats = &stats[p];
        int nFav = 0;
        for (int w = 1; w < kWeaponMax; w++)
            if (pStats->nKillsWith[w] > pStats->nKillsWith[nFav])
                nFav = w;
        const int nAim = pStats->nShots ? ClipHigh(pStats->nHits * 100 / pStats->nShots, 100) : 0;
        const int nShade = p == myconnectindex ? -128 : 0;
        Bsnprintf(buffer, sizeof(buffer), "%.15s", gProfile[p].name);
        Bstrupr(buffer);
        viewDrawText(3, buffer, nCols[0], y, nShade, 0, 0, true);
        sprintf(buffer, "%d", gPlayer[p].fragCount);
        viewDrawText(3, buffer, nCols[1], y, nShade, 0, 2, true);
        sprintf(buffer, "%d", pStats->nDeaths);
        viewDrawText(3, buffer, nCols[2], y, nShade, 0, 2, true);
        sprintf(buffer, "%d%%", nAim);
        viewDrawText(3, buffer, nCols[3], y, nShade, 0, 2, true);
        sprintf(buffer, "%d", pStats->nBestStreak);
        viewDrawText(3, buffer, nCols[4], y, nShade, 0, 2, true);
        viewDrawText(3, nFav ? pzWeaponNames[nFav] : "-", nCols[5], y, nShade, 0, 0, true);
        y += 9;
    }
    // who got the better of whom
    int nNemesis = -1, nNemesisKills = 0, nVictim = -1, nVictimKills = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (p == myconnectindex)
            continue;
        if (gPlayer[p].fragInfo[myconnectindex] > nNemesisKills)
            nNemesis = p, nNemesisKills = gPlayer[p].fragInfo[myconnectindex];
        if (gPlayer[myconnectindex].fragInfo[p] > nVictimKills)
            nVictim = p, nVictimKills = gPlayer[myconnectindex].fragInfo[p];
    }
    y += 4;
    if (nNemesis >= 0)
    {
        sprintf(buffer, "YOUR NEMESIS: %s (KILLED YOU %d TIMES)", gProfile[nNemesis].name, nNemesisKills);
        Bstrupr(buffer);
        viewDrawText(3, buffer, 160, y, 0, 0, 1, true);
        y += 9;
    }
    if (nVictim >= 0)
    {
        sprintf(buffer, "YOUR FAVOURITE VICTIM: %s (%d KILLS)", gProfile[nVictim].name, nVictimKills);
        Bstrupr(buffer);
        viewDrawText(3, buffer, 160, y, 0, 0, 1, true);
    }
}
