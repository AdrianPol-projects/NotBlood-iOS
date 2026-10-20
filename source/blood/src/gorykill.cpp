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

// Blood Extra Crispy as a mod, and its Gory Kills (see gorykill.h).
//
// Chosen in the MODS menu, the mod's folder gives the game its art (tiles001-030.art: its title,
// menu background, sprites and new tiles), its SOUNDS.RFF and SURFACE.DAT. Its maps, BLOOD.INI and
// scripts aren't used: they are made for Nightdive's KEX engine (enemy types and texts only it has).
//
// The mod (for Nightdive's Refreshed Supply) scripts its kills in BEC_Players.txt: USE near a
// suffering enemy removes it, plays a first-person QAV over the screen and runs a timeline (a timer
// counting down by 2 a game frame) of sounds, a heal, and a body kicked away with a gory SEQ that
// stays as a corpse. That timeline is in the table below, timer values and all. The QAVs, SEQs, gore
// sounds and tiles (tiles018-030.art) come from the player's copy of the mod: EXTRAC.RFF is opened
// next to the game's own files and only what the kills use is taken from it.

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "blood.h"
#include "config.h"
#include "db.h"
#include "fx.h"
#include "gib.h"
#include "globals.h"
#include "gorykill.h"
#include "levels.h"
#include "player.h"
#include "qav.h"
#include "resource.h"
#include "seq.h"
#include "sfx.h"
#include "sound.h"
#include "mmulti.h"
#include "osd.h"
#include "trig.h"

namespace
{

enum
{
    kEvSound = 0,  // a sound (plus a random 0..rnd-1)
    kEvLaugh,      // Caleb heals 10 (up to 200) and laughs now and then; blood on the screen
    kEvBody,       // the body flies away from Caleb with the kill's SEQ and stays as a corpse
    kEvKick,       // the quick kick lands: three short hits that push hard (the mod's BV_QUICKKICK)
    kEvEnd = -1,
};

struct GKEVENT
{
    short nTime;  // the mod's timer: starts at the kill's length, 2 less a game frame
    signed char nType;
    signed char nRand;
    int nSound;
};

struct GKDEF
{
    short nDude[4];  // enemy types (0: none)
    short nHealth;   // the enemy's health (x16, as in xsprite) must be at most this; -1: any
    short nDist;     // how near it must be (Build units)
    short nLength;   // the timer's start
    short nQav, nQavCloak;  // the QAV, and the one for an invisible Caleb (shadow cloak)
    int nBodySeq;    // the body's SEQ (0: no body)
    GKEVENT const *pEvents;
    bool bKick;      // the quick kick: Caleb can move meanwhile, and gets hurt
};

// sounds: original Blood's, and the mod's gore (in EXTRAC.RFF)
enum
{
    kSfxCultDie1 = 1018, kSfxPunt = 357, kSfxAZomPain = 1106, kSfxAZomDie1 = 1107, kSfxAZomSwip = 1101,
    kSfxFZomPain = 1202, kSfxFZomDie1 = 1204, kSfxGargPain = 1402, kSfxGargDie1 = 1403, kSfxGillPain = 1702,
    kSfxGillDie1 = 1703, kSfxIDonHme = 7002, kSfxIScream1 = 7007, kSfxPFlesh = 503, kSfxCerbPain = 1302,
    kSfxCerbDie1 = 1303, kSfxPhanSpot = 1600, kSfxPhanDie1 = 1603, kSfxPlay1109 = 734,
    kSfxGorBone = 22161, kSfxGorHead = 22162, kSfxGorRan1 = 22164, kSfxGorRip1 = 22167, kSfxDemMel1 = 22112,
};

#define SND(t, s) { t, kEvSound, 0, s }
#define SNDR(t, s, r) { t, kEvSound, r, s }
#define LAUGH(t) { t, kEvLaugh, 0, 0 }
#define BODY(t) { t, kEvBody, 0, 0 }
#define KICK(t) { t, kEvKick, 0, 0 }
#define END { 0, kEvEnd, 0, 0 }

// cultists (the four have the same timeline; tesla and TNT add a punt at the start)
GKEVENT const gEvCultist[] = {
    SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvCultistTesla[] = {
    SND(114, kSfxDemMel1), SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), SND(110, kSfxPunt), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvCultistTNT[] = {
    SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), SND(110, kSfxPunt), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvAxeZombie[] = {
    SND(100, kSfxAZomPain), SND(90, kSfxAZomPain), SNDR(80, kSfxAZomDie1, 2), SND(80, kSfxGorRip1), LAUGH(80),
    SND(26, kSfxDemMel1), SND(20, kSfxPunt), BODY(10), END };
GKEVENT const gEvFatZombie[] = {
    SND(130, kSfxAZomSwip), SND(124, kSfxFZomPain), SND(124, kSfxGorBone), SND(100, kSfxAZomSwip),
    SNDR(94, kSfxFZomDie1, 2), SND(94, kSfxGorRip1), SND(60, kSfxAZomSwip), SND(54, kSfxPunt), SND(54, kSfxGorHead),
    LAUGH(54), SND(20, kSfxDemMel1), SND(14, kSfxPunt), BODY(4), END };
GKEVENT const gEvGargoyle[] = {
    SND(120, kSfxGorBone), SND(110, kSfxAZomSwip), SND(100, kSfxPunt), SND(100, kSfxGargPain), SND(100, kSfxGorHead),
    SND(84, kSfxAZomSwip), SND(74, kSfxPunt), SND(74, kSfxGargPain), SND(74, kSfxGorHead), SND(60, kSfxAZomSwip),
    SND(50, kSfxPunt), SND(50, kSfxGargPain), SND(50, kSfxGorHead), SND(30, kSfxDemMel1), SND(24, kSfxPunt),
    SNDR(24, kSfxGargDie1, 2), SNDR(24, kSfxGorRan1, 2), LAUGH(24), BODY(14), END };
GKEVENT const gEvGillBeast[] = {
    SND(80, kSfxGillPain), SND(64, kSfxGillPain), SNDR(52, kSfxGillDie1, 2), SNDR(52, kSfxGorRan1, 2), LAUGH(52),
    SND(30, kSfxDemMel1), SND(24, kSfxPunt), BODY(14), END };
GKEVENT const gEvInnocent[] = {
    SND(100, kSfxIDonHme), SND(90, kSfxGorBone), SNDR(80, kSfxIScream1, 2), SNDR(70, kSfxIScream1, 2),
    SNDR(60, kSfxIScream1, 2), SNDR(50, kSfxIScream1, 2), SND(50, kSfxGorRip1), LAUGH(50),
    SND(30, kSfxDemMel1), SND(24, kSfxPunt), BODY(14), END };
GKEVENT const gEvHellHound[] = {
    SND(100, kSfxAZomSwip), SND(90, kSfxPFlesh), SND(90, kSfxCerbPain), LAUGH(90), SND(80, kSfxAZomSwip),
    SND(70, kSfxPFlesh), SND(70, kSfxCerbPain), SND(60, kSfxAZomSwip), SND(50, kSfxPFlesh), SNDR(50, kSfxCerbDie1, 2),
    SND(40, kSfxDemMel1), SND(34, kSfxPunt), BODY(24), END };
GKEVENT const gEvPhantasm[] = {
    SND(120, kSfxPhanSpot), SND(100, kSfxPhanSpot), SND(56, kSfxAZomSwip), SND(50, kSfxGorHead),
    SNDR(50, kSfxPhanDie1, 2), LAUGH(50), END };

// the mod's distances: 128 (most) and 85 of its units, 16 Build units each
enum { kNear = 2048, kNearer = 1360 };

// USE near an enemy that isn't hurt enough for a Gory Kill: Caleb kicks it back (QAV_QKICK), at most
// once a second (the mod's KickDelay)
GKEVENT const gEvKick[] = { SND(14, kSfxDemMel1), KICK(8), END };
GKDEF const gKick = { { 0 }, -1, kNearer, 24, 622, 669, 0, gEvKick, true };
enum { kKickDelay = 60 };

// always kicked (no Gory Kill for them): burning ones and beasts, as in the mod's list
short const gAlwaysKicked[] = { kDudeBurningInnocent, kDudeBurningCultist, kDudeBurningZombieAxe, kDudeBurningZombieButcher,
                                kDudeBurningBeast, kDudeBeast, kDudeCultistBeast };

GKDEF const gKills[] = {
    { { kDudeCultistTommy, kDudeCultistTommyProne }, 200, kNear, 140, 626, 689, 15034, gEvCultist },
    { { kDudeCultistShotgun, kDudeCultistShotgunProne }, 200, kNear, 140, 627, 690, 15035, gEvCultist },
    { { kDudeCultistTesla }, 200, kNear, 140, 628, 691, 15036, gEvCultistTesla },
    { { kDudeCultistTNT }, 200, kNear, 140, 629, 692, 15037, gEvCultistTNT },
    { { kDudeZombieAxeNormal, kDudeZombieAxeLaying }, 200, kNearer, 130, 630, 688, 4359, gEvAxeZombie },
    { { kDudeZombieButcher }, 200, kNearer, 140, 631, 687, 13816, gEvFatZombie },
    { { kDudeGargoyleFlesh }, 200, kNearer, 140, 632, 686, 13821, gEvGargoyle },
    { { kDudeGillBeast }, 200, kNearer, 140, 633, 685, 7176, gEvGillBeast },
    { { kDudeInnocent }, -1, kNearer, 140, 640, 683, 13830, gEvInnocent },
    { { kDudeHellHound }, 200, kNear, 140, 662, 682, 5633, gEvHellHound },
    { { kDudePhantasm }, 200, kNearer, 140, 663, 681, 0, gEvPhantasm },
};

enum { kScreenBloodTime = 90 };

struct GKSTATE
{
    GKDEF const *pDef;  // the kill playing, NULL for none
    QAV *pQav;
    int nTimer;
    int nScreenBlood, nScreenBloodTile;
    int nKickDelay;
    int nKickTarget;
};

Resource gBecRes;  // the mod's EXTRAC.RFF
bool bAvailable;
char szDir[BMAX_PATH];  // the mod's folder (from the game folder), "" when it isn't chosen
GKSTATE gState[kMaxPlayers];

bool fileExists(const char *pzPath)
{
    int h = kopen4load(pzPath, 0);
    if (h < 0)
        return false;
    kclose(h);
    return true;
}

// copies one file of the mod's EXTRAC.RFF into a game resource, unless the game has one already
bool copyResource(Resource &to, unsigned int nId, const char *pzType, const char *pzName)
{
    DICTNODE *hFrom = nId != (unsigned int)-1 ? gBecRes.Lookup(nId, pzType) : gBecRes.Lookup(pzName, pzType);
    if (!hFrom)
        return false;
    int const nSize = gBecRes.Size(hFrom);
    char *pData = (char *)Xmalloc(nSize);
    gBecRes.Read(hFrom, pData);  // decrypted
    char name[16];
    if (nId != (unsigned int)-1)
        Bsnprintf(name, sizeof(name), "BEC%u", nId);  // the mod reuses names for different ids
    else
        Bstrncpyz(name, pzName, sizeof(name));
    to.AddFromBuffer(name, pzType, pData, nSize, nId != (unsigned int)-1 ? (int)nId : -1, 0);
    Xfree(pData);
    return true;
}

// a sound the kills play: from the game if it has it, else the mod's (with its sample)
void takeSound(int nId)
{
    if (gSoundRes.Lookup(nId, "SFX"))
        return;
    DICTNODE *hSfx = gBecRes.Lookup((unsigned int)nId, "SFX");
    if (!hSfx || gBecRes.Size(hSfx) < (int)sizeof(SFX))
        return;
    SFX sfx;
    gBecRes.Read(hSfx, &sfx);
    sfx.rawName[8] = '\0';
    if (!gSoundRes.Lookup(sfx.rawName, "RAW") && !copyResource(gSoundRes, (unsigned int)-1, "RAW", sfx.rawName))
        return;
    copyResource(gSoundRes, nId, "SFX", NULL);
}

bool singlePlayer(void)
{
    return gGameOptions.nGameType == kGameTypeSinglePlayer && numplayers == 1;
}

GKDEF const *killFor(int nType)
{
    for (auto const &k : gKills)
        for (short nDude : k.nDude)
            if (nDude && nDude == nType)
                return &k;
    return NULL;
}

// the body stays where it lands, a corpse like any other (the mod's CALLBACK_DUDE_TO_GIB only makes it
// one that gibs when shot, as Blood's corpses do)
void throwBody(PLAYER *pPlayer, GKDEF const *pDef)
{
    if (!pDef->nBodySeq || !gSysRes.Lookup(pDef->nBodySeq, "SEQ"))
        return;
    spritetype *pThing = actFireThing(pPlayer->pSprite, 0, 0, -7500, kThingBloodChunks, 1200000);
    if (!pThing || pThing->extra <= 0)
        return;
    seqSpawn(pDef->nBodySeq, 3, pThing->extra, -1);
    for (int i = 0; i < 3; i++)
        fxSpawnBlood(pThing, 0);
}

// the kick lands on the enemy it was aimed at: the mod's three BV_QUICKKICK hits, each 4 damage (pummel)
// and a hard push (its thrust 250.33; the pitchfork's 42.67 is Blood's 174762), if it's still in reach
void kick(PLAYER *pPlayer, int nTarget)
{
    if (nTarget < 0 || nTarget >= kMaxSprites)
        return;
    spritetype *pSprite = pPlayer->pSprite;
    spritetype *pDude = &sprite[nTarget];
    if (pDude->statnum != kStatDude || pDude->extra <= 0 || xsprite[pDude->extra].health <= 0)
        return;
    int const dx = pDude->x - pSprite->x, dy = pDude->y - pSprite->y;
    if (approxDist(dx, dy) > kNearer + (pDude->clipdist << 2))  // it got away meanwhile
        return;
    int const nAngle = getangle(dx, dy);
    int const nMass = getDudeInfo(pDude->type)->mass;
    sfxPlay3DSound(pDude, kSfxPunt, -1, 0);
    for (int i = 0; i < 3 && pDude->extra > 0 && xsprite[pDude->extra].health > 0; i++)
    {
        actDamageSprite(pSprite->index, pDude, kDamageFall, 4 << 4);
        if (nMass > 0)
        {
            int const nPush = divscale8(1025365, nMass);
            xvel[nTarget] += mulscale16(Cos(nAngle) >> 16, nPush);
            yvel[nTarget] += mulscale16(Sin(nAngle) >> 16, nPush);
        }
    }
}

void runEvent(PLAYER *pPlayer, GKSTATE *pState, GKEVENT const *pEv)
{
    spritetype *pSprite = pPlayer->pSprite;
    switch (pEv->nType)
    {
    case kEvSound:
        sfxPlay3DSound(pSprite, pEv->nSound + (pEv->nRand > 1 ? (int)Random(pEv->nRand) : 0), -1, 0);
        break;
    case kEvLaugh:
        actHealDude(pPlayer->pXSprite, 10, 200, 1);
        if (Random(100) < 50)
            sfxPlay3DSound(pSprite, kSfxPlay1109 + Random(7), -1, 0);
        if (pState->nScreenBlood <= 0)
        {
            pState->nScreenBlood = kScreenBloodTime;
            pState->nScreenBloodTile = 5532 + Random(3);
        }
        break;
    case kEvBody:
        throwBody(pPlayer, pState->pDef);
        break;
    case kEvKick:
        kick(pPlayer, pState->nKickTarget);
        break;
    }
}

// gktest <n>: a nearly dead enemy for kill n appears in front of Caleb (for trying the kills)
int osdcmd_gktest(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
        return OSDCMD_SHOWHELP;
    if (!Bstrcasecmp(parm->parms[0], "kick"))  // a healthy cultist: USE kicks it
    {
        spritetype *pDude = actSpawnDude(gMe->pSprite, kDudeCultistTommy, 600, 0);
        if (pDude)
            OSD_Printf("Kick test: enemy type %d, %d health\n", pDude->type, xsprite[pDude->extra].health >> 4);
        return OSDCMD_OK;
    }
    int const n = atoi(parm->parms[0]);
    if (n < 0 || n >= (int)ARRAY_SIZE(gKills))
        return OSDCMD_SHOWHELP;
    spritetype *pDude = actSpawnDude(gMe->pSprite, gKills[n].nDude[0], 600, 0);
    if (!pDude)
        return OSDCMD_OK;
    xsprite[pDude->extra].health = gKills[n].nHealth >= 0 ? gKills[n].nHealth / 2 : 160;
    OSD_Printf("Gory Kill test: enemy type %d, %d health\n", pDude->type, xsprite[pDude->extra].health >> 4);
    return OSDCMD_OK;
}

} // namespace

void goryKillSetModDir(const char *pzDir)
{
    Bstrncpyz(szDir, pzDir ? pzDir : "", sizeof(szDir));
}

const char *goryKillModFile(const char *pzName)
{
    static char path[BMAX_PATH];
    if (!szDir[0])
        return NULL;
    BDIR *dir = Bopendir(szDir);
    if (!dir)
        return NULL;
    bool bFound = false;
    while (Bdirent *d = Breaddir(dir))
    {
        if (!Bstrcasecmp(d->name, pzName))
        {
            Bsnprintf(path, sizeof(path), "%s/%s", szDir, d->name);
            bFound = true;
            break;
        }
    }
    Bclosedir(dir);
    return bFound ? path : NULL;
}

void goryKillInit(void)
{
    OSD_RegisterFunction("gktest", "gktest <0-10>: a nearly dead enemy for that Gory Kill appears in front of you", osdcmd_gktest);

    bAvailable = false;
    if (!szDir[0])
        return;
    char const *pzExtra = goryKillModFile("EXTRAC.RFF");
    if (!pzExtra || !fileExists(pzExtra))
    {
        LOG_F(ERROR, "Blood Extra Crispy: no EXTRAC.RFF in %s", szDir);
        return;
    }
    char path[BMAX_PATH];
    Bstrncpyz(path, pzExtra, sizeof(path));
    gBecRes.Init(path);
    // the mod's files have ids, but not the flag Blood looks for before it indexes them
    for (unsigned int i = 0; i < gBecRes.count; i++)
        gBecRes.dict[i].flags |= DICT_ID;
    gBecRes.Reindex();
    bAvailable = true;

    // the mod's art comes first: its own title, menus and sprites, and its new tiles
    artSetExtraDir(szDir, 1);

    int nSeqs = 0, nQavs = 0;
    for (auto const &k : gKills)
    {
        if (k.nBodySeq && !gSysRes.Lookup(k.nBodySeq, "SEQ") && copyResource(gSysRes, k.nBodySeq, "SEQ", NULL))
            nSeqs++;
        nQavs += !!gBecRes.Lookup(k.nQav, "QAV");
        for (GKEVENT const *pEv = k.pEvents; pEv->nType != kEvEnd; pEv++)
            if (pEv->nType == kEvSound)
                for (int i = 0; i < max<int>(pEv->nRand, 1); i++)
                    takeSound(pEv->nSound + i);
    }
    LOG_F(INFO, "Gory Kills: Blood Extra Crispy files in %s (%d of %d kills, %d new body animations)", szDir, nQavs,
          (int)ARRAY_SIZE(gKills), nSeqs);
}

bool goryKillAvailable(void)
{
    return bAvailable;
}

bool goryKillActive(PLAYER *pPlayer)
{
    return pPlayer && gState[pPlayer->nPlayer].pDef != NULL && !gState[pPlayer->nPlayer].pDef->bKick;
}

bool goryKillBusy(PLAYER *pPlayer)
{
    return pPlayer && gState[pPlayer->nPlayer].pDef != NULL;
}

bool goryKillTryStart(PLAYER *pPlayer)
{
    if (!bAvailable || !singlePlayer() || !pPlayer || goryKillBusy(pPlayer))
        return false;
    spritetype *pSprite = pPlayer->pSprite;
    XSPRITE *pXSprite = pPlayer->pXSprite;
    if (!pSprite || !pXSprite || pXSprite->health <= 0 || pPlayer->posture == kPostureSwim || pXSprite->height > 16)
        return false;

    // the nearest enemy in front for a Gory Kill; failing that, the nearest one to kick
    int nBest = -1, nBestDist = INT32_MAX, nKick = -1, nKickDist = INT32_MAX;
    GKDEF const *pBestDef = NULL;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    // the sector Caleb's eyes are in (his sprite's can be the one below a step he stands over)
    int16_t nEyeSect = pSprite->sectnum;
    updatesectorz(pSprite->x, pSprite->y, pPlayer->zView, &nEyeSect);
    if (nEyeSect < 0)
        nEyeSect = pSprite->sectnum;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pDude = &sprite[nSprite];
        if (pDude == pSprite || IsPlayerSprite(pDude) || pDude->extra <= 0 || pDude->extra >= kMaxXSprites)
            continue;
        XSPRITE *pXDude = &xsprite[pDude->extra];
        if (pXDude->health <= 0)
            continue;
        GKDEF const *pDef = killFor(pDude->type);
        bool const bGory = pDef && (pDef->nHealth < 0 || pXDude->health <= pDef->nHealth);
        bool bKickable = pDef && !bGory;
        for (short nType : gAlwaysKicked)
            bKickable |= nType == pDude->type;
        if (!bGory && !bKickable)
            continue;
        int const nRange = bGory ? pDef->nDist : (int)kNearer;
        int const dx = pDude->x - pSprite->x, dy = pDude->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        if (nDist > nRange || klabs(pDude->z - pSprite->z) > (nRange << 4))
            continue;
        int const nAngle = (getangle(dx, dy) - pSprite->ang) & 2047;
        if (nAngle > 512 && nAngle < 1536)  // behind Caleb
            continue;
        int nTop, nBottom;
        GetSpriteExtents(pDude, &nTop, &nBottom);
        int const nDudeZ = (nTop + nBottom) >> 1;  // its middle, as Blood's AI looks
        // reachable: a straight line from Caleb's eyes hits it (as aiming does; cansee() stops at some
        // one-way walls Blood draws through)
        bool bReach = cansee(pSprite->x, pSprite->y, pPlayer->zView, nEyeSect, pDude->x, pDude->y, nDudeZ, pDude->sectnum);
        if (!bReach && (dx || dy))
            bReach = HitScan(pSprite, pPlayer->zView, dx, dy, (nDudeZ - pPlayer->zView) >> 4, CLIPMASK1, 0) == 3
                     && gHitInfo.hitsprite == nSprite;
        if (!bReach)
            continue;
        if (bGory && nDist < nBestDist)
        {
            nBest = nSprite;
            nBestDist = nDist;
            pBestDef = pDef;
        }
        else if (!bGory && nDist < nKickDist)
        {
            nKick = nSprite;
            nKickDist = nDist;
        }
    }
    if (nBest < 0 && (nKick < 0 || pState->nKickDelay > 0))
        return false;
    GKDEF const *pStart = nBest >= 0 ? pBestDef : &gKick;

    int const nQav = powerupCheck(pPlayer, kPwUpShadowCloak) ? pStart->nQavCloak : pStart->nQav;
    DICTNODE *hQav = gBecRes.Lookup(nQav, "QAV");
    if (!hQav)
        hQav = gBecRes.Lookup(pStart->nQav, "QAV");
    if (!hQav)
        return false;

    pState->pQav = (QAV *)gBecRes.Lock(hQav);
    pState->pQav->nSprite = -1;
    pState->pQav->Precache();
    pState->pDef = pStart;
    pState->nTimer = pStart->nLength;
    if (pStart->bKick)
    {
        pState->nKickDelay = kKickDelay;
        pState->nKickTarget = nKick;
        return true;
    }

    // the enemy is gone: it's in Caleb's hands now (the QAV); it counts as killed, its drops stay
    spritetype *pDude = &sprite[nBest];
    xsprite[pDude->extra].health = 0;
    actKillDude(pSprite->index, pDude, kDamageFall, 16);
    actPostSprite(nBest, kStatFree);
    return true;
}

void goryKillFrame(void)
{
    for (int i = connecthead; i >= 0; i = connectpoint2[i])
    {
        PLAYER *pPlayer = &gPlayer[i];
        GKSTATE *pState = &gState[i];
        if (pState->nScreenBlood > 0)
            pState->nScreenBlood--;
        if (pState->nKickDelay > 0)
            pState->nKickDelay -= 2;
        if (!pState->pDef)
            continue;
        if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        {
            pState->pDef = NULL;
            continue;
        }
        int const nBefore = pState->nTimer;
        pState->nTimer -= 2;
        for (GKEVENT const *pEv = pState->pDef->pEvents; pEv->nType != kEvEnd; pEv++)
            if (pEv->nTime < nBefore && pEv->nTime >= pState->nTimer)
                runEvent(pPlayer, pState, pEv);
        if (pState->nTimer <= 0)
            pState->pDef = NULL;
    }
}

bool goryKillDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal)
{
    if (!goryKillBusy(pPlayer))
        return false;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    QAV *pQav = pState->pQav;
    if (!pQav)
        return false;
    // the timer goes 60 a second, QAV ticks 120
    int const nTicks = ClipRange((pState->pDef->nLength - pState->nTimer) * 2, 0, pQav->at10 - 1);
    pQav->x = x;
    pQav->y = y;
    int nFlags = 2 | kQavOrientationQ16;
    if (powerupCheck(pPlayer, kPwUpShadowCloak))
        nFlags |= 1;
    pQav->Draw(nTicks, nFlags, nShade, nPal, 0);
    return true;
}

void goryKillDrawScreenBlood(PLAYER *pPlayer)
{
    if (!pPlayer)
        return;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    if (pState->nScreenBlood <= 0 || tilesiz[pState->nScreenBloodTile].x <= 0)
        return;
    // translucent, more so as it fades
    int const nFlags = RS_AUTO | RS_TRANS1 | (pState->nScreenBlood < kScreenBloodTime / 2 ? RS_TRANS2 : 0);
    rotatesprite(160 << 16, 140 << 16, 65536, 0, pState->nScreenBloodTile, 0, 0, nFlags, 0, 0, xdim - 1, ydim - 1);
}

void goryKillReset(void)
{
    for (auto &s : gState)
        s = GKSTATE();
}
