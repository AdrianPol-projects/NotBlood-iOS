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
#include "bot.h"
#include "chosenmodes.h"
#include "menuchosen.h"
#include "gamemenu.h"

CHOSENRULES gChosenRules;

static const char *pzModeNames[kChosenModeMax] = { "NORMAL", "GUN GAME", "KING OF THE HILL", "JUGGERNAUT", "LAST MAN STANDING", "SURVIVAL", "CAPTURE THE FLAG", "HIDE AND SEEK" };

static const char *pzBloodbathModes[] = { "NORMAL", "GUN GAME", "KING OF THE HILL", "JUGGERNAUT", "LAST MAN STANDING", "HIDE AND SEEK" };
static const int nBloodbathModes[] = { kChosenModeNormal, kChosenModeGunGame, kChosenModeKingOfTheHill, kChosenModeJuggernaut, kChosenModeLastManStanding, kChosenModeHideSeek };
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
static int nDeclaredTeam = -1;

// hide and seek
enum { kHider = 0, kSeeker = 1 };
static int nRole[kMaxPlayers];
struct DISGUISE
{
    bool bOn;
    short nPicnum;
    uint8_t nXRepeat, nYRepeat, nPal;
    int8_t nShade;
    uint16_t nCstat;
    short nAng;
};
static DISGUISE disguise[kMaxPlayers]; // what a hider looks like (a copy: the thing itself can be blown up)
static int nHideUntil, nHideEnd, nNextTaunt;

// rounds and the map vote
static PKT_STARTGAME startPacket;      // the match as the host started it (round 1)
static int nEndTime = -1;              // gLevelTime the round ended
static bool bNextSent;
static bool bVoting, bVoteMenuWanted;
static int nVoteEnd, nVoteAllAt;
static int nVoteEpisode[kChosenVoteChoices], nVoteLevel[kChosenVoteChoices];
static bool bVoteSameUserMap[kChosenVoteChoices];
static signed char nVotes[kMaxPlayers];
static char zVoteText[kChosenVoteChoices][64];

int gNetHostOnly = 0;
int gHostOnlyPref = 0;

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
    gChosenRules.nSize = ClipRange(gPacketStartGame.nChosenSize, 0, 2);
    gChosenRules.nBarrels = ClipRange(gPacketStartGame.nChosenBarrels, 0, 2);
    gChosenRules.bAkimbo = gPacketStartGame.bChosenAkimbo != 0;
    gChosenRules.nRounds = gGameOptions.nGameType == kGameTypeCoop ? 1 : ClipRange(gPacketStartGame.nChosenRounds, 1, 9);
    gChosenRules.nRound = ClipRange(gPacketStartGame.nChosenRound, 1, gChosenRules.nRounds);
    for (int i = 0; i < kMaxPlayers; i++)
        gChosenRules.nRoundWins[i] = (uint8_t)gPacketStartGame.nChosenRoundWins[i];
    gChosenRules.bVote = gPacketStartGame.bChosenVote != 0;
    gChosenRules.bHostOnly = gPacketStartGame.bChosenHostOnly != 0;
    gNetHostOnly = gChosenRules.bHostOnly;
    // the next round starts from the match as it was started (the round's end changes the flags)
    if (gChosenRules.nRound <= 1)
        startPacket = gPacketStartGame;
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
        pPlayer->pXSprite->scale = 512; // twice the size (and twice as easy to hit): everybody sees who it is
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

static void DeclareTeamWinner(int nTeam, const char *pzText)
{
    DeclareWinner(-1, pzText);
    nDeclaredTeam = nTeam;
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
// hide and seek: hiders look like a decoration of the map, seekers (blind at first) hunt them
//

static int nDisguiseSprites[64], nDisguiseCount;

static void FindDisguises(void)
{
    nDisguiseCount = 0;
    // decorations, and things like barrels, pots and crates
    static const int nStats[] = { kStatDecoration, kStatThing };
    for (int s = 0; s < ARRAY_SSIZE(nStats); s++)
    {
        for (int i = headspritestat[nStats[s]]; i >= 0 && nDisguiseCount < ARRAY_SSIZE(nDisguiseSprites); i = nextspritestat[i])
        {
            spritetype *pSprite = &sprite[i];
            // standing things of about a person's size: no flat or invisible ones, nothing huge or tiny
            if ((pSprite->cstat & (CSTAT_SPRITE_ALIGNMENT_MASK | CSTAT_SPRITE_INVISIBLE)) || pSprite->picnum <= 0 || pSprite->xrepeat < 8 || pSprite->yrepeat < 8)
                continue;
            // in pixels as the player's picture is (64 high at a size of 64)
            const int nHeight = tilesiz[pSprite->picnum].y * pSprite->yrepeat / 64, nWidth = tilesiz[pSprite->picnum].x * pSprite->xrepeat / 64;
            if (nHeight < 16 || nHeight > 96 || nWidth < 8 || nWidth > 96)
                continue;
            nDisguiseSprites[nDisguiseCount++] = i;
        }
    }
}

static void PickDisguise(int nPlayer)
{
    DISGUISE *pDisguise = &disguise[nPlayer];
    pDisguise->bOn = nDisguiseCount > 0;
    if (!pDisguise->bOn)
        return;
    const spritetype *pLook = &sprite[nDisguiseSprites[Random(nDisguiseCount)]];
    pDisguise->nPicnum = pLook->picnum;
    pDisguise->nXRepeat = pLook->xrepeat, pDisguise->nYRepeat = pLook->yrepeat;
    pDisguise->nPal = pLook->pal;
    pDisguise->nShade = pLook->shade;
    pDisguise->nCstat = pLook->cstat & (CSTAT_SPRITE_XFLIP | CSTAT_SPRITE_YFLIP);
    pDisguise->nAng = pLook->ang;
}

static bool IsHiding(int nPlayer)
{
    return gChosenRules.nMode == kChosenModeHideSeek && nRole[nPlayer] == kHider;
}

static int CountRole(int nWhich)
{
    int nCount = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (nRole[p] == nWhich)
            nCount++;
    return nCount;
}

static void StartHideSeek(void)
{
    FindDisguises();
    int nPlayers = 0, nOrder[kMaxPlayers];
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        nOrder[nPlayers++] = p, nRole[p] = kHider;
    // one seeker for every four players. The game's random numbers start the same every level, so
    // the pick comes from the match's seed (sent with the start, the same everywhere) and the round
    uint32_t nSeed = 2166136261u + gChosenRules.nRound * 16777619u;
    for (int i = 0; i < (int)sizeof(gPacketStartGame.szRandomizerSeed) && gPacketStartGame.szRandomizerSeed[i]; i++)
        nSeed = (nSeed ^ (uint8_t)gPacketStartGame.szRandomizerSeed[i]) * 16777619u;
    const int nSeekers = ClipLow((nPlayers + 2) / 4, 1);
    for (int i = 0; i < nSeekers && nPlayers > 1; i++)
    {
        nSeed = nSeed * 1103515245u + 12345u;
        const int j = i + (int)((nSeed >> 8) % (uint32_t)(nPlayers - i));
        swap(&nOrder[i], &nOrder[j]);
        nRole[nOrder[i]] = kSeeker;
    }
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        PickDisguise(p);
    // a round: 30 seconds to hide, then the time limit (or 3 minutes) to find them
    nHideUntil = kTicsPerSec * 30;
    nHideEnd = nHideUntil + (gPlayerRoundTimeLimit ? gPlayerRoundTimeLimit : kTicsPerSec * 60 * 3);
    nNextTaunt = nHideUntil + kTicsPerSec * 30;
    // hide and seek ends its own way: the game's own limits just out of reach (0 would count as reached),
    // the time limit just after its end so the clock on the screen shows about the right time
    gPlayerRoundScoreLimit = 0x3fffffff;
    gPlayerRoundTimeLimit = nHideEnd + kTicsPerSec * 2;
    LOG_F(INFO, "Hide and seek: %d seekers, %d disguises", nSeekers, nDisguiseCount);
}

static void HideSeekFrame(void)
{
    // hiders have nothing but the pitchfork (TNT and the like come with ammo pickups too)
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        if (nRole[p] != kHider || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
            continue;
        bool bArmed = false;
        for (int w = kWeaponFlare; w < kWeaponMax && !bArmed; w++)
            bArmed = pPlayer->hasWeapon[w] != 0;
        if (bArmed)
            GiveOnly(pPlayer, kWeaponPitchfork);
    }
    // the seekers wait (blind) while the others hide
    if (gLevelTime < nHideUntil)
    {
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            if (nRole[p] != kSeeker || !gPlayer[p].pSprite)
                continue;
            const int nSprite = gPlayer[p].nSprite;
            xvel[nSprite] = yvel[nSprite] = 0;
            if (zvel[nSprite] < 0)
                zvel[nSprite] = 0;
        }
        if (gLevelTime == nHideUntil - kTicsPerSec * 10)
            viewSetMessage("10 seconds to hide!", 8, MESSAGE_PRIORITY_NORMAL);
        return;
    }
    if (gLevelTime == nHideUntil)
    {
        viewSetMessage("Ready or not, here they come!", 8, MESSAGE_PRIORITY_NORMAL);
        sndStartSample("NOTBLOOD3", 64, -1, 11025);
    }
    // now and then every hider makes a noise, so they can't just stand still in a corner
    if (gLevelTime >= nNextTaunt)
    {
        nNextTaunt = gLevelTime + kTicsPerSec * 30;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (nRole[p] == kHider && gPlayer[p].pXSprite && gPlayer[p].pXSprite->health > 0)
                sfxPlay3DSound(gPlayer[p].pSprite, 4400 + Random(10), 2, 0);
    }
    const int nHiders = CountRole(kHider);
    if (!nHiders)
    {
        DeclareWinner(-1, "The seekers found everybody!");
        return;
    }
    if (gLevelTime >= nHideEnd)
    {
        // the hiders still hidden win, and score for it
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (nRole[p] == kHider)
                gPlayer[p].fragCount += 3;
        DeclareWinner(-1, nHiders == 1 ? "One hider was never found!" : "The hiders win!");
    }
}

// a hider that's found joins the seekers
static void HiderFound(int nPlayer)
{
    if (nRole[nPlayer] != kHider)
        return;
    nRole[nPlayer] = kSeeker;
    char buffer[64];
    sprintf(buffer, "\r%s\r was found!", gProfile[nPlayer].name);
    viewSetMessageColor(buffer, 0, MESSAGE_PRIORITY_NORMAL, gColorMsg && !VanillaMode() ? playerColorPalMessage(gPlayer[nPlayer].teamId) : 0);
}

bool modesIsEnemy(PLAYER *pMe, PLAYER *pOther)
{
    if (gChosenRules.nMode != kChosenModeHideSeek)
        return true;
    // hiders hide, seekers look for hiders: up close, or one that gives itself away by moving
    if (nRole[pMe->nPlayer] != kSeeker || nRole[pOther->nPlayer] != kHider || gLevelTime < nHideUntil)
        return false;
    spritetype *pMeSprite = pMe->pSprite, *pOtherSprite = pOther->pSprite;
    if (!pMeSprite || !pOtherSprite)
        return false;
    const int nSpeed = approxDist(xvel[pOtherSprite->index], yvel[pOtherSprite->index]);
    return approxDist(pMeSprite->x - pOtherSprite->x, pMeSprite->y - pOtherSprite->y) < 1800 || nSpeed > 0x8000 || pOther->input.buttonFlags.shoot;
}

bool modesDrawPlayer(tspritetype *pTSprite, PLAYER *pPlayer)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return false;
    const int nPlayer = pPlayer->nPlayer;
    if (IsHiding(nPlayer) && pPlayer->pXSprite && pPlayer->pXSprite->health > 0 && disguise[nPlayer].bOn)
    {
        const DISGUISE *pLook = &disguise[nPlayer];
        pTSprite->picnum = pLook->nPicnum;
        pTSprite->xrepeat = pLook->nXRepeat;
        pTSprite->yrepeat = pLook->nYRepeat;
        pTSprite->pal = pLook->nPal;
        pTSprite->shade = pLook->nShade;
        pTSprite->cstat = (pTSprite->cstat & ~(CSTAT_SPRITE_XFLIP | CSTAT_SPRITE_YFLIP | CSTAT_SPRITE_TRANSLUCENT | CSTAT_SPRITE_TRANSLUCENT_INVERT)) | pLook->nCstat;
        pTSprite->ang = pLook->nAng;
        if (pPlayer == gView && gViewPos != VIEWPOS_0)
            pTSprite->cstat |= CSTAT_SPRITE_TRANSLUCENT; // seen from behind: see through it
        return true;
    }
    if (modesJuggernaut() == nPlayer && pPlayer->pXSprite && pPlayer->pXSprite->health > 0)
    {
        // red, glowing and big: the juggernaut stands out from far away
        pTSprite->pal = kFlagRedPal;
        pTSprite->shade = (int8_t)ClipRange(-24 + mulscale30(Sin(((int)totalclock * 16) & kAngMask), 24), -63, 0);
    }
    return false;
}

bool modesHideName(PLAYER *pPlayer)
{
    return IsHiding(pPlayer->nPlayer) && gView && gView->nPlayer != pPlayer->nPlayer;
}

bool modesAkimboPitchfork(PLAYER *pPlayer)
{
    return gChosenRules.bAkimbo && gGameOptions.nGameType != kGameTypeSinglePlayer && powerupCheck(pPlayer, kPwUpTwoGuns);
}

//
// TNT barrels everywhere: around the items, where everybody goes
//

static void PlaceBarrels(void)
{
    const int nWant = gChosenRules.nBarrels == 2 ? 40 : 16;
    int nItems = 0;
    for (int i = headspritestat[kStatItem]; i >= 0; i = nextspritestat[i])
        nItems++;
    if (!nItems)
        return;
    int nPlaced = 0;
    for (int nTry = 0; nTry < nWant * 4 && nPlaced < nWant; nTry++)
    {
        int i = headspritestat[kStatItem];
        for (int n = Random(nItems); n > 0 && i >= 0; n--)
            i = nextspritestat[i];
        if (i < 0)
            continue;
        // a little way off the item, in the open
        const int nAng = Random(2048), nDist = 256 + Random(512);
        const int x = sprite[i].x + mulscale30(Cos(nAng), nDist), y = sprite[i].y + mulscale30(Sin(nAng), nDist);
        int16_t nSector = sprite[i].sectnum;
        updatesector(x, y, &nSector);
        if (nSector < 0 || !cansee(sprite[i].x, sprite[i].y, sprite[i].z - (16<<8), sprite[i].sectnum, x, y, getflorzofslope(nSector, x, y) - (16<<8), nSector))
            continue;
        if (sector[nSector].extra > 0 && (xsector[sector[nSector].extra].Underwater || xsector[sector[nSector].extra].damageType))
            continue;
        // never right where somebody starts
        bool bNearStart = false;
        for (int p = 0; p < kMaxPlayers && !bNearStart; p++)
            bNearStart = approxDist(gStartZone[p].x - x, gStartZone[p].y - y) < 600;
        if (bNearStart)
            continue;
        if (actSpawnThing(nSector, x, y, getflorzofslope(nSector, x, y), kThingTNTBarrel))
            nPlaced++;
    }
    LOG_F(INFO, "TNT barrels everywhere: %d placed", nPlaced);
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
    nDeclaredTeam = -1;
    nEndTime = -1;
    bNextSent = bVoting = bVoteMenuWanted = false;
    Bmemset(nVotes, -1, sizeof(nVotes));
    Bmemset(nRole, 0, sizeof(nRole));
    Bmemset(disguise, 0, sizeof(disguise));
    menuCloseMapVote();
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
    if (gChosenRules.nMode == kChosenModeHideSeek)
        StartHideSeek();
    if (gChosenRules.nBarrels)
        PlaceBarrels();
    if (gChosenRules.nRounds > 1)
    {
        char buffer[64];
        sprintf(buffer, gChosenRules.nRound == gChosenRules.nRounds ? "Final round!" : "Round %d of %d", gChosenRules.nRound, gChosenRules.nRounds);
        viewSetMessage(buffer, 8, MESSAGE_PRIORITY_NORMAL);
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
    if (gChosenRules.nSize) // tiny or big players (and their hit boxes)
        pPlayer->pXSprite->scale = gChosenRules.nSize == 1 ? 128 : 512;
    int nOnly = 0;
    if (gChosenRules.nMode == kChosenModeHideSeek)
    {
        if (nRole[pPlayer->nPlayer] == kSeeker)
        {
            GiveOnly(pPlayer, kWeaponShotgun);
            GiveWeapon(pPlayer, kWeaponTommy, 100);
        }
        else
        {
            GiveOnly(pPlayer, kWeaponPitchfork); // a hider can't hurt anybody anyway
            PickDisguise(pPlayer->nPlayer);
        }
    }
    else if (gChosenRules.nMode == kChosenModeGunGame)
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
    case kChosenModeHideSeek:
        if (nKiller == nVictim)
            pKiller->fragCount++; // no minus for it: the hider just joins the seekers
        break;
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
    if (gChosenRules.nMode == kChosenModeHideSeek)
        HiderFound(nPlayer);
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

//
// rounds: when one is over the host starts the next on the same map; after the last the
// players can vote for the next map (the host starts that one too)
//

static bool IsHost(void)
{
    return myconnectindex == connecthead;
}

static bool IsBotPlayer(int nPlayer)
{
    return nPlayer != myconnectindex && botIsBot(nPlayer);
}

// the round's winner: a player (bloodbath) or a team (teams), -1 for nobody
static int RoundWinner(void)
{
    if (gGameOptions.nGameType == kGameTypeTeams)
    {
        if (nDeclaredTeam >= 0)
            return nDeclaredTeam;
        return gPlayerScores[0] > gPlayerScores[1] ? 0 : gPlayerScores[1] > gPlayerScores[0] ? 1 : -1;
    }
    if (nDeclaredWinner >= 0)
        return nDeclaredWinner;
    int nBest = -1, nBestScore = INT_MIN, nTied = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (gPlayer[p].fragCount > nBestScore)
            nBest = p, nBestScore = gPlayer[p].fragCount, nTied = 0;
        else if (gPlayer[p].fragCount == nBestScore)
            nTied++;
    }
    return nTied ? -1 : nBest;
}

static const char *WinnerName(int nWho)
{
    if (gGameOptions.nGameType == kGameTypeTeams)
        return nWho ? "Red Team" : "Blue Team";
    return gProfile[nWho].name;
}

static void HostStart(int nRound, int nVote)
{
    if (!IsHost() || bNextSent)
        return;
    bNextSent = true;
    PKT_STARTGAME packet = startPacket;
    packet.nChosenRound = nRound;
    for (int i = 0; i < kMaxPlayers; i++)
        packet.nChosenRoundWins[i] = nRound > 1 ? gChosenRules.nRoundWins[i] : 0;
    if (nVote >= 0 && !bVoteSameUserMap[nVote])
    {
        packet.episodeId = nVoteEpisode[nVote];
        packet.levelId = nVoteLevel[nVote];
        packet.userMap = 0;
        packet.userMapName[0] = 0;
    }
    if (nRound <= 1)
        startPacket = packet;
    gPacketStartGame = packet;
    LOG_F(INFO, "Starting round %d (episode %d, level %d)", nRound, packet.episodeId + 1, packet.levelId + 1);
    if (numplayers > 1)
    {
        botNetSendRoster();
        netBroadcastNewGame();
    }
    gStartNewGame = 1;
}

static void RefreshVoteText(void)
{
    for (int c = 0; c < kChosenVoteChoices; c++)
    {
        int nCount = 0;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            nCount += nVotes[p] == c;
        const char *pzName;
        if (bVoteSameUserMap[c])
            pzName = gPacketStartGame.userMapName;
        else
        {
            pzName = gEpisodeInfo[nVoteEpisode[c]].levelsInfo[nVoteLevel[c]].Title;
            if (!pzName[0])
                pzName = gEpisodeInfo[nVoteEpisode[c]].levelsInfo[nVoteLevel[c]].Filename;
        }
        Bsnprintf(zVoteText[c], sizeof(zVoteText[c]), "%s%.28s (%d)", c == 0 ? "AGAIN: " : "", pzName, nCount);
        Bstrupr(zVoteText[c]);
    }
}

static void StartVote(void)
{
    // the same map again, and two others (every machine picks the same: the game's random numbers)
    const bool bUserMap = gGameOptions.zLevelName[0] && gPacketStartGame.userMap;
    bVoteSameUserMap[0] = bUserMap;
    nVoteEpisode[0] = gGameOptions.nEpisode, nVoteLevel[0] = gGameOptions.nLevel;
    int nLevels = 0;
    for (int e = 0; e < gEpisodeCount; e++)
        nLevels += gEpisodeInfo[e].nLevels;
    for (int c = 1; c < kChosenVoteChoices; c++)
    {
        bVoteSameUserMap[c] = false;
        nVoteEpisode[c] = nVoteEpisode[0], nVoteLevel[c] = nVoteLevel[0];
        for (int nTry = 0; nTry < 20 && nLevels > 1; nTry++)
        {
            int n = Random(nLevels), e = 0;
            while (e < gEpisodeCount - 1 && n >= gEpisodeInfo[e].nLevels)
                n -= gEpisodeInfo[e].nLevels, e++;
            bool bTaken = !bUserMap && e == nVoteEpisode[0] && n == nVoteLevel[0];
            for (int d = 1; d < c; d++)
                bTaken |= e == nVoteEpisode[d] && n == nVoteLevel[d];
            if (bTaken)
                continue;
            nVoteEpisode[c] = e, nVoteLevel[c] = n;
            break;
        }
    }
    Bmemset(nVotes, -1, sizeof(nVotes));
    bVoting = true;
    bVoteMenuWanted = true;
    nVoteEnd = gLevelTime + kTicsPerSec * 20;
    nVoteAllAt = 0;
    viewSetMessage("Vote for the next map!", 8, MESSAGE_PRIORITY_NORMAL);
    // the host votes for its bots
    if (IsHost())
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (IsBotPlayer(p))
            {
                nVotes[p] = (signed char)((uint32_t)rand() % kChosenVoteChoices);
                netBroadcastVote(p, nVotes[p]);
            }
    RefreshVoteText();
}

bool modesVoting(void)
{
    return bVoting;
}

const char *modesVoteText(int nChoice)
{
    return zVoteText[ClipRange(nChoice, 0, kChosenVoteChoices - 1)];
}

int modesMyVote(void)
{
    return nVotes[myconnectindex];
}

void modesVote(int nChoice)
{
    if (!bVoting || nChoice < 0 || nChoice >= kChosenVoteChoices)
        return;
    nVotes[myconnectindex] = nChoice;
    netBroadcastVote(myconnectindex, nChoice);
    RefreshVoteText();
}

void modesReceiveVote(int nPlayer, int nChoice)
{
    if (nPlayer < 0 || nPlayer >= kMaxPlayers || nChoice < 0 || nChoice >= kChosenVoteChoices)
        return;
    nVotes[nPlayer] = nChoice;
    RefreshVoteText();
}

static int VoteWinner(void)
{
    int nCount[kChosenVoteChoices] = {};
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (nVotes[p] >= 0)
            nCount[(int)nVotes[p]]++;
    int nBest = 0;
    for (int c = 1; c < kChosenVoteChoices; c++)
        if (nCount[c] > nCount[nBest])
            nBest = c;
    return nBest;
}

static void RoundOver(void)
{
    if (nEndTime < 0)
    {
        nEndTime = gLevelTime;
        const int nWinner = RoundWinner();
        if (nWinner >= 0 && gChosenRules.nRoundWins[nWinner] < 255)
            gChosenRules.nRoundWins[nWinner]++;
        if (gChosenRules.nRounds > 1 && gChosenRules.nRound >= gChosenRules.nRounds)
        {
            // the match: the most rounds won
            int nBest = -1, nTied = 0;
            const int nCount = gGameOptions.nGameType == kGameTypeTeams ? 2 : kMaxPlayers;
            for (int i = 0; i < nCount; i++)
            {
                if (gGameOptions.nGameType != kGameTypeTeams && gPlayer[i].pSprite == NULL)
                    continue;
                if (nBest < 0 || gChosenRules.nRoundWins[i] > gChosenRules.nRoundWins[nBest])
                    nBest = i, nTied = 0;
                else if (gChosenRules.nRoundWins[i] == gChosenRules.nRoundWins[nBest])
                    nTied++;
            }
            char buffer[96];
            if (nBest >= 0 && !nTied)
                Bsnprintf(buffer, sizeof(buffer), "%s wins the match (%d rounds)!", WinnerName(nBest), gChosenRules.nRoundWins[nBest]);
            else
                Bsnprintf(buffer, sizeof(buffer), "The match is a draw!");
            viewSetMessage(buffer, 8, MESSAGE_PRIORITY_NORMAL);
        }
    }
    const bool bMoreRounds = gChosenRules.nRound < gChosenRules.nRounds;
    if (bMoreRounds)
    {
        if (gLevelTime - nEndTime >= kTicsPerSec * 8)
            HostStart(gChosenRules.nRound + 1, -1);
        return;
    }
    if (!gChosenRules.bVote)
        return;
    if (!bVoting && gLevelTime - nEndTime >= kTicsPerSec * 5 && !bNextSent)
        StartVote();
    if (!bVoting)
        return;
    // everybody has voted: a moment to see it, then go
    bool bAll = true;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        bAll &= nVotes[p] >= 0;
    if (bAll && !nVoteAllAt)
        nVoteAllAt = gLevelTime;
    if (gLevelTime >= nVoteEnd || (nVoteAllAt && gLevelTime - nVoteAllAt >= kTicsPerSec * 2))
        HostStart(1, VoteWinner());
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
    {
        RoundOver();
        return;
    }
    // guns akimbo for everybody
    if (gChosenRules.bAkimbo)
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            PLAYER *pPlayer = &gPlayer[p];
            if (pPlayer->pXSprite && pPlayer->pXSprite->health > 0)
                pPlayer->pwUpTime[kPwUpTwoGuns] = ClipLow(pPlayer->pwUpTime[kPwUpTwoGuns], kTicsPerSec * 4);
        }
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
                DeclareTeamWinner(bIn[0] ? 0 : 1, bIn[0] ? "Blue Team is the last team standing!" : "Red Team is the last team standing!");
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
        {
            DeclareWinner(gGameOptions.nGameType == kGameTypeTeams ? -1 : nOn, gGameOptions.nGameType == kGameTypeTeams ? (nTeam ? "Red Team holds the hill!" : "Blue Team holds the hill!") : "is the king of the hill");
            if (gGameOptions.nGameType == kGameTypeTeams)
                nDeclaredTeam = nTeam & 1;
        }
        break;
    }
    case kChosenModeJuggernaut:
        if (nJuggernaut >= 0)
        {
            PLAYER *pJuggernaut = &gPlayer[nJuggernaut];
            if (pJuggernaut->pXSprite && pJuggernaut->pXSprite->health > 0)
            {
                pJuggernaut->pwUpTime[kPwUpTwoGuns] = ClipLow(pJuggernaut->pwUpTime[kPwUpTwoGuns], kTicsPerSec * 4);
                pJuggernaut->pXSprite->scale = 512;
            }
        }
        break;
    case kChosenModeHideSeek:
        HideSeekFrame();
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
    if (gGameOptions.nGameType == kGameTypeSinglePlayer)
        return true;
    if (gChosenRules.nMode == kChosenModeGunGame || IsHiding(pPlayer->nPlayer)) // hiders have no use for guns
        return false;
    int nOnly;
    if (OnlyWeapon(&nOnly))
        return nWeapon == nOnly;
    return true;
}

int modesDamage(int nSource, spritetype *pTarget, int nDamage)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer || nSource < 0 || nDamage <= 0)
        return nDamage;
    if (!IsPlayerSprite(pTarget) || !IsPlayerSprite(&sprite[nSource]) || &sprite[nSource] == pTarget)
        return nDamage;
    const int nFrom = sprite[nSource].type - kDudePlayer1, nTo = pTarget->type - kDudePlayer1;
    if (gChosenRules.nMode == kChosenModeHideSeek)
    {
        // only seekers hurt, only hiders, and not while they're still blind
        if (nRole[nFrom] != kSeeker || nRole[nTo] != kHider || gLevelTime < nHideUntil)
            return 0;
    }
    if (gChosenRules.bOneHit)
        return 2000<<4; // one hit and it's over
    if (gChosenRules.nMode == kChosenModeJuggernaut && nFrom == nJuggernaut)
        return nDamage * 3 / 2; // the juggernaut hits harder
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

static void DrawRoundsAndVote(void);

void modesDrawHud(void)
{
    if (gGameOptions.nGameType == kGameTypeSinglePlayer || !gView)
        return;
    DrawRoundsAndVote();
    if (gChosenRules.nMode == kChosenModeNormal)
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
        else if (gPlayer[nJuggernaut].pSprite)
        {
            // how far, like the hill
            const spritetype *pJuggernaut = gPlayer[nJuggernaut].pSprite;
            Bsnprintf(buffer, sizeof(buffer), "JUGGERNAUT: %.15s, %d AWAY", gProfile[nJuggernaut].name, approxDist(gView->pSprite->x - pJuggernaut->x, gView->pSprite->y - pJuggernaut->y) / 16);
            Bstrupr(buffer);
        }
        break;
    case kChosenModeHideSeek:
    {
        const int nHiders = CountRole(kHider);
        if (gLevelTime < nHideUntil)
        {
            const int nLeft = (nHideUntil - gLevelTime) / kTicsPerSec + 1;
            if (nRole[nView] == kSeeker)
            {
                // blindfolded: nothing to see but the countdown
                videoClearViewableArea(0);
                sprintf(buffer, "THEY ARE HIDING... %d", nLeft);
                viewDrawText(1, buffer, 160, 90, -128, 0, 1, true);
                sprintf(buffer, "YOU ARE A SEEKER");
            }
            else
                sprintf(buffer, "HIDE! SEEKERS COME IN %d", nLeft);
        }
        else
        {
            const int nLeft = ClipLow((nHideEnd - gLevelTime) / kTicsPerSec, 0);
            sprintf(buffer, "%s - %d HIDING - %d:%02d", nRole[nView] == kSeeker ? "SEEK" : "HIDE", nHiders, nLeft / 60, nLeft % 60);
        }
        break;
    }
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

static void DrawRoundsAndVote(void)
{
    if (bVoteMenuWanted)
    {
        bVoteMenuWanted = false;
        if (!gGameMenuMgr.m_bActive)
            menuOpenMapVote();
    }
    if (gGameMenuMgr.m_bActive) // the vote menu (or another) shows it all
        return;
    char buffer[96];
    if (gChosenRules.nRounds > 1)
    {
        sprintf(buffer, "ROUND %d OF %d", gChosenRules.nRound, gChosenRules.nRounds);
        const int nView = gView ? gView->nPlayer : myconnectindex;
        const int nMine = gGameOptions.nGameType == kGameTypeTeams ? gPlayer[nView].teamId & 1 : nView;
        sprintf(buffer + strlen(buffer), " - %s %d", gGameOptions.nGameType == kGameTypeTeams ? "TEAM WINS" : "YOUR WINS", gChosenRules.nRoundWins[nMine]);
        viewDrawText(3, buffer, 160, 31, -128, 0, 1, true);
    }
    if (bVoting)
    {
        const int nLeft = ClipLow((nVoteEnd - gLevelTime) / kTicsPerSec + 1, 0);
        sprintf(buffer, nVotes[myconnectindex] >= 0 ? "NEXT MAP VOTE (%d): YOU VOTED" : "NEXT MAP VOTE (%d)", nLeft);
        // closed the vote without voting: it comes back
        static int nClosedAt;
        if (nVotes[myconnectindex] >= 0)
            nClosedAt = 0;
        else if (!nClosedAt)
            nClosedAt = (int)totalclock;
        else if ((int)totalclock - nClosedAt > kTicRate * 3)
            nClosedAt = 0, bVoteMenuWanted = true;
        viewDrawText(3, buffer, 160, 40, -128, 0, 1, true);
        for (int c = 0; c < kChosenVoteChoices; c++)
            viewDrawText(3, zVoteText[c], 160, 50 + c * 9, nVotes[myconnectindex] == c ? -128 : 0, nVotes[myconnectindex] == c ? kFlagRedPal : 0, 1, true);
    }
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

//
// only the host starts new games
//

bool modesMayStartGame(void)
{
    return numplayers < 2 || IsHost() || !gNetHostOnly;
}

void modesSendHostOnly(void)
{
    if (!IsHost())
        return;
    gNetHostOnly = gHostOnlyPref;
    netBroadcastHostOnly(gNetHostOnly);
}

void modesReceiveHostOnly(int nValue)
{
    gNetHostOnly = nValue != 0;
    if (gNetHostOnly)
        menuLeaveNetStart(); // a player in the game setup: only the host can start it now
}
