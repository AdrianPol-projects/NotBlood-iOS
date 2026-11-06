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

// Bots for offline bloodbath. Each bot only knows what a player in its place could know:
// what is in front of it and not behind a wall, what it hears (shots, footsteps, jumps)
// and what it remembers. It finds its way with the map's sectors (rooms joined by
// openings), decides every few frames what to do (fight, chase, ambush, flank, run away,
// camp, pick up items) and then "presses keys" through a normal GINPUT, with a human
// reaction time, limited turning speed and an aim that wobbles and settles.
// The bots never call the game's random number generators, so they don't change the game.

#include <math.h>

#include "build.h"
#include "compat.h"
#include "mmulti.h"
#include "osd.h"
#include "common_game.h"
#include "actor.h"
#include "blood.h"
#include "db.h"
#include "endgame.h"
#include "gamemenu.h"
#include "eventq.h"
#include "gameutil.h"
#include "globals.h"
#include "levels.h"
#include "network.h"
#include "player.h"
#include "sfx.h"
#include "trig.h"
#include "view.h"
#include "weapon.h"
#include "config.h"
#include "menu.h"
#include "menuchosen.h"
#include "bot.h"
#include "chosenmodes.h"

int gBotCount = 3;
int gBotSkill = kBotSkillNormal;
int gBotPersona = kBotPersonaMixed;
int gBotEachSkill[kBotMaxBots] = { -1, -1, -1, -1, -1, -1, -1 };
int gBotEachPersona[kBotMaxBots] = { -1, -1, -1, -1, -1, -1, -1 };
int gBotAccuracy = 100;
int gBotReaction = 100;
int gBotAggression = 100;
int gBotJumping = 100;
int gBotAmbush = 100;
int gBotFlank = 100;
int gBotCamping = 100;
int gBotTaunts = 1;
int gBotDebug = 0;
int gBotCultists = 1;
int gBotHandicap = 2;
int gBotHighGround = 100;
static int gBotTuning = 0;
int gBotGrudges = 1;
int gBotAdaptive = 0;
char gBotEachName[kBotMaxBots][16];
int gBotEachTeam[kBotMaxBots];
int gBotEachHandicap[kBotMaxBots] = { -1, -1, -1, -1, -1, -1, -1 };
#define BOTTUNE100 { 100, 100, 100, 100, 100, 100, 100, 100 }
int gBotEachTune[kBotMaxBots][kBotTuneMax] = { BOTTUNE100, BOTTUNE100, BOTTUNE100, BOTTUNE100, BOTTUNE100, BOTTUNE100, BOTTUNE100 };
#undef BOTTUNE100
int gHumanTeam[8];
int gBotNetCount = 0;
bool gBotNet = false;

// the bots of a network game, as the host sent them (packet 242)
static int nRosterBots;
static char nRosterPlayer[kBotMaxBots];
static char nRosterBot[kBotMaxBots];   // which bot of SET EACH BOT (host only)
static PROFILE rosterProfile[kBotMaxBots];
static int nBotOrder = kBotOrderFree;      // for the bots on the human's side
static bool bOrderByHost;                  // a network game's host gave the orders: "follow me" is the host
static int nHoldX, nHoldY, nHoldZ, nHoldSect;
static int nAdapt = 100;                   // adaptive difficulty: percent of aim error and reaction time // 2: the presets include the tuning players liked (old fine tuning is reset once)
static int gBotWatch = 0;

bool gBotMatch = false;
bool gBotWatchMode = false;

int gCareerStage = 1, gCareerWins = 0, gCareerMatches = 0, gCareerKills = 0, gCareerDeaths = 0, gCareerBestStreak = 0;
char gCareerRivals[128] = "";
int gBotCareer = 0;

struct CAREERSTAGE
{
    short nLevel;          // in the BloodBath episode
    char nGameType, nMode, nSkill;
    char nFrags, nLives;
    const char *pzNames[6];
    char nPersonas[6];
};

// the ladder: more and better opponents, the match types mixed in
static const CAREERSTAGE gCareer[kCareerStages] = {
    { 1, kGameTypeBloodBath, kChosenModeNormal, kBotSkillEasy, 5, 0, { "Barnabas" }, { kBotPersonaBalanced } },
    { 2, kGameTypeBloodBath, kChosenModeNormal, kBotSkillEasy, 8, 0, { "Silas", "Josiah" }, { kBotPersonaRusher, kBotPersonaCamper } },
    { 3, kGameTypeBloodBath, kChosenModeGunGame, kBotSkillNormal, 0, 0, { "Gideon", "Ishmael", "Zebulon" }, { kBotPersonaJumper, kBotPersonaBalanced, kBotPersonaCoward } },
    { 4, kGameTypeBloodBath, kChosenModeNormal, kBotSkillNormal, 10, 0, { "Thaddeus", "Jericho", "Lazarus" }, { kBotPersonaTrickster, kBotPersonaRusher, kBotPersonaCamper } },
    { 5, kGameTypeBloodBath, kChosenModeKingOfTheHill, kBotSkillNormal, 45, 0, { "Obadiah", "Jedidiah", "Mordecai" }, { kBotPersonaCamper, kBotPersonaRusher, kBotPersonaTrickster } },
    { 6, kGameTypeBloodBath, kChosenModeJuggernaut, kBotSkillNormal, 10, 0, { "Ezekiel", "Silas", "Gideon", "Barnabas" }, { kBotPersonaRusher, kBotPersonaJumper, kBotPersonaBalanced, kBotPersonaTrickster } },
    { 7, kGameTypeBloodBath, kChosenModeNormal, kBotSkillHard, 12, 0, { "Malachi", "Ishmael", "Josiah", "Lazarus" }, { kBotPersonaTrickster, kBotPersonaCamper, kBotPersonaJumper, kBotPersonaRusher } },
    { 8, kGameTypeBloodBath, kChosenModeLastManStanding, kBotSkillHard, 0, 3, { "Mordecai", "Zebulon", "Jericho", "Thaddeus", "Ezekiel" }, { kBotPersonaCoward, kBotPersonaCamper, kBotPersonaTrickster, kBotPersonaRusher, kBotPersonaBalanced } },
    { 9, kGameTypeBloodBath, kChosenModeGunGame, kBotSkillHard, 0, 0, { "Obadiah", "Gideon", "Silas", "Malachi", "Jedidiah" }, { kBotPersonaJumper, kBotPersonaRusher, kBotPersonaTrickster, kBotPersonaBalanced, kBotPersonaCamper } },
    { 10, kGameTypeBloodBath, kChosenModeNormal, kBotSkillNightmare, 15, 0, { "Malachi", "Ezekiel", "Mordecai", "Ishmael", "Lazarus", "Jericho" }, { kBotPersonaTrickster, kBotPersonaRusher, kBotPersonaCamper, kBotPersonaJumper, kBotPersonaBalanced, kBotPersonaCoward } },
};

const char *gBotSkillNames[kBotSkillMax] = { "EASY", "NORMAL", "HARD", "NIGHTMARE" };
const char *gBotPersonaNames[kBotPersonaMax] = { "MIXED", "BALANCED", "RUSHER", "CAMPER", "TRICKSTER", "COWARD", "JUMPER" };

static const char *pzBotNames[] = {
    "Malachi", "Ezekiel", "Obadiah", "Mordecai", "Jedidiah", "Zebulon", "Thaddeus",
    "Barnabas", "Lazarus", "Silas", "Gideon", "Josiah", "Ishmael", "Jericho",
};

static const char *pzBotKillLines[] = {
    "Rest in pieces.", "Next!", "Too slow.", "Stay down.", "That's gotta hurt.", "Back to the grave with you.",
    "Is that all?", "You should see the other guy... oh wait.", "Burn.", "Another one for the pile.",
};

static const char *pzBotGrudgeLines[] = {
    "Payback!", "That's for last time.", "Remember me?", "We're even now... almost.", "I told you I'd find you.",
};

static const char *pzBotDeathLines[] = {
    "Lucky shot.", "I'll be back.", "Cheap!", "Not again...", "Hmph.", "Enjoy it while it lasts.",
};

enum {
    kBotStateRoam = 0,
    kBotStateCollect,
    kBotStateFight,
    kBotStateHunt,
    kBotStateAmbush,
    kBotStateFlank,
    kBotStateFlee,
    kBotStateCamp,
    kBotStateDead,
    kBotStateHighGround,
    kBotStateObjective,
    kBotStateMax
};

static const char *pzBotStateNames[kBotStateMax] = { "roam", "collect", "fight", "hunt", "ambush", "flank", "flee", "camp", "dead", "high ground", "objective" };

struct BOTSKILL
{
    int nAimError;  // build angle units (2048 = full turn)
    int nReaction;  // frames (30 per second) before reacting to a new enemy
    int nTurnSpeed; // most it can turn in a frame
    int nFov;       // half of its field of view
    int nHearing;   // how far it hears shots
    int nLead;      // percent of correct lead on projectiles
    int nTactics;   // chance of using its head (ambush, flank, retreat to health)
    int nTrigger;   // percent of the time it keeps shooting while on target (lower: pauses like a nervous player)
    int nAutoAim;   // the profile's autoaim (0 off, 2 hitscan weapons only, 1 on)
};

static const BOTSKILL gBotSkillInfo[kBotSkillMax] = {
    { 204, 31, 30, 240, 4500, 10, 25, 55, 0 },
    { 136, 20, 50, 295, 6800, 35, 45, 78, 0 },
    { 85, 14, 78, 355, 9800, 60, 70, 90, 2 },
    { 51, 10, 115, 415, 13500, 85, 95, 100, 1 },
};

struct BOTPERSONA
{
    int nAggression; // chase and push in instead of keeping distance
    int nCamping;    // stay in a good spot and wait
    int nAmbush;     // wait beside the doorway an enemy is about to come through
    int nFlank;      // take another way round to come from behind/the side
    int nJump;       // jump around in fights
    int nFlee;       // health below which it backs off
    int nStrafe;     // dodge sideways in fights
    int nHighGround; // go for (and hold) places high up that see a lot
};

static const BOTPERSONA gBotPersonaInfo[kBotPersonaMax] = {
    { 72, 14, 81, 40, 52, 30, 60, 40 },  // mixed (not used directly)
    { 72, 14, 81, 40, 52, 30, 60, 40 },  // balanced
    { 100, 0, 18, 25, 75, 10, 50, 20 },  // rusher
    { 39, 63, 100, 15, 15, 40, 30, 68 }, // camper
    { 72, 18, 100, 90, 52, 35, 65, 48 }, // trickster
    { 26, 35, 90, 30, 68, 65, 70, 44 },  // coward
    { 91, 4, 36, 30, 100, 25, 90, 28 },  // jumper
};

#define kBotMaxPath 96
#define kBotMaxBlocked 4
#define kBotMaxCampSpots 48
#define kBotMaxItemSpots 128

struct BOTMEMORY
{
    bool bValid;
    int x, y, z, nSector;
    int vx, vy;
    int nTime;   // when it was last known (gLevelTime)
    bool bSeen;  // seen (not just heard)
};

struct BOT
{
    bool bActive;
    int nPlayer;
    int nEach;      // which bot of SET EACH BOT (0-6)
    int nSkill, nPersona;
    BOTSKILL skill;
    BOTPERSONA persona;
    uint32_t nRand;

    int nState, nStateTime, nNextThink;
    int nGoalX, nGoalY, nGoalZ, nGoalSect, nGoalSprite;
    short path[kBotMaxPath];
    int nPathLen, nPathPos, nPathGoalSect, nRepathTime;
    int nWaitUntil, nWatchAng;

    int nTarget;          // sprite index, -1 none
    int nTargetSince;     // when it first saw the current target
    int nReactAt;         // can shoot/turn to it from then on
    int nAimErr, nAimErrGoal, nAimErrNext, nTrackFrames, nAimErrZ, nFirePauseUntil;
    int nStrafeDir, nStrafeUntil, nJumpNext, nCrouchUntil;
    int nWeaponNext;
    int nFireHold, nFireRelease;

    int nLastHealth, nHurtTime;
    BOTMEMORY mem[kMaxPlayers];
    int nHurt[kMaxPlayers];        // damage it saw its shots do to each enemy since that one last spawned
    int nEnemyHealth[kMaxPlayers]; // to see the change
    int nLastShot;                 // when it last fired
    int nLastKill, nLastPain;      // momentum: just got a kill / just got hurt badly
    int nThirdPartyUntil;          // letting two others fight it out first

    int nStuckX, nStuckY, nStuckCheck, nStuckCount, nUnstickUntil, nUnstickStrafe;
    int blockedWall[kBotMaxBlocked], blockedUntil[kBotMaxBlocked];
    int nUseTime;
    int nRespawnDelay;
    int nLastFrags, nLastDeaths, nTalkAt;
    const char *pzTalk;
    int nSearchUntil;
    bool bWasDead;
    int ignoreSprite[4], ignoreUntil[4]; // items it couldn't pick up
    int nAtGoalSince;
    int nProgressDist, nProgressTime, nProgressX, nProgressY; // getting closer to where it's heading?
    int nAvoidSide, nAvoidUntil;
    int nCampSpot, nLastCamp;
    int nGlance, nGlanceNext;
    int nVantage, nHoldUntil, nLastHighTry; // the high spot it is going to or holding
    int nLiftSince, nLiftWall;      // waiting for a lift at this opening since
    bool bDetour;                   // gone to push a lift's switch; then back to its goal
    bool bStayAbove;                // the enemy is below: shoot down rather than follow (decided now and then)
    int nAboveDecideAt;
    int nDetourAng, nSavedX, nSavedY, nSavedZ, nSavedSect;
};

static BOT gBot[kMaxPlayers];

static int nCampSpots;
static vec3_t campSpot[kBotMaxCampSpots];
static short campSect[kBotMaxCampSpots];
static short campAng[kBotMaxCampSpots];
static uint8_t nCampFails[kBotMaxCampSpots];
#define kBotMaxVantage 24
static vec3_t vantageSpot[kBotMaxVantage]; // high places that see a lot: what the bots know about a map
static short vantageSect[kBotMaxVantage];
static int vantageValue[kBotMaxVantage];
static int vantageHeight[kBotMaxVantage];
static uint8_t nVantageFails[kBotMaxVantage];
static int nVantageSpots;
static int nItemSpots;
static vec2_t itemSpot[kBotMaxItemSpots];
static short itemSpotSect[kBotMaxItemSpots];

static vec2_t sectCenter[MAXSECTORS];
static int nSectPenalty[MAXSECTORS]; // extra path cost in percent, for flanking and running away

static int nNow; // gLevelTime of the frame being thought about

static void BotDescribe(int i, char *pzBuffer, int nSize);

//
// small helpers
//

static inline int BotRand(BOT *pBot, int nRange)
{
    // xorshift; deliberately not the game's own random numbers
    uint32_t x = pBot->nRand;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pBot->nRand = x;
    return nRange > 0 ? int((x >> 8) % (uint32_t)nRange) : 0;
}

static inline bool BotChance(BOT *pBot, int nPercent)
{
    return BotRand(pBot, 100) < nPercent;
}

static inline int AngDiff(int a, int b) // b-a in -1024..1023
{
    return ((b - a + 1024) & 2047) - 1024;
}

static inline int BotDist(int x1, int y1, int x2, int y2)
{
    return approxDist(x2 - x1, y2 - y1);
}

static int SpriteCenterZ(spritetype *pSprite)
{
    int top, bottom;
    GetSpriteExtents(pSprite, &top, &bottom);
    return (top + bottom) >> 1;
}

static bool IsEnemyPlayer(PLAYER *pMe, PLAYER *pOther)
{
    if (pMe == pOther || !pOther->pSprite || !pOther->pXSprite)
        return false;
    if (gGameOptions.nGameType == kGameTypeCoop)
        return false;
    if (gGameOptions.nGameType == kGameTypeTeams && pMe->teamId == pOther->teamId)
        return false;
    return modesIsEnemy(pMe, pOther); // hide and seek: seekers after hiders they can spot
}

static PLAYER *SpritePlayer(int nSprite)
{
    if (nSprite < 0)
        return NULL;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (gPlayer[p].nSprite == nSprite)
            return &gPlayer[p];
    return NULL;
}

static bool SpriteAlive(int nSprite)
{
    if (nSprite < 0 || sprite[nSprite].statnum >= kMaxStatus || sprite[nSprite].extra <= 0)
        return false;
    if (sprite[nSprite].statnum != kStatDude)
        return false;
    return xsprite[sprite[nSprite].extra].health > 0;
}

//
// map knowledge: sectors as rooms joined by openings
//

static int PortalGap(int nWall, int x, int y)
{
    const int s1 = sectorofwall(nWall), s2 = wall[nWall].nextsector;
    int c1, f1, c2, f2;
    getzsofslope(s1, x, y, &c1, &f1);
    getzsofslope(s2, x, y, &c2, &f2);
    return min(f1, f2) - max(c1, c2);
}

static uint8_t bDoorSect[MAXSECTORS]; // doors a player can open by pressing "use" (on them or a switch beside them)

static bool IsMovingSectorType(int nType)
{
    return nType == kSectorZMotion || nType == kSectorSlide || nType == kSectorRotate || nType == kSectorSlideMarked || nType == kSectorRotateMarked
        || nType == kSectorZMotionSprite || nType == kSectorRotateStep;
}

// where the buttons are: for every channel the things you can push that send to it, and where to
// stand to push them
#define kMaxOpeners 8
struct OPENER
{
    short nSector;
    int x, y;    // where to stand
    short nAng;  // facing it
};
static OPENER openers[1024][kMaxOpeners];
static uint8_t nOpeners[1024];

static void AddOpener(int nChannel, int nSector, int tx, int ty)
{
    nChannel &= 1023;
    if (nOpeners[nChannel] >= kMaxOpeners)
        return;
    // stand a step away from it, towards the middle of its room
    const int nAway = getangle(sectCenter[nSector].x - tx, sectCenter[nSector].y - ty);
    int x = tx + mulscale30(Cos(nAway), 140), y = ty + mulscale30(Sin(nAway), 140);
    if (!inside(x, y, nSector))
        x = sectCenter[nSector].x, y = sectCenter[nSector].y;
    OPENER *pOpener = &openers[nChannel][nOpeners[nChannel]++];
    pOpener->nSector = nSector;
    pOpener->x = x, pOpener->y = y;
    pOpener->nAng = getangle(tx - x, ty - y);
}

// can someone standing in nFrom open door sector nDoor?
static bool CanOpenFrom(int nDoor, int nFrom)
{
    XSECTOR *pXSector = &xsector[sector[nDoor].extra];
    if (pXSector->Push || pXSector->Wallpush)
        return true;
    const int nChannel = pXSector->rxID & 1023;
    for (int i = 0; i < nOpeners[nChannel]; i++)
        if (openers[nChannel][i].nSector == nFrom || openers[nChannel][i].nSector == nDoor)
            return true;
    return false;
}

static void FindDoors(void)
{
    Bmemset(nOpeners, 0, sizeof(nOpeners));
    uint8_t bPushChannel[1024];
    Bmemset(bPushChannel, 0, sizeof(bPushChannel));
    for (int w = 0; w < numwalls; w++)
    {
        if (wall[w].extra > 0 && xwall[wall[w].extra].triggerPush && xwall[wall[w].extra].txID)
        {
            bPushChannel[xwall[wall[w].extra].txID & 1023] = 1;
            AddOpener(xwall[wall[w].extra].txID, sectorofwall(w), (wall[w].x + wall[wall[w].point2].x) >> 1, (wall[w].y + wall[wall[w].point2].y) >> 1);
        }
    }
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum < kMaxStatus && sprite[i].extra > 0 && xsprite[sprite[i].extra].Push && xsprite[sprite[i].extra].txID)
        {
            bPushChannel[xsprite[sprite[i].extra].txID & 1023] = 1;
            AddOpener(xsprite[sprite[i].extra].txID, sprite[i].sectnum, sprite[i].x, sprite[i].y);
        }
    }
    for (int s = 0; s < numsectors; s++)
    {
        bDoorSect[s] = 0;
        if (sector[s].extra <= 0 || !IsMovingSectorType(sector[s].type))
            continue;
        XSECTOR *pXSector = &xsector[sector[s].extra];
        if (pXSector->locked || pXSector->Key)
            continue;
        bDoorSect[s] = pXSector->Push || pXSector->Wallpush || (pXSector->rxID && bPushChannel[pXSector->rxID & 1023]);
    }
}

static bool IsDoorSector(int nSector)
{
    return nSector >= 0 && nSector < numsectors && bDoorSect[nSector];
}

static bool IsLift(int nSector) // a moving sector whose floor goes up and down
{
    if (nSector < 0 || sector[nSector].extra <= 0 || !IsMovingSectorType(sector[nSector].type))
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return pXSector->onFloorZ != pXSector->offFloorZ;
}

static bool DoorClosed(int nSector) // rotating and sliding doors don't change height: closed is their "off" state
{
    if (!IsDoorSector(nSector))
        return false;
    const int nType = sector[nSector].type;
    if (nType != kSectorSlide && nType != kSectorRotate && nType != kSectorSlideMarked && nType != kSectorRotateMarked && nType != kSectorRotateStep)
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return !pXSector->state && !pXSector->busy;
}

static bool IsHurtSector(int nSector)
{
    if (sector[nSector].extra <= 0)
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return pXSector->damageType > 0 && sector[nSector].type == kSectorDamage;
}

enum {
    kEdgeWalk = 0,
    kEdgeJump = 1,
    kEdgeDoor = 2,
    kEdgeBlocked = 3,
    kEdgeLift = 4,  // only with the lift at the right height: ride it, call it, or push its switch
};

static int EdgeType(int nWall)
{
    const int nNext = wall[nWall].nextsector;
    if (nNext < 0)
        return kEdgeBlocked;
    const int nFrom = sectorofwall(nWall);
    // a door this side can open (some only open with a button on the other side)
    const bool bDoor = (IsDoorSector(nNext) && CanOpenFrom(nNext, nFrom)) || (IsDoorSector(nFrom) && CanOpenFrom(nFrom, nFrom));
    if (wall[nWall].cstat & 1) // blocking wall
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    if (DoorClosed(nNext) || DoorClosed(nFrom))
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    const int x = (wall[nWall].x + wall[wall[nWall].point2].x) >> 1;
    const int y = (wall[nWall].y + wall[wall[nWall].point2].y) >> 1;
    if (approxDist(wall[nWall].x - wall[wall[nWall].point2].x, wall[nWall].y - wall[wall[nWall].point2].y) < 48)
        return kEdgeBlocked; // too narrow to walk through
    const int nGap = PortalGap(nWall, x, y);
    const int nRiseNow = getflorzofslope(nFrom, x, y) - getflorzofslope(nNext, x, y);
    if ((IsLift(nNext) || IsLift(nFrom)) && nRiseNow > (54<<8))
        return kEdgeBlocked; // a lift that is up (or down) right now (see EdgeTypePlan)
    if (nGap < (28<<8))
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    const int nRise = getflorzofslope(nFrom, x, y) - getflorzofslope(nNext, x, y);
    if (nRise > (54<<8) && !(sector[nNext].extra > 0 && xsector[sector[nNext].extra].Underwater))
        return kEdgeBlocked;
    if (nRise > (18<<8))
        return kEdgeJump;
    return kEdgeWalk;
}

// the lift on either side of an opening (-1 none)
static int EdgeLift(int nWall)
{
    if (IsLift(wall[nWall].nextsector))
        return wall[nWall].nextsector;
    if (IsLift(sectorofwall(nWall)))
        return sectorofwall(nWall);
    return -1;
}

// the lift's floor height at which this opening can be walked (INT32_MIN none)
static int LiftFloorFor(int nWall)
{
    const int nLift = EdgeLift(nWall);
    if (nLift < 0 || (wall[nWall].cstat & 1))
        return INT32_MIN;
    const int nFrom = sectorofwall(nWall), nNext = wall[nWall].nextsector;
    const int x = (wall[nWall].x + wall[wall[nWall].point2].x) >> 1, y = (wall[nWall].y + wall[wall[nWall].point2].y) >> 1;
    XSECTOR *pXSector = &xsector[sector[nLift].extra];
    const int nOther = nLift == nNext ? getflorzofslope(nFrom, x, y) : getflorzofslope(nNext, x, y);
    const int nHeights[2] = { pXSector->onFloorZ, pXSector->offFloorZ };
    for (int z : nHeights)
    {
        const int nRise = nLift == nNext ? nOther - z : z - nOther; // floor going on minus floor coming from
        if (nRise <= (54<<8) && nRise >= -(160<<8))
            return z;
    }
    return INT32_MIN;
}

static bool bStrictLifts; // finding pits: a switch elsewhere doesn't count (you can't get to it from down there)

// can someone in nFrom get this lift moving: press use on it (standing on it), on its wall (from
// beside it), or on a switch for it
static bool LiftUsableFrom(int nLift, int nFrom)
{
    XSECTOR *pXSector = &xsector[sector[nLift].extra];
    if (pXSector->locked || pXSector->Key)
        return false;
    if ((pXSector->Push && nFrom == nLift) || (pXSector->Wallpush && nFrom != nLift))
        return true;
    const int nChannel = pXSector->rxID & 1023;
    if (!pXSector->rxID)
        return false;
    for (int i = 0; i < nOpeners[nChannel]; i++)
        if (!bStrictLifts || openers[nChannel][i].nSector == nFrom || openers[nChannel][i].nSector == nLift)
            return true;
    return false;
}

// For planning: like EdgeType, but an opening to or from a lift counts when the lift can be at its height.
static int EdgeTypePlan(int nWall)
{
    const int nType = EdgeType(nWall);
    if (nType != kEdgeBlocked)
        return nType;
    if (wall[nWall].nextsector < 0 || approxDist(wall[nWall].x - wall[wall[nWall].point2].x, wall[nWall].y - wall[wall[nWall].point2].y) < 48)
        return kEdgeBlocked;
    if (LiftFloorFor(nWall) == INT32_MIN || !LiftUsableFrom(EdgeLift(nWall), sectorofwall(nWall)))
        return kEdgeBlocked;
    return kEdgeLift;
}

static void PortalPoint(int nWall, int tx, int ty, int *px, int *py)
{
    // the point of the opening closest to (tx,ty), kept away from its edges
    const int x1 = wall[nWall].x, y1 = wall[nWall].y;
    const int x2 = wall[wall[nWall].point2].x, y2 = wall[wall[nWall].point2].y;
    const float dx = float(x2 - x1), dy = float(y2 - y1);
    const float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.f)
    {
        *px = x1, *py = y1;
        return;
    }
    const float margin = min(len * 0.35f, 220.f) / len;
    float t = ((tx - x1) * dx + (ty - y1) * dy) / (len * len);
    t = clamp(t, margin, 1.f - margin);
    *px = x1 + int(dx * t);
    *py = y1 + int(dy * t);
}

static uint8_t nWallFails[kMaxWalls]; // openings bots got stuck at, this level
static uint8_t nItemFails[kMaxSprites]; // items bots couldn't get to (secret lifts and the like)
static uint8_t bPitWall[kMaxWalls];     // a drop with no way back up
static uint8_t bPitSect[MAXSECTORS];    // down in such a pit

// An opening it couldn't get through: avoid it for a while; when several times, for good.
static void BlockWall(BOT *pBot, int nWall)
{
    int nSlot = 0;
    for (int i = 1; i < kBotMaxBlocked; i++)
        if (pBot->blockedUntil[i] < pBot->blockedUntil[nSlot])
            nSlot = i;
    pBot->blockedWall[nSlot] = nWall;
    pBot->blockedUntil[nSlot] = nNow + 30*60;
    if (nWallFails[nWall] < 255)
        nWallFails[nWall]++;
    if (gBotDebug == 2)
    {
        const int s1 = sectorofwall(nWall), s2 = wall[nWall].nextsector;
        LOG_F(INFO, "Bot %s stuck at opening %d (%d times): sector %d floor %d ceiling %d -> sector %d floor %d ceiling %d, cstat %d/%d, width %d",
            gProfile[pBot->nPlayer].name, nWall, nWallFails[nWall], s1, (int)sector[s1].floorz, (int)sector[s1].ceilingz, s2, s2 >= 0 ? (int)sector[s2].floorz : 0, s2 >= 0 ? (int)sector[s2].ceilingz : 0,
            (int)wall[nWall].cstat, (int)wall[nWall].nextwall >= 0 ? (int)wall[wall[nWall].nextwall].cstat : -1,
            approxDist((int)wall[nWall].x - (int)wall[wall[nWall].point2].x, (int)wall[nWall].y - (int)wall[wall[nWall].point2].y));
    }
}

enum { kDoorOpen, kDoorClosed, kDoorMoving };

static int DoorState(int nWall)
{
    const int nSectors[2] = { wall[nWall].nextsector, sectorofwall(nWall) };
    bool bAnyDoor = false;
    for (int s : nSectors)
    {
        if (!IsDoorSector(s))
            continue;
        bAnyDoor = true;
        XSECTOR *pXSector = &xsector[sector[s].extra];
        if (pXSector->busy)
            return kDoorMoving;
        if (!pXSector->state)
            return kDoorClosed;
    }
    if (!bAnyDoor)
        return kDoorClosed;
    return PortalGap(nWall, wall[nWall].x, wall[nWall].y) < (28<<8) ? kDoorClosed : kDoorOpen;
}

// What "use" would do right now: 1 push something, 0 nothing, -1 it's a level exit (never press).
static int UseCheck(PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int hit = HitScan(pSprite, pPlayer->zView, Cos(pSprite->ang)>>16, Sin(pSprite->ang)>>16, pPlayer->slope, 0x10000040, 128);
    if ((approxDist(pSprite->x - gHitInfo.hitx, pSprite->y - gHitInfo.hity) >> 4) >= 64)
        return 0;
    int nTx = 0, nResult = 0;
    switch (hit)
    {
    case 3:
        if (sprite[gHitInfo.hitsprite].extra > 0 && sprite[gHitInfo.hitsprite].statnum != kStatDude)
        {
            XSPRITE *pXSprite = &xsprite[sprite[gHitInfo.hitsprite].extra];
            nResult = pXSprite->Push, nTx = pXSprite->txID;
        }
        break;
    case 0:
    case 4:
        if (wall[gHitInfo.hitwall].extra > 0 && xwall[wall[gHitInfo.hitwall].extra].triggerPush)
            nResult = 1, nTx = xwall[wall[gHitInfo.hitwall].extra].txID;
        else if (wall[gHitInfo.hitwall].nextsector >= 0 && sector[wall[gHitInfo.hitwall].nextsector].extra > 0)
        {
            XSECTOR *pXSector = &xsector[sector[wall[gHitInfo.hitwall].nextsector].extra];
            nResult = pXSector->Wallpush, nTx = pXSector->txID;
        }
        break;
    case 1:
    case 2:
        if (gHitInfo.hitsect >= 0 && sector[gHitInfo.hitsect].extra > 0)
        {
            XSECTOR *pXSector = &xsector[sector[gHitInfo.hitsect].extra];
            nResult = pXSector->Push, nTx = pXSector->txID;
        }
        break;
    }
    if (nResult && (nTx == kChannelLevelExitNormal || nTx == kChannelLevelExitSecret))
        return -1;
    return nResult ? 1 : 0;
}

static bool WallBlocked(BOT *pBot, int nWall)
{
    if (nWallFails[nWall] >= 3)
        return true;
    if (!pBot)
        return false;
    for (int i = 0; i < kBotMaxBlocked; i++)
        if (pBot->blockedWall[i] == nWall && pBot->blockedUntil[i] > nNow)
            return true;
    return false;
}
// A* over sectors
static int pathCost[MAXSECTORS];
static short pathFrom[MAXSECTORS];  // wall used to enter the sector
static vec2_t pathEntry[MAXSECTORS];
static uint8_t pathState[MAXSECTORS]; // 0 new, 1 open, 2 closed
struct PATHNODE { int nKey; short nSector; };
#define kPathHeapMax (MAXSECTORS*4)
static PATHNODE pathHeap[kPathHeapMax];
static int pathHeapSize;
static short pathChain[MAXSECTORS];

static void HeapPush(int nSector, int nKey)
{
    if (pathHeapSize >= kPathHeapMax)
        return;
    int i = pathHeapSize++;
    while (i > 0)
    {
        const int nParent = (i - 1) >> 1;
        if (pathHeap[nParent].nKey <= nKey)
            break;
        pathHeap[i] = pathHeap[nParent];
        i = nParent;
    }
    pathHeap[i].nKey = nKey;
    pathHeap[i].nSector = nSector;
}

static int HeapPop(void)
{
    const int nTop = pathHeap[0].nSector;
    const PATHNODE last = pathHeap[--pathHeapSize];
    int i = 0;
    for (;;)
    {
        int nChild = i*2 + 1;
        if (nChild >= pathHeapSize)
            break;
        if (nChild + 1 < pathHeapSize && pathHeap[nChild + 1].nKey < pathHeap[nChild].nKey)
            nChild++;
        if (pathHeap[nChild].nKey >= last.nKey)
            break;
        pathHeap[i] = pathHeap[nChild];
        i = nChild;
    }
    if (pathHeapSize > 0)
        pathHeap[i] = last;
    return nTop;
}

// Finds a way from (sx,sy) in nStart to (gx,gy) in nGoal. Fills pOut with the walls (openings)
// to go through and returns how many, or -1 if there is no way. pBot may be NULL.
static int PathSearch(BOT *pBot, int nStart, int sx, int sy, int nGoal, int gx, int gy, short *pOut, int nMaxOut, int *pCost)
{
    if (nStart < 0 || nGoal < 0 || nStart >= numsectors || nGoal >= numsectors)
        return -1;
    if (pCost)
        *pCost = 0;
    if (nStart == nGoal)
        return 0;
    Bmemset(pathState, 0, numsectors);
    pathHeapSize = 0;
    pathCost[nStart] = 0;
    pathFrom[nStart] = -1;
    pathEntry[nStart].x = sx;
    pathEntry[nStart].y = sy;
    pathState[nStart] = 1;
    HeapPush(nStart, BotDist(sx, sy, gx, gy));
    int nExpanded = 0;
    bool bFound = false;
    while (pathHeapSize > 0 && nExpanded < 4096)
    {
        const int s = HeapPop();
        if (pathState[s] == 2)
            continue;
        pathState[s] = 2;
        nExpanded++;
        if (s == nGoal)
        {
            bFound = true;
            break;
        }
        const int nStartWall = sector[s].wallptr, nEndWall = nStartWall + sector[s].wallnum;
        for (int w = nStartWall; w < nEndWall; w++)
        {
            const int n = wall[w].nextsector;
            if (n < 0 || pathState[n] == 2)
                continue;
            const int nEdge = EdgeTypePlan(w);
            if (nEdge == kEdgeBlocked || WallBlocked(pBot, w))
                continue;
            int px, py;
            PortalPoint(w, pathEntry[s].x, pathEntry[s].y, &px, &py);
            int nCost = BotDist(pathEntry[s].x, pathEntry[s].y, px, py) + 64;
            if (nEdge == kEdgeJump)
                nCost += 400;
            else if (nEdge == kEdgeDoor)
                nCost += 600;
            else if (nEdge == kEdgeLift)
            {
                nCost += 1500; // waiting for it
                const bool bWasStrict = bStrictLifts;
                bStrictLifts = true;
                if (!LiftUsableFrom(EdgeLift(w), s))
                    nCost += 5000; // its switch is somewhere else: a long way round
                bStrictLifts = bWasStrict;
            }
            if (IsHurtSector(n))
                nCost += 6000;
            if (bPitWall[w])
                nCost += 25000; // only if there's no other way
            const int nDrop = getflorzofslope(n, px, py) - getflorzofslope(s, px, py);
            if (nDrop > (48<<8)) // jumping down: easy to do, but it gives up the high ground and may be a pit
                nCost += (nDrop >> 8) * (pBot ? 4 + pBot->persona.nHighGround / 8 : 4);
            if (nSectPenalty[n])
                nCost += nCost * nSectPenalty[n] / 100 + nSectPenalty[n] * 8;
            nCost += pathCost[s];
            if (pathState[n] == 0 || nCost < pathCost[n])
            {
                pathCost[n] = nCost;
                pathFrom[n] = w;
                pathEntry[n].x = px;
                pathEntry[n].y = py;
                pathState[n] = 1;
                HeapPush(n, nCost + BotDist(px, py, gx, gy)); // stale entries are skipped when popped
            }
        }
    }
    if (!bFound)
        return -1;
    if (pCost)
        *pCost = pathCost[nGoal] + BotDist(pathEntry[nGoal].x, pathEntry[nGoal].y, gx, gy);
    int n = 0;
    for (int s = nGoal; s != nStart && n < MAXSECTORS; )
    {
        const int w = pathFrom[s];
        if (w < 0)
            break;
        pathChain[n++] = w;
        s = sectorofwall(w);
    }
    const int nOut = min(n, nMaxOut); // a very long way: the first part of it, it plans again later
    for (int i = 0; i < nOut; i++)
        pOut[i] = pathChain[n - 1 - i];
    return nOut;
}

static bool BotFindPath(BOT *pBot, int nGoalSect, int gx, int gy)
{
    spritetype *pSprite = gPlayer[pBot->nPlayer].pSprite;
    pBot->nPathLen = pBot->nPathPos = 0;
    pBot->nPathGoalSect = nGoalSect;
    pBot->nRepathTime = nNow;
    const int nLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, nGoalSect, gx, gy, pBot->path, kBotMaxPath, NULL);
    if (nLen < 0)
        return false;
    pBot->nPathLen = nLen;
    return true;
}

static void BotClearPenalties(void)
{
    Bmemset(nSectPenalty, 0, sizeof(nSectPenalty[0]) * numsectors);
}

//
// level setup
//

// How good a place is: how much it sees, how far it looks down on it, weapons close by
static int SpotValue(int x, int y, int nSector, int *pHeight)
{
    const int zFloor = getflorzofslope(nSector, x, y);
    const int zEye = zFloor - (40<<8);
    int nSeen = 0, nDrop = 0, nChecks = 0;
    const int nStep = numsectors / 250 + 1;
    for (int s = (x & 7) % nStep; s < numsectors && nChecks < 250; s += nStep)
    {
        if (s == nSector)
            continue;
        const int cx = sectCenter[s].x, cy = sectCenter[s].y;
        if (BotDist(x, y, cx, cy) > 9000 || !inside(cx, cy, s))
            continue;
        nChecks++;
        const int f = getflorzofslope(s, cx, cy);
        if (getceilzofslope(s, cx, cy) > f - (32<<8))
            continue;
        if (!cansee(x, y, zEye, nSector, cx, cy, f - (40<<8), s))
            continue;
        nSeen++;
        nDrop += ClipRange((f - zFloor) >> 8, -64, 160);
    }
    *pHeight = nSeen ? nDrop / nSeen : 0;
    int nValue = nSeen * nStep * (100 + ClipRange(*pHeight, -50, 120)) / 100;
    int nItems = 0;
    for (int i = headspritestat[kStatItem]; i >= 0 && nItems < 3; i = nextspritestat[i])
    {
        const int nType = sprite[i].type;
        if (((nType > kItemWeaponBase && nType < kItemWeaponMax) || nType == kItemTwoGuns || nType == kItemDeathMask) && BotDist(x, y, sprite[i].x, sprite[i].y) < 1500)
            nItems++, nValue += nValue / 6 + 4;
    }
    return nValue;
}

static void AddVantage(int x, int y, int nSector)
{
    if (nSector < 0 || IsHurtSector(nSector) || !inside(x, y, nSector))
        return;
    const int z = getflorzofslope(nSector, x, y);
    if (getceilzofslope(nSector, x, y) > z - (56<<8))
        return;
    for (int i = 0; i < nVantageSpots; i++)
        if (BotDist(vantageSpot[i].x, vantageSpot[i].y, x, y) < 800)
            return;
    int nHeight;
    const int nValue = SpotValue(x, y, nSector, &nHeight);
    if (nHeight < 24) // only places above what they see
        return;
    int nSlot = nVantageSpots;
    if (nVantageSpots >= kBotMaxVantage)
    {
        nSlot = 0;
        for (int i = 1; i < kBotMaxVantage; i++)
            if (vantageValue[i] < vantageValue[nSlot])
                nSlot = i;
        if (vantageValue[nSlot] >= nValue)
            return;
    }
    else
        nVantageSpots++;
    vantageSpot[nSlot].x = x, vantageSpot[nSlot].y = y, vantageSpot[nSlot].z = z;
    vantageSect[nSlot] = nSector;
    vantageValue[nSlot] = nValue;
    vantageHeight[nSlot] = nHeight;
}

static void FindVantageSpots(void)
{
    nVantageSpots = 0;
    Bmemset(nVantageFails, 0, sizeof(nVantageFails));
    // places above a neighbouring area (ledges, balconies, towers), item and camping spots
    int nLedges = 0;
    for (int s = 0; s < numsectors && nLedges < 96; s++)
    {
        const int f = sector[s].floorz;
        bool bAbove = false;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum && !bAbove; w++)
            bAbove = wall[w].nextsector >= 0 && sector[wall[w].nextsector].floorz - f > (64<<8) && EdgeTypePlan(w) != kEdgeBlocked;
        if (bAbove)
            AddVantage(sectCenter[s].x, sectCenter[s].y, s), nLedges++;
    }
    for (int i = 0; i < nItemSpots; i++)
        AddVantage(itemSpot[i].x, itemSpot[i].y, itemSpotSect[i]);
    for (int i = 0; i < nCampSpots; i++)
        AddVantage(campSpot[i].x, campSpot[i].y, campSect[i]);
    if (gBotDebug == 2)
        for (int i = 0; i < nVantageSpots; i++)
            LOG_F(INFO, "Bot vantage spot %d: %d,%d sector %d value %d height %d", i, vantageSpot[i].x, vantageSpot[i].y, vantageSect[i], vantageValue[i], vantageHeight[i]);
}

// Drops that a player can't climb back up from (only a lift or nothing gets you out): pits.
static void FindPits(void)
{
    Bmemset(bPitWall, 0, sizeof(bPitWall));
    Bmemset(bPitSect, 0, sizeof(bPitSect));
    static short path[kBotMaxPath];
    bStrictLifts = true;
    int nChecked = 0;
    for (int w = 0; w < numwalls && nChecked < 400; w++)
    {
        const int n = wall[w].nextsector;
        if (n < 0 || EdgeType(w) == kEdgeBlocked)
            continue;
        const int s = sectorofwall(w);
        const int x = (wall[w].x + wall[wall[w].point2].x) >> 1, y = (wall[w].y + wall[wall[w].point2].y) >> 1;
        if (getflorzofslope(n, x, y) - getflorzofslope(s, x, y) <= (54<<8))
            continue; // can jump back up
        if (sector[n].extra > 0 && xsector[sector[n].extra].Underwater)
            continue; // swim up
        nChecked++;
        if (PathSearch(NULL, n, sectCenter[n].x, sectCenter[n].y, s, x, y, path, kBotMaxPath, NULL) < 0)
        {
            bPitWall[w] = 1;
            bPitSect[n] = 1;
            if (gBotDebug == 2)
                LOG_F(INFO, "Bot pit: opening %d from sector %d down to sector %d", w, s, n);
        }
    }
    bStrictLifts = false;
}

static void BotFindSpots(void)
{
    nCampSpots = 0;
    nItemSpots = 0;
    Bmemset(nWallFails, 0, sizeof(nWallFails));
    Bmemset(nItemFails, 0, sizeof(nItemFails));
    Bmemset(nCampFails, 0, sizeof(nCampFails));
    FindDoors();
    for (int s = 0; s < numsectors; s++)
    {
        int x = 0, y = 0;
        const int nWalls = sector[s].wallnum;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + nWalls; w++)
            x += wall[w].x, y += wall[w].y;
        sectCenter[s].x = nWalls ? x / nWalls : 0;
        sectCenter[s].y = nWalls ? y / nWalls : 0;
    }
    for (int i = headspritestat[kStatItem]; i >= 0 && nItemSpots < kBotMaxItemSpots; i = nextspritestat[i])
    {
        itemSpot[nItemSpots].x = sprite[i].x;
        itemSpot[nItemSpots].y = sprite[i].y;
        itemSpotSect[nItemSpots] = sprite[i].sectnum;
        nItemSpots++;
    }
    // camping spots: inner corners of rooms with at least two ways in, watching the openings
    uint32_t nRand = 0x9e3779b9u ^ (uint32_t)numsectors;
    for (int nTry = 0; nTry < numsectors * 2 && nCampSpots < kBotMaxCampSpots; nTry++)
    {
        nRand ^= nRand << 13, nRand ^= nRand >> 17, nRand ^= nRand << 5;
        const int s = nRand % (uint32_t)numsectors;
        if (sector[s].wallnum < 4 || IsHurtSector(s))
            continue;
        if (sector[s].extra > 0 && xsector[sector[s].extra].Underwater)
            continue;
        int nPortals = 0;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
            if (EdgeType(w) != kEdgeBlocked)
                nPortals++;
        if (nPortals < 2)
            continue;
        const int w = sector[s].wallptr + (nRand >> 8) % (uint32_t)sector[s].wallnum;
        // step from the corner towards the room's middle
        const int cx = wall[w].x, cy = wall[w].y;
        const int nAng = getangle(sectCenter[s].x - cx, sectCenter[s].y - cy);
        const int x = cx + mulscale30(Cos(nAng), 220), y = cy + mulscale30(Sin(nAng), 220);
        if (!inside(x, y, s))
            continue;
        const int z = getflorzofslope(s, x, y);
        if (getceilzofslope(s, x, y) > z - (56<<8))
            continue;
        bool bTooClose = false;
        for (int i = 0; i < nCampSpots && !bTooClose; i++)
            bTooClose = BotDist(campSpot[i].x, campSpot[i].y, x, y) < 1500;
        if (bTooClose)
            continue;
        campSpot[nCampSpots].x = x;
        campSpot[nCampSpots].y = y;
        campSpot[nCampSpots].z = z;
        campSect[nCampSpots] = s;
        campAng[nCampSpots] = nAng;
        nCampSpots++;
    }
    FindVantageSpots();
    FindPits();
}

// "mixed": the personalities in a random order, so the bots of a match all differ
static int nMixedOrder[kBotMaxBots] = { kBotPersonaBalanced, kBotPersonaRusher, kBotPersonaCamper, kBotPersonaTrickster, kBotPersonaCoward, kBotPersonaJumper, kBotPersonaBalanced };

static void ShuffleMixed(uint32_t nSeed)
{
    static const int nAll[kBotPersonaMax - 1] = { kBotPersonaBalanced, kBotPersonaRusher, kBotPersonaCamper, kBotPersonaTrickster, kBotPersonaCoward, kBotPersonaJumper };
    int nOrder[ARRAY_SSIZE(nAll)];
    for (int i = 0; i < ARRAY_SSIZE(nAll); i++)
        nOrder[i] = nAll[i];
    for (int i = ARRAY_SSIZE(nAll) - 1; i > 0; i--)
    {
        nSeed = nSeed * 1103515245u + 12345u;
        const int j = (nSeed >> 16) % (i + 1);
        swap(&nOrder[i], &nOrder[j]);
    }
    for (int i = 0; i < kBotMaxBots; i++)
        nMixedOrder[i] = nOrder[i % ARRAY_SSIZE(nAll)];
}

static void BotResolveSettings(BOT *pBot, int nBot)
{
    int nSkill = gBotEachSkill[nBot] >= 0 ? gBotEachSkill[nBot] : gBotSkill;
    int nPersona = gBotEachPersona[nBot] >= 0 ? gBotEachPersona[nBot] : gBotPersona;
    nSkill = ClipRange(nSkill, 0, kBotSkillMax - 1);
    nPersona = ClipRange(nPersona, 0, kBotPersonaMax - 1);
    if (nPersona == kBotPersonaMixed)
        nPersona = nMixedOrder[nBot % ARRAY_SSIZE(nMixedOrder)];
    pBot->nSkill = nSkill;
    pBot->nPersona = nPersona;
    pBot->skill = gBotSkillInfo[nSkill];
    pBot->persona = gBotPersonaInfo[nPersona];
    // the all-bots fine tuning, times this bot's own (both in percent)
    const int *pTune = gBotEachTune[ClipRange(nBot, 0, kBotMaxBots-1)];
    auto Tune = [pTune](int nAll, int nWhich) { return ClipRange(nAll, 0, 300) * ClipRange(pTune[nWhich], 0, 300) / 100; };
    BOTSKILL *pSkill = &pBot->skill;
    pSkill->nAimError = pSkill->nAimError * 100 / ClipRange(Tune(gBotAccuracy, kBotTuneAim), 10, 900);
    pSkill->nReaction = pSkill->nReaction * ClipRange(Tune(gBotReaction, kBotTuneReaction), 10, 900) / 100;
    BOTPERSONA *pPersona = &pBot->persona;
    pPersona->nAggression = ClipRange(pPersona->nAggression * Tune(gBotAggression, kBotTuneAggression) / 100, 0, 100);
    pPersona->nJump = ClipRange(pPersona->nJump * Tune(gBotJumping, kBotTuneJumping) / 100, 0, 100);
    pPersona->nAmbush = ClipRange(pPersona->nAmbush * Tune(gBotAmbush, kBotTuneAmbush) / 100, 0, 100);
    pPersona->nFlank = ClipRange(pPersona->nFlank * Tune(gBotFlank, kBotTuneFlank) / 100, 0, 100);
    pPersona->nCamping = ClipRange(pPersona->nCamping * Tune(gBotCamping, kBotTuneCamping) / 100, 0, 100);
    pPersona->nHighGround = ClipRange(pPersona->nHighGround * Tune(gBotHighGround, kBotTuneHighGround) / 100, 0, 100);
}

// a bot's player profile: its name, look, how much damage it takes
static void BotMakeProfile(BOT *pBot, int nBot, int nRandomName, PROFILE *pProfile)
{
    Bmemset(pProfile, 0, sizeof(PROFILE));
    if (gBotEachName[nBot][0])
        Bstrncpyz(pProfile->name, gBotEachName[nBot], sizeof(pProfile->name));
    else
        Bstrncpyz(pProfile->name, pzBotNames[nRandomName % ARRAY_SSIZE(pzBotNames)], sizeof(pProfile->name));
    pProfile->nAutoAim = pBot->skill.nAutoAim;
    pProfile->nWeaponSwitch = 1;
    pProfile->bWeaponFastSwitch = 0;
    pProfile->skill = ClipRange(gBotEachHandicap[nBot] >= 0 ? gBotEachHandicap[nBot] : gBotHandicap, 0, 4);
    pProfile->nTeamPreference = 0;
    pProfile->nColorPreference = 0;
    pProfile->nModel = gBotCultists ? (nBot & 1) : 0;
    pProfile->nWeaponHBobbing = 1;
}

// the next random name that no other bot of the match has (nor took as its own name)
static int BotPickName(int *pnNext, uint32_t *puUsed)
{
    for (int nTries = 0; nTries < ARRAY_SSIZE(pzBotNames); nTries++)
    {
        const int n = (*pnNext)++ % ARRAY_SSIZE(pzBotNames);
        bool bTaken = (*puUsed >> n) & 1;
        for (int i = 0; i < kBotMaxBots && !bTaken; i++)
            bTaken = !Bstrcasecmp(gBotEachName[i], pzBotNames[n]);
        if (!bTaken)
        {
            *puUsed |= 1u << n;
            return n;
        }
    }
    return 0;
}

bool botIsBot(int nPlayer)
{
    return (gBotMatch || gBotNet) && nPlayer >= 0 && nPlayer < kMaxPlayers && gBot[nPlayer].bActive;
}

bool botAny(void)
{
    return gBotMatch || gBotNet;
}

// the host of a network game thinks for the bots; everybody else only sees them
static bool BotThinksHere(void)
{
    return gBotMatch || (gBotNet && myconnectindex == connecthead);
}

static int FirstBot(void)
{
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (botIsBot(p))
            return p;
    return -1;
}

const char *botStateName(int nPlayer)
{
    if (!botIsBot(nPlayer))
        return "";
    return pzBotStateNames[gBot[nPlayer].nState];
}

void botSetupMatch(void)
{
    if (gBotTuning < 2)
    {
        // the presets now are what used to be fun with the sliders: start the sliders afresh
        gBotAccuracy = gBotReaction = gBotAggression = gBotJumping = gBotAmbush = gBotFlank = gBotCamping = gBotHighGround = 100;
        gBotTuning = 2;
    }
    const CAREERSTAGE *pStage = gBotCareer > 0 ? &gCareer[ClipRange(gBotCareer, 1, kCareerStages) - 1] : NULL;
    int nBots = ClipRange(gBotCount, 1, kMaxPlayers - 1);
    if (pStage)
    {
        nBots = 0;
        while (nBots < 6 && pStage->pzNames[nBots])
            nBots++;
    }
    gBotMatch = true;
    gBotNet = false;
    nRosterBots = 0;
    bOrderByHost = false;
    myconnectindex = connecthead = 0;
    numplayers = 1; // still a local game as far as the network code is concerned
    gNetPlayers = gInitialNetPlayers = nBots + 1;
    for (int i = 0; i < kMaxPlayers; i++)
        connectpoint2[i] = (i + 1 < gNetPlayers) ? i + 1 : -1;
    netBroadcastPlayerInfo(myconnectindex);
    gProfileNet[myconnectindex].skill = gProfile[myconnectindex].skill = gSkill;
    if (gBotWatchMode)
    {
        // watching: the human is a spectator (Blood's own spectating), the camera goes from bot to bot
        Bstrncpyz(gProfileNet[myconnectindex].name, "spectator", sizeof(gProfileNet[myconnectindex].name));
        gProfile[myconnectindex] = gProfileNet[myconnectindex];
        gPacketStartGame.uNetGameFlags |= kNetGameFlagSpectatingAllow;
    }
    uint32_t nSeed = ((uint32_t)totalclock + (uint32_t)time(NULL)) * 2654435761u + 12345u;
    int nNextName = (nSeed >> 8) % ARRAY_SSIZE(pzBotNames);
    uint32_t uUsedNames = 0;
    ShuffleMixed(nSeed);
    for (int i = 0; i < kMaxPlayers; i++)
    {
        BOT *pBot = &gBot[i];
        Bmemset(pBot, 0, sizeof(BOT));
        if (i == myconnectindex || i >= gNetPlayers)
            continue;
        const int nBot = i - 1;
        pBot->bActive = true;
        pBot->nPlayer = i;
        pBot->nEach = nBot;
        pBot->nRand = nSeed ^ (0x51ed270bu * (i + 1));
        if (!pBot->nRand)
            pBot->nRand = 1;
        BotResolveSettings(pBot, nBot);
        PROFILE *pProfile = &gProfileNet[i];
        if (pStage)
        {
            // the career's opponents: always the same names, how they play and how good they are
            pBot->nSkill = pStage->nSkill;
            pBot->nPersona = pStage->nPersonas[nBot];
            pBot->skill = gBotSkillInfo[pBot->nSkill];
            pBot->persona = gBotPersonaInfo[pBot->nPersona];
            BotMakeProfile(pBot, nBot, 0, pProfile);
            Bstrncpyz(pProfile->name, pStage->pzNames[nBot], sizeof(pProfile->name));
            pProfile->skill = ClipRange(gBotHandicap, 0, 4);
        }
        else
            BotMakeProfile(pBot, nBot, BotPickName(&nNextName, &uUsedNames), pProfile);
        gProfile[i] = *pProfile;
        LOG_F(INFO, "Bot %d: %s, %s, %s", nBot + 1, pProfile->name, gBotSkillNames[pBot->nSkill], gBotPersonaNames[pBot->nPersona]);
    }
    gGameOptions.nGameType = gPacketStartGame.gameType;
    gGameOptions.uGameFlags &= ~(kGameFlagContinuing|kGameFlagEnding);
    gPlayerRoundEnding = 0;
    UpdateNetworkMenus(); // NEW GAME in the game's menu now sets up the next bot match
    nBotOrder = gGameOptions.nGameType == kGameTypeCoop ? kBotOrderFollow : kBotOrderFree;
    LOG_F(INFO, "Bot match: game type %d, mode %d, %d bots", (int)gGameOptions.nGameType, (int)gPacketStartGame.nChosenMode, nBots);
    nHoldSect = -1;
    nAdapt = 100;
}

void botEndMatch(void)
{
    gBotWatchMode = false;
    gBotCareer = 0;
    if (!gBotMatch)
        return;
    gBotMatch = false;
    for (int i = 0; i < kMaxPlayers; i++)
        gBot[i].bActive = false;
    netResetToSinglePlayer();
}

//
// bots in network games
//

int botPeople(int *pnPlayers)
{
    int n = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (botIsBot(p))
            continue;
        if (pnPlayers)
            pnPlayers[n] = p;
        n++;
    }
    return n;
}

int botNetMaxBots(void)
{
    return ClipRange(kMaxPlayers - botPeople(NULL), 0, kBotMaxBots);
}

static uint32_t nNetSeed;

bool botNetSetup(void)
{
    nRosterBots = 0;
    if (myconnectindex != connecthead)
        return false;
    int nWant = ClipRange(gBotNetCount, 0, botNetMaxBots());
    if (nWant > 0 && gPacketMode != PACKETMODE_1)
    {
        viewSetMessage("Bots need the normal network mode (not -broadcast)", 8, MESSAGE_PRIORITY_NORMAL);
        nWant = 0;
    }
    if (gBotTuning < 2)
    {
        gBotAccuracy = gBotReaction = gBotAggression = gBotJumping = gBotAmbush = gBotFlank = gBotCamping = gBotHighGround = 100;
        gBotTuning = 2;
    }
    // free player numbers for them, after the people
    bool bTaken[kMaxPlayers] = {};
    int nPeople[kMaxPlayers];
    const int nPeopleCount = botPeople(nPeople);
    for (int i = 0; i < nPeopleCount; i++)
        bTaken[nPeople[i]] = true;
    nNetSeed = ((uint32_t)totalclock + (uint32_t)time(NULL)) * 2654435761u + 12345u;
    ShuffleMixed(nNetSeed);
    int nNextName = (nNetSeed >> 8) % ARRAY_SSIZE(pzBotNames);
    uint32_t uUsedNames = 0;
    for (int nBot = 0, p = 0; nBot < nWant; nBot++)
    {
        while (p < kMaxPlayers && bTaken[p])
            p++;
        if (p >= kMaxPlayers)
            break;
        BOT bot;
        Bmemset(&bot, 0, sizeof(bot));
        BotResolveSettings(&bot, nBot);
        nRosterPlayer[nRosterBots] = p;
        nRosterBot[nRosterBots] = nBot;
        BotMakeProfile(&bot, nBot, BotPickName(&nNextName, &uUsedNames), &rosterProfile[nRosterBots]);
        nRosterBots++;
        p++;
    }
    gPacketStartGame.nBots = nRosterBots;
    return true;
}

void botNetSendRoster(void)
{
    if (numplayers < 2)
        return;
    char *pPacket = packet;
    PutPacketByte(pPacket, 242);
    PutPacketByte(pPacket, nRosterBots);
    for (int i = 0; i < nRosterBots; i++)
    {
        PutPacketByte(pPacket, nRosterPlayer[i]);
        PutPacketBuffer(pPacket, &rosterProfile[i], sizeof(PROFILE));
    }
    netSendPacketAll(packet, pPacket - packet);
}

void botNetNoBots(void)
{
    if (gNetMode == NETWORK_NONE || myconnectindex != connecthead)
        return;
    nRosterBots = 0;
    gPacketStartGame.nBots = 0;
    Bmemset(gPacketStartGame.nChosenTeam, 0, sizeof(gPacketStartGame.nChosenTeam));
    botApplyRoster();
    botNetSendRoster();
}

void botReceiveRoster(char *pPacket)
{
    nRosterBots = ClipRange(GetPacketByte(pPacket), 0, kBotMaxBots);
    for (int i = 0; i < nRosterBots; i++)
    {
        nRosterPlayer[i] = ClipRange(GetPacketByte(pPacket), 0, kMaxPlayers-1);
        nRosterBot[i] = i;
        GetPacketBuffer(pPacket, &rosterProfile[i], sizeof(PROFILE));
        rosterProfile[i].name[MAXPLAYERNAME-1] = '\0';
    }
}

void botApplyRoster(void)
{
    // the people stay in the order they joined, the bots come after them
    int nOrder[kMaxPlayers];
    int n = botPeople(nOrder);
    bool bTaken[kMaxPlayers] = {};
    for (int i = 0; i < n; i++)
        bTaken[nOrder[i]] = true;
    for (int i = 0; i < kMaxPlayers; i++)
        gBot[i].bActive = false;
    const bool bHost = myconnectindex == connecthead;
    int nBots = 0;
    for (int i = 0; i < nRosterBots && n < kMaxPlayers; i++)
    {
        const int p = nRosterPlayer[i];
        if (bTaken[p])
            continue; // can't happen: the host gave it a free number
        bTaken[p] = true;
        nOrder[n++] = p;
        BOT *pBot = &gBot[p];
        Bmemset(pBot, 0, sizeof(BOT));
        pBot->bActive = true;
        pBot->nPlayer = p;
        pBot->nEach = nRosterBot[i];
        if (bHost)
        {
            pBot->nRand = nNetSeed ^ (0x51ed270bu * (p + 1));
            if (!pBot->nRand)
                pBot->nRand = 1;
            BotResolveSettings(pBot, pBot->nEach);
        }
        gProfile[p] = gProfileNet[p] = rosterProfile[i];
        nBots++;
        LOG_F(INFO, "Bot %d: %s (player %d)%s", i + 1, rosterProfile[i].name, p + 1, bHost ? "" : ", played by the host");
    }
    connecthead = nOrder[0];
    for (int i = 0; i < n; i++)
        connectpoint2[nOrder[i]] = i + 1 < n ? nOrder[i + 1] : -1;
    gNetPlayers = n;
    gBotNet = nBots > 0;
    if (gBotNet)
    {
        bOrderByHost = false;
        nBotOrder = gPacketStartGame.gameType == kGameTypeCoop ? kBotOrderFollow : kBotOrderFree;
        nHoldSect = -1;
        nAdapt = 100;
    }
    UpdateNetworkMenus();
}

void botNetReset(void)
{
    if (!gBotNet && !nRosterBots)
        return;
    gBotNet = false;
    nRosterBots = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        gBot[i].bActive = false;
}

void botNetInput(void)
{
    if (!gBotNet || myconnectindex != connecthead)
        return;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (!botIsBot(p))
            continue;
        botGetInput(p, &gFifoInput[gNetFifoHead[p]&255][p]);
        gNetFifoHead[p]++;
    }
}

void botReceiveTalk(char *pPacket)
{
    const int nPlayer = ClipRange(GetPacketByte(pPacket), 0, kMaxPlayers-1);
    const int nSound = GetPacketWord(pPacket);
    char zText[64];
    Bstrncpyz(zText, pPacket, sizeof(zText));
    if (zText[0])
    {
        char buffer[128];
        Bsnprintf(buffer, sizeof(buffer), "%s: %s", gProfile[nPlayer].name, zText);
        viewSetMessage(buffer, 0, MESSAGE_PRIORITY_NORMAL);
    }
    if (nSound > 0 && gPlayer[nPlayer].pSprite && gPlayer[nPlayer].pXSprite && gPlayer[nPlayer].pXSprite->health > 0)
        sfxPlay3DSound(gPlayer[nPlayer].pSprite, nSound, -1, 0);
}

void botResolveTeams(char *pnTeams)
{
    Bmemset(pnTeams, 0, kMaxPlayers);
    if (gPacketStartGame.gameType != kGameTypeTeams)
        return;
    int nCount[2] = { 0, 0 };
    // the people: their pick in SET TEAMS, otherwise as before (auto teams or their own preference)
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (botIsBot(p))
            continue;
        const int nPick = ClipRange(gHumanTeam[p], kBotTeamAuto, kBotTeamRed);
        if (nPick != kBotTeamAuto)
        {
            pnTeams[p] = nPick;
            nCount[nPick - 1]++;
        }
        else
            nCount[((gPacketStartGame.bAutoTeams || !gProfile[p].nTeamPreference) ? p : gProfile[p].nTeamPreference - 1) & 1]++;
    }
    // the bots: their pick, otherwise the smaller team
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (!botIsBot(p))
            continue;
        int nPick = ClipRange(gBotEachTeam[gBot[p].nEach], kBotTeamAuto, kBotTeamRed);
        if (nPick == kBotTeamAuto)
            nPick = nCount[0] <= nCount[1] ? kBotTeamBlue : kBotTeamRed;
        pnTeams[p] = nPick;
        nCount[nPick - 1]++;
    }
}

void botDescribeEach(int nBot, char *pzBuffer, int nSize)
{
    nBot = ClipRange(nBot, 0, kBotMaxBots-1);
    const int nSkill = gBotEachSkill[nBot] >= 0 ? gBotEachSkill[nBot] : gBotSkill;
    const int nPersona = gBotEachPersona[nBot] >= 0 ? gBotEachPersona[nBot] : gBotPersona;
    char zName[24];
    if (gBotEachName[nBot][0])
        Bstrncpyz(zName, gBotEachName[nBot], sizeof(zName));
    else
        Bsnprintf(zName, sizeof(zName), "BOT %d", nBot + 1);
    Bsnprintf(pzBuffer, nSize, "%s - %s %s", zName, gBotSkillNames[ClipRange(nSkill, 0, kBotSkillMax-1)], gBotPersonaNames[ClipRange(nPersona, 0, kBotPersonaMax-1)]);
    Bstrupr(pzBuffer);
}

static int osdcmd_bot_sector(osdcmdptr_t parm);

void botLevelStart(void)
{
    if (!BotThinksHere())
        return;
    BotFindSpots();
    if (const char *pzTx = getenv("NOTBLOOD_BOT_TX")) // testing: who sends to this channel
    {
        const int nTx = atoi(pzTx);
        for (int w = 0; w < numwalls; w++)
            if (wall[w].extra > 0 && xwall[wall[w].extra].txID == nTx)
                LOG_F(INFO, "tx %d: wall %d of sector %d push %d", nTx, w, sectorofwall(w), xwall[(int)wall[w].extra].triggerPush);
        for (int s = 0; s < numsectors; s++)
            if (sector[s].extra > 0 && xsector[sector[s].extra].txID == nTx)
                LOG_F(INFO, "tx %d: sector %d type %d enter %d push %d", nTx, s, (int)sector[s].type, xsector[(int)sector[s].extra].Enter, xsector[(int)sector[s].extra].Push);
        for (int i = 0; i < kMaxSprites; i++)
            if (sprite[i].statnum < kMaxStatus && sprite[i].extra > 0 && xsprite[sprite[i].extra].txID == nTx)
                LOG_F(INFO, "tx %d: sprite %d type %d sector %d push %d", nTx, i, (int)sprite[i].type, (int)sprite[i].sectnum, xsprite[(int)sprite[i].extra].Push);
    }
    if (const char *pzPath = getenv("NOTBLOOD_BOT_PATH")) // testing: "from,to" sectors: the way bots plan
    {
        int a = 0, b = 0;
        if (sscanf(pzPath, "%d,%d", &a, &b) == 2 && a >= 0 && b >= 0 && a < numsectors && b < numsectors)
        {
            short path[kBotMaxPath];
            int nCost = 0;
            const int n = PathSearch(NULL, a, sectCenter[a].x, sectCenter[a].y, b, sectCenter[b].x, sectCenter[b].y, path, kBotMaxPath, &nCost);
            LOG_F(INFO, "path %d -> %d: %d openings, cost %d", a, b, n, nCost);
            for (int i = 0; i < n; i++)
                LOG_F(INFO, "  opening %d: %d -> %d plan %d", path[i], sectorofwall(path[i]), (int)wall[path[i]].nextsector, EdgeTypePlan(path[i]));
        }
    }
    if (const char *pzDump = getenv("NOTBLOOD_BOT_SECTORS")) // testing: dump these sectors (comma separated)
    {
        char buffer[256];
        Bstrncpyz(buffer, pzDump, sizeof(buffer));
        for (char *pzTok = strtok(buffer, ","); pzTok; pzTok = strtok(NULL, ","))
        {
            const char *parms[1] = { pzTok };
            osdfuncparm_t parm = { 1, "bot_sector", parms, "" };
            osdcmd_bot_sector(&parm);
        }
    }
    BotClearPenalties();
    for (int i = 0; i < kMaxPlayers; i++)
    {
        BOT *pBot = &gBot[i];
        if (!pBot->bActive)
            continue;
        pBot->nState = kBotStateRoam;
        pBot->nStateTime = 0;
        pBot->nNextThink = 0;
        pBot->nTarget = -1;
        pBot->nPathLen = pBot->nPathPos = 0;
        pBot->nGoalSect = -1;
        pBot->nGoalSprite = -1;
        pBot->nStuckCheck = 0;
        pBot->nStuckCount = 0;
        pBot->nLastHealth = gPlayer[i].pXSprite ? gPlayer[i].pXSprite->health : 0;
        pBot->nLastFrags = gPlayer[i].fragCount;
        pBot->nLastDeaths = 0;
        pBot->nTalkAt = 0;
        pBot->pzTalk = NULL;
        pBot->nWeaponNext = 0;
        pBot->nCampSpot = -1;
        pBot->nLastCamp = 0;
        pBot->nVantage = -1;
        pBot->nHoldUntil = 0;
        pBot->nLastHighTry = 0;
        pBot->nLiftWall = -1;
        pBot->bDetour = false;
        pBot->nFirePauseUntil = 0;
        pBot->nAimErrZ = 0;
        pBot->nFireHold = pBot->nFireRelease = 0;
        pBot->bWasDead = false;
        for (int j = 0; j < kBotMaxBlocked; j++)
            pBot->blockedUntil[j] = 0;
        for (int j = 0; j < 4; j++)
            pBot->ignoreUntil[j] = 0;
        pBot->nAtGoalSince = 0;
        Bmemset(pBot->mem, 0, sizeof(pBot->mem));
        Bmemset(pBot->nHurt, 0, sizeof(pBot->nHurt));
        for (int j = 0; j < kMaxPlayers; j++)
            pBot->nEnemyHealth[j] = 100;
        pBot->nLastKill = pBot->nLastPain = pBot->nLastShot = pBot->nThirdPartyUntil = 0;
    }
}

//
// weapons
//

struct BOTWEAPON
{
    int nPower;
    int nMinDist, nBestDist, nMaxDist;
    int nCone;      // fire when the aim is this close (angle units)
    int nLeadSpeed; // like gWeaponTrack's predict value, 0 for hitscan
};

static const BOTWEAPON gBotWeapon[kWeaponMax] = {
    { 0, 0, 0, 0, 0, 0 },                     // none
    { 12, 0, 0, 900, 140, 0 },                // pitchfork
    { 45, 300, 3000, 12000, 70, 0x2aaaaa },   // flare gun
    { 75, 0, 1100, 4000, 60, 0 },             // shotgun
    { 65, 0, 2500, 10000, 50, 0 },            // tommy gun
    { 85, 1400, 4500, 14000, 70, 0x2aaaaa },  // napalm
    { 70, 1400, 3200, 6000, 140, 0 },         // TNT
    { 55, 0, 700, 1700, 140, 0 },             // spray can
    { 85, 0, 3000, 10000, 55, 0x255555 },     // tesla
    { 80, 600, 3500, 12000, 70, 0x2aaaaa },   // life leech
    { 55, 0, 3000, 25000, 400, 0 },           // voodoo doll
    { 0, 0, 0, 0, 0, 0 },                     // proximity TNT
    { 0, 0, 0, 0, 0, 0 },                     // remote TNT
    { 90, 0, 0, 900, 140, 0 },                // beast hands
};

static bool bTargetAbove; // the enemy is up high: splash weapons drive it out of there

static int WeaponScore(PLAYER *pPlayer, int nWeapon, int nDist)
{
    const BOTWEAPON *pInfo = &gBotWeapon[nWeapon];
    if (!pInfo->nPower)
        return 0;
    if (nWeapon == kWeaponBeast)
        return pPlayer->curWeapon == kWeaponBeast ? 1000 : 0;
    if (!WeaponIsEquipable(pPlayer, nWeapon))
        return 0;
    int nScore = pInfo->nPower * 10;
    if (nDist < pInfo->nMinDist)
        nScore /= 8; // it would hurt itself
    else if (nDist > pInfo->nMaxDist)
        nScore /= 4;
    else
    {
        const int nOff = klabs(nDist - pInfo->nBestDist);
        nScore -= nScore * ClipHigh(nOff, 6000) / 20000;
    }
    if (bTargetAbove && (nWeapon == kWeaponNapalm || nWeapon == kWeaponTNT || nWeapon == kWeaponFlare))
        nScore += nScore / 4;
    if (powerupCheck(pPlayer, kPwUpTwoGuns) && (nWeapon == kWeaponFlare || nWeapon == kWeaponShotgun || nWeapon == kWeaponTommy || nWeapon == kWeaponNapalm || nWeapon == kWeaponTesla))
        nScore += nScore / 3;
    return nScore;
}

static int BestWeapon(PLAYER *pPlayer, int nDist, int *pScore)
{
    int nBest = 0, nBestScore = 0;
    for (int w = kWeaponPitchfork; w < kWeaponMax; w++)
    {
        const int nScore = WeaponScore(pPlayer, w, nDist);
        if (nScore > nBestScore)
            nBest = w, nBestScore = nScore;
    }
    if (pScore)
        *pScore = nBestScore;
    return nBest;
}

static int PlayerPower(PLAYER *pPlayer) // a rough idea of how dangerous someone is, as a player could judge it
{
    int nPower = gBotWeapon[ClipRange(pPlayer->curWeapon, 0, kWeaponMax-1)].nPower;
    if (powerupCheck(pPlayer, kPwUpTwoGuns))
        nPower += nPower / 2;
    if (powerupCheck(pPlayer, kPwUpDeathMask))
        nPower += 100;
    return ClipLow(nPower, 10);
}

//
// items worth walking to
//

static int ItemWeapon(int nType)
{
    switch (nType)
    {
    case kItemWeaponSawedoff: case kItemAmmoSawedoffFew: case kItemAmmoSawedoffBox: return kWeaponShotgun;
    case kItemWeaponTommygun: case kItemAmmoTommygunFew: case kItemAmmoTommygunDrum: return kWeaponTommy;
    case kItemWeaponFlarePistol: case kItemAmmoFlares: return kWeaponFlare;
    case kItemWeaponVoodooDoll: case kItemAmmoVoodooDoll: return kWeaponVoodoo;
    case kItemWeaponTeslaCannon: case kItemAmmoTeslaCharge: return kWeaponTesla;
    case kItemWeaponNapalmLauncher: case kItemAmmoGasolineCan: return kWeaponNapalm;
    case kItemWeaponSprayCan: case kItemAmmoSprayCan: return kWeaponSprayCan;
    case kItemWeaponTNT: case kItemAmmoTNTBundle: case kItemAmmoTNTBox: return kWeaponTNT;
    case kItemWeaponLifeLeech: case kItemAmmoTrappedSoul: return kWeaponLifeLeech;
    }
    return 0;
}

static int ItemValue(PLAYER *pPlayer, spritetype *pItem)
{
    const int nType = pItem->type;
    const int nHealth = pPlayer->pXSprite->health >> 4;
    if (nType >= kItemWeaponBase && nType < kItemWeaponMax)
    {
        const int w = ItemWeapon(nType);
        if (!w)
            return 0;
        if (!pPlayer->hasWeapon[w])
            return 40 + gBotWeapon[w].nPower;
        return WeaponIsEquipable(pPlayer, w) ? 8 : 35;
    }
    if (nType >= kItemAmmoBase && nType < kItemAmmoMax)
    {
        const int w = ItemWeapon(nType);
        if (!w || !pPlayer->hasWeapon[w])
            return 4;
        return WeaponIsEquipable(pPlayer, w) ? 12 : 40;
    }
    switch (nType)
    {
    case kItemHealthMedPouch:
    case kItemHealthLifeEssense:
    case kItemHealthRedPotion:
    case kItemHealthDoctorBag:
        return nHealth >= 100 ? 0 : (100 - nHealth) * 3 / 2;
    case kItemHealthLifeSeed:
        return 60 + ClipLow(100 - nHealth, 0);
    case kItemTwoGuns:
        return 110;
    case kItemDeathMask:
        return 100;
    case kItemShadowCloak:
        return 70;
    case kItemReflectShots:
        return 60;
    }
    if (nType >= kItemArmorAsbest && nType <= kItemArmorSuper)
    {
        const int nArmor = (pPlayer->armor[0] + pPlayer->armor[1] + pPlayer->armor[2]) >> 4;
        return nArmor < 100 ? 40 - nArmor / 4 : 5;
    }
    if (nType >= kItemFeatherFall && nType <= kItemBeastVision)
        return 15;
    return 0;
}

static bool ItemIgnored(BOT *pBot, int nSprite)
{
    if (nItemFails[nSprite] >= 2)
        return true;
    for (int i = 0; i < 4; i++)
        if (pBot->ignoreSprite[i] == nSprite && pBot->ignoreUntil[i] > nNow)
            return true;
    return false;
}

static void IgnoreItem(BOT *pBot, int nSprite, bool bUnreachable = false)
{
    if (bUnreachable && nItemFails[nSprite] < 255)
        nItemFails[nSprite]++;
    int nSlot = 0;
    for (int i = 1; i < 4; i++)
        if (pBot->ignoreUntil[i] < pBot->ignoreUntil[nSlot])
            nSlot = i;
    pBot->ignoreSprite[nSlot] = nSprite;
    pBot->ignoreUntil[nSlot] = nNow + 30*20;
    pBot->nAtGoalSince = 0;
}

static int FindBestItem(BOT *pBot, PLAYER *pPlayer, int *pValue, int nAvoidX, int nAvoidY)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = 0;
    for (int i = headspritestat[kStatItem]; i >= 0; i = nextspritestat[i])
    {
        spritetype *pItem = &sprite[i];
        if ((pItem->cstat & CSTAT_SPRITE_INVISIBLE) || ItemIgnored(pBot, i))
            continue;
        int nValue = ItemValue(pPlayer, pItem);
        if (bPitSect[pItem->sectnum] && pPlayer->pSprite->sectnum != pItem->sectnum)
            nValue = nValue >= 100 ? nValue / 2 : 0; // down in a pit: only for something really good
        else if (pItem->z - pSprite->z > (96<<8) && nValue < 100)
            nValue = nValue * (100 - pBot->persona.nHighGround * 7 / 10) / 100; // far down: giving up the high ground for it
        if (nValue <= 0)
            continue;
        const int nDist = BotDist(pSprite->x, pSprite->y, pItem->x, pItem->y);
        if (nDist > 24000)
            continue;
        int nScore = nValue * 4000 / (2500 + nDist);
        if (nAvoidX != INT32_MIN && BotDist(nAvoidX, nAvoidY, pItem->x, pItem->y) < nDist) // closer to the enemy than to it
            nScore /= 4;
        if (nScore > nBestScore)
            nBest = i, nBestScore = nScore;
    }
    if (pValue)
        *pValue = nBestScore;
    return nBest;
}

//
// senses
//

static bool BotCanSee(BOT *pBot, PLAYER *pPlayer, spritetype *pTarget, bool bIgnoreFov)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int dx = pTarget->x - pSprite->x, dy = pTarget->y - pSprite->y;
    const int nDist = approxDist(dx, dy);
    if (nDist > 30000)
        return false;
    PLAYER *pTargetPlayer = SpritePlayer(pTarget->index);
    const bool bFiring = pTargetPlayer && (pTargetPlayer->input.buttonFlags.shoot || pTargetPlayer->input.buttonFlags.shoot2);
    if (!bIgnoreFov && nDist > 500)
    {
        int nFov = pBot->skill.nFov;
        if (bFiring)
            nFov += nFov / 3; // muzzle flash in the corner of the eye
        if (klabs(AngDiff(pSprite->ang, getangle(dx, dy))) > nFov)
            return false;
    }
    if (pTargetPlayer)
    {
        if (powerupCheck(pTargetPlayer, kPwUpShadowCloak) && !bFiring && nDist > 900)
            return false;
        const int nShade = sector[pTarget->sectnum].floorshade;
        if (nShade > 40 && !bFiring && nDist > 3500 && !pTargetPlayer->flashEffect)
            return false;
    }
    const int z1 = pPlayer->zView;
    const int z2 = SpriteCenterZ(pTarget);
    if (cansee(pSprite->x, pSprite->y, z1, pSprite->sectnum, pTarget->x, pTarget->y, z2, pTarget->sectnum))
        return true;
    int top, bottom;
    GetSpriteExtents(pTarget, &top, &bottom);
    return cansee(pSprite->x, pSprite->y, z1, pSprite->sectnum, pTarget->x, pTarget->y, top + (4<<8), pTarget->sectnum);
}

static void Remember(BOT *pBot, int nPlayer, spritetype *pTarget, bool bSeen, int nFuzz)
{
    BOTMEMORY *pMem = &pBot->mem[nPlayer];
    pMem->bValid = true;
    pMem->x = pTarget->x;
    pMem->y = pTarget->y;
    pMem->z = pTarget->z;
    pMem->nSector = pTarget->sectnum;
    if (nFuzz > 0)
    {
        short nSector = pTarget->sectnum;
        const int x = pTarget->x + BotRand(pBot, nFuzz*2) - nFuzz;
        const int y = pTarget->y + BotRand(pBot, nFuzz*2) - nFuzz;
        updatesector(x, y, &nSector);
        if (nSector >= 0)
            pMem->x = x, pMem->y = y, pMem->nSector = nSector;
    }
    pMem->vx = xvel[pTarget->index];
    pMem->vy = yvel[pTarget->index];
    pMem->nTime = nNow;
    pMem->bSeen = bSeen;
}

// Looks and listens. Returns the sprite it should fight (-1 none).
static int BotPerceive(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = 0x7fffffff;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pOther = &gPlayer[p];
        if (!IsEnemyPlayer(pPlayer, pOther))
            continue;
        if (pOther->pXSprite->health <= 0)
        {
            pBot->mem[p].bValid = false;
            continue;
        }
        spritetype *pTarget = pOther->pSprite;
        const int nDist = BotDist(pSprite->x, pSprite->y, pTarget->x, pTarget->y);
        const bool bTracking = pBot->nTarget == pTarget->index && nNow - pBot->mem[p].nTime < 6;
        if (BotCanSee(pBot, pPlayer, pTarget, bTracking))
        {
            Remember(pBot, p, pTarget, true, 0);
            int nScore = nDist;
            if (pTarget->index == pBot->nTarget)
                nScore -= 2000; // stick with who it is fighting
            if (modesJuggernaut() == p)
                nScore -= 3000; // everybody hunts the juggernaut
            if (gBotGrudges && pOther->fragInfo[pPlayer->nPlayer] - pPlayer->fragInfo[p] >= 2)
                nScore -= 1500; // it has a score to settle with that one
            if (pOther->hasFlag)
                nScore -= 2500; // stop the flag carrier
            if (nScore < nBestScore)
                nBest = pTarget->index, nBestScore = nScore;
            continue;
        }
        // hearing: shots carry far, jumps and footsteps less so
        const int nHear = pBot->skill.nHearing;
        const bool bShooting = pOther->input.buttonFlags.shoot || pOther->input.buttonFlags.shoot2;
        const int nSpeed = approxDist(xvel[pTarget->index], yvel[pTarget->index]);
        if (bShooting && nDist < nHear && pOther->curWeapon != kWeaponPitchfork && pOther->curWeapon != kWeaponVoodoo)
            Remember(pBot, p, pTarget, false, nDist / 10);
        else if (pOther->input.buttonFlags.jump && nDist < nHear / 3)
            Remember(pBot, p, pTarget, false, nDist / 8);
        else if (nSpeed > 0x30000 && nDist < nHear / 6 && !pOther->input.buttonFlags.crouch)
            Remember(pBot, p, pTarget, false, nDist / 6);
    }
    // monsters, in co-op or when the match has them
    if (gGameOptions.nGameType == kGameTypeCoop || gGameOptions.nMonsterSettings)
    {
        for (int i = headspritestat[kStatDude]; i >= 0; i = nextspritestat[i])
        {
            spritetype *pDude = &sprite[i];
            if (IsPlayerSprite(pDude) || !SpriteAlive(i) || (pDude->flags & 32))
                continue;
            const int nDist = BotDist(pSprite->x, pSprite->y, pDude->x, pDude->y);
            if (nDist > 9000)
                continue;
            if (!BotCanSee(pBot, pPlayer, pDude, pBot->nTarget == i))
                continue;
            int nScore = nDist + 1500; // players first
            if (i == pBot->nTarget)
                nScore -= 2000;
            if (nScore < nBestScore)
                nBest = i, nBestScore = nScore;
        }
    }
    return nBest;
}

static int RecentMemory(BOT *pBot, int nMaxAge) // the enemy player it knows the most recent thing about
{
    int nBest = -1, nBestTime = nNow - nMaxAge;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        BOTMEMORY *pMem = &pBot->mem[p];
        if (pMem->bValid && pMem->nTime > nBestTime)
            nBest = p, nBestTime = pMem->nTime;
    }
    return nBest;
}

//
// decisions
//

static void SetState(BOT *pBot, int nState)
{
    if (pBot->nState == nState)
        return;
    pBot->nState = nState;
    pBot->nStateTime = nNow;
    pBot->nPathLen = pBot->nPathPos = 0;
    pBot->nGoalSect = -1;
    pBot->nGoalSprite = -1;
    pBot->nWaitUntil = 0;
    pBot->bDetour = false;
}

static bool SetGoal(BOT *pBot, int x, int y, int z, int nSector)
{
    if (nSector < 0)
        return false;
    pBot->nGoalX = x;
    pBot->nGoalY = y;
    pBot->nGoalZ = z;
    pBot->nGoalSect = nSector;
    if (!BotFindPath(pBot, nSector, x, y))
    {
        pBot->nGoalSect = -1;
        return false;
    }
    return true;
}

static bool GoalReached(BOT *pBot, spritetype *pSprite, int nRange)
{
    if (pBot->nGoalSect < 0 || BotDist(pSprite->x, pSprite->y, pBot->nGoalX, pBot->nGoalY) >= nRange)
        return false;
    // right below or above it doesn't count (a ledge over a pit)
    return pSprite->sectnum == pBot->nGoalSect || klabs(pSprite->z - pBot->nGoalZ) < (48<<8);
}

// Somewhere to go when there is nothing better to do: places where items lie (where
// players go too), now and then a camping spot. Aggressive bots go looking for players,
// roughly where a player who knows the map and hears distant fighting would guess.
static bool PickRoamGoal(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    if (BotChance(pBot, 15 + pBot->persona.nAggression / 2))
    {
        int nCount = 0, nPick = -1;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (IsEnemyPlayer(pPlayer, &gPlayer[p]) && gPlayer[p].pXSprite->health > 0 && BotRand(pBot, ++nCount) == 0)
                nPick = p;
        if (nPick >= 0)
        {
            spritetype *pEnemy = gPlayer[nPick].pSprite;
            for (int nTry = 0; nTry < 4; nTry++)
            {
                const int nFuzz = 2500;
                const int x = pEnemy->x + BotRand(pBot, nFuzz*2) - nFuzz, y = pEnemy->y + BotRand(pBot, nFuzz*2) - nFuzz;
                short nSector = pEnemy->sectnum;
                updatesector(x, y, &nSector);
                if (nSector < 0)
                    continue;
                if (SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector))
                    return true;
            }
        }
    }
    for (int nTry = 0; nTry < 6; nTry++)
    {
        int x, y, nSector;
        if (nCampSpots > 0 && (nItemSpots == 0 || BotChance(pBot, 25)))
        {
            const int i = BotRand(pBot, nCampSpots);
            x = campSpot[i].x, y = campSpot[i].y, nSector = campSect[i];
        }
        else if (nItemSpots > 0)
        {
            const int i = BotRand(pBot, nItemSpots);
            x = itemSpot[i].x, y = itemSpot[i].y, nSector = itemSpotSect[i];
        }
        else
        {
            nSector = BotRand(pBot, numsectors);
            x = sectCenter[nSector].x, y = sectCenter[nSector].y;
            if (!inside(x, y, nSector))
                continue;
        }
        if (BotDist(pSprite->x, pSprite->y, x, y) < 1200 || (bPitSect[nSector] && pSprite->sectnum != nSector))
            continue;
        if (getflorzofslope(nSector, x, y) - pSprite->z > (96<<8) && BotChance(pBot, pBot->persona.nHighGround))
            continue; // rather not go down there
        if (SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector))
            return true;
    }
    return false;
}

static bool VantageTaken(BOT *pBot, int i);

static bool PickCampSpot(BOT *pBot, spritetype *pSprite)
{
    // the good high places first, as much as it likes them
    if (nVantageSpots > 0 && BotChance(pBot, pBot->persona.nHighGround))
    {
        int nBest = -1, nBestScore = 0;
        for (int i = 0; i < nVantageSpots; i++)
        {
            if (nVantageFails[i] >= 2 || VantageTaken(pBot, i))
                continue;
            const int nDist = BotDist(pSprite->x, pSprite->y, vantageSpot[i].x, vantageSpot[i].y);
            const int nScore = vantageValue[i] * 100 / (100 + nDist / 60) + BotRand(pBot, vantageValue[i] / 3 + 1);
            if (nScore > nBestScore)
                nBest = i, nBestScore = nScore;
        }
        if (nBest >= 0 && SetGoal(pBot, vantageSpot[nBest].x, vantageSpot[nBest].y, vantageSpot[nBest].z, vantageSect[nBest]))
        {
            pBot->nVantage = nBest;
            pBot->nCampSpot = -1;
            pBot->nLastCamp = nNow;
            pBot->nWatchAng = getangle(sectCenter[vantageSect[nBest]].x - vantageSpot[nBest].x, sectCenter[vantageSect[nBest]].y - vantageSpot[nBest].y);
            // look out over what it sees: towards the lower ground around
            int nLow = -1, nLowZ = vantageSpot[nBest].z;
            const int s = vantageSect[nBest];
            for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
                if (wall[w].nextsector >= 0 && sector[wall[w].nextsector].floorz > nLowZ)
                    nLow = wall[w].nextsector, nLowZ = sector[nLow].floorz;
            if (nLow >= 0)
                pBot->nWatchAng = getangle(sectCenter[nLow].x - vantageSpot[nBest].x, sectCenter[nLow].y - vantageSpot[nBest].y);
            return true;
        }
    }
    pBot->nVantage = -1;
    if (nCampSpots <= 0)
        return false;
    int nBest = -1, nBestScore = 0x7fffffff;
    for (int nTry = 0; nTry < 6; nTry++)
    {
        const int i = BotRand(pBot, nCampSpots);
        if (nCampFails[i] >= 2 || bPitSect[campSect[i]])
            continue;
        bool bTaken = false; // two bots in the same corner looks silly
        for (int j = 0; j < kMaxPlayers && !bTaken; j++)
            bTaken = gBot[j].bActive && &gBot[j] != pBot && gBot[j].nCampSpot == i && (gBot[j].nState == kBotStateCamp || gBot[j].nState == kBotStateAmbush);
        if (bTaken)
            continue;
        const int nScore = BotDist(pSprite->x, pSprite->y, campSpot[i].x, campSpot[i].y) + BotRand(pBot, 3000);
        if (nScore < nBestScore)
            nBest = i, nBestScore = nScore;
    }
    if (nBest < 0 || !SetGoal(pBot, campSpot[nBest].x, campSpot[nBest].y, campSpot[nBest].z, campSect[nBest]))
        return false;
    pBot->nWatchAng = campAng[nBest];
    pBot->nCampSpot = nBest;
    pBot->nLastCamp = nNow;
    return true;
}

// Wait beside the opening the enemy is expected to come through.
static bool PlanAmbush(BOT *pBot, PLAYER *pPlayer, BOTMEMORY *pMem)
{
    spritetype *pSprite = pPlayer->pSprite;
    short enemyPath[kBotMaxPath];
    int nEnemyCost;
    const int nLen = PathSearch(NULL, pMem->nSector, pMem->x, pMem->y, pSprite->sectnum, pSprite->x, pSprite->y, enemyPath, kBotMaxPath, &nEnemyCost);
    if (nLen <= 0)
        return false;
    // the last opening on its way that the bot can reach well before it
    for (int i = nLen - 1; i >= 0; i--)
    {
        const int w = enemyPath[i];
        const int nWaitSect = wall[w].nextsector;
        int dx, dy;
        PortalPoint(w, pMem->x, pMem->y, &dx, &dy);
        const int nMyDist = BotDist(pSprite->x, pSprite->y, dx, dy);
        const int nEnemyDist = BotDist(pMem->x, pMem->y, dx, dy);
        if (nMyDist > 5000)
            break;
        if (nEnemyDist < 900 || nMyDist > nEnemyDist * 2 / 3)
            continue;
        // beside the opening, on the side the enemy comes out
        const int wx = wall[wall[w].point2].x - wall[w].x, wy = wall[wall[w].point2].y - wall[w].y;
        const int nWallAng = getangle(wx, wy);
        int nNormal = (nWallAng + 512) & 2047;
        if (!inside(dx + mulscale30(Cos(nNormal), 64), dy + mulscale30(Sin(nNormal), 64), nWaitSect))
            nNormal = (nWallAng - 512) & 2047;
        const int nOff = approxDist(wx, wy) / 2 + 260;
        const int nFirstSide = BotChance(pBot, 50) ? 1 : -1;
        for (int nTry = 0; nTry < 3; nTry++)
        {
            int x, y;
            if (nTry < 2)
            {
                // to one side of the opening, out of sight of anyone still coming
                const int nSide = nTry ? -nFirstSide : nFirstSide;
                x = dx + mulscale30(Cos(nNormal), 320) + nSide * mulscale30(Cos(nWallAng), nOff);
                y = dy + mulscale30(Sin(nNormal), 320) + nSide * mulscale30(Sin(nWallAng), nOff);
            }
            else
            {
                // or further back, facing it
                x = dx + mulscale30(Cos(nNormal), 800);
                y = dy + mulscale30(Sin(nNormal), 800);
            }
            short nSector = nWaitSect;
            updatesector(x, y, &nSector);
            if (nSector < 0)
                continue;
            const int z = getflorzofslope(nSector, x, y);
            if (getceilzofslope(nSector, x, y) > z - (56<<8))
                continue;
            if (!cansee(x, y, z - (32<<8), nSector, dx, dy, getflorzofslope(nWaitSect, dx, dy) - (32<<8), nWaitSect))
                continue;
            if (!SetGoal(pBot, x, y, z, nSector))
                continue;
            pBot->nWatchAng = getangle(dx - x, dy - y);
            return true;
        }
    }
    return false;
}

// Go the other way round: the direct way between them costs a lot more.
static bool PlanFlank(BOT *pBot, PLAYER *pPlayer, BOTMEMORY *pMem)
{
    spritetype *pSprite = pPlayer->pSprite;
    short directPath[kBotMaxPath];
    int nDirectCost;
    const int nLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, pMem->nSector, pMem->x, pMem->y, directPath, kBotMaxPath, &nDirectCost);
    if (nLen < 2)
        return false;
    for (int i = 0; i < nLen; i++)
    {
        const int s = wall[directPath[i]].nextsector;
        if (s != pMem->nSector)
            nSectPenalty[s] = 600;
    }
    // where it could be seen coming from
    for (int s = 0; s < numsectors; s++)
    {
        if (BotDist(sectCenter[s].x, sectCenter[s].y, pMem->x, pMem->y) > 7000 || s == pMem->nSector)
            continue;
        if (cansee(pMem->x, pMem->y, pMem->z - (40<<8), pMem->nSector, sectCenter[s].x, sectCenter[s].y, getflorzofslope(s, sectCenter[s].x, sectCenter[s].y) - (32<<8), s))
            nSectPenalty[s] = max(nSectPenalty[s], 250);
    }
    nSectPenalty[pMem->nSector] = 0;
    nSectPenalty[pSprite->sectnum] = 0;
    int nFlankCost;
    const int nFlankLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, pMem->nSector, pMem->x, pMem->y, pBot->path, kBotMaxPath, &nFlankCost);
    BotClearPenalties();
    if (nFlankLen <= 0)
        return false;
    // the same way after all, or far too long a way round
    bool bSame = nFlankLen == nLen;
    for (int i = 0; bSame && i < nLen; i++)
        bSame = pBot->path[i] == directPath[i];
    if (bSame || nFlankCost > nDirectCost * 6)
    {
        pBot->nPathLen = 0;
        return false;
    }
    pBot->nGoalX = pMem->x;
    pBot->nGoalY = pMem->y;
    pBot->nGoalZ = pMem->z;
    pBot->nGoalSect = pMem->nSector;
    pBot->nPathGoalSect = pMem->nSector;
    pBot->nPathLen = nFlankLen;
    pBot->nPathPos = 0;
    pBot->nRepathTime = nNow + 30*20; // keep this way, don't fall back to the direct one
    return true;
}

static bool VantageTaken(BOT *pBot, int i)
{
    for (int j = 0; j < kMaxPlayers; j++)
        if (gBot[j].bActive && &gBot[j] != pBot && gBot[j].nVantage == i && (gBot[j].nState == kBotStateHighGround || nNow < gBot[j].nHoldUntil))
            return true;
    return false;
}

// A high place near it that sees the enemy (and that the enemy isn't standing on).
static bool PlanHighGround(BOT *pBot, PLAYER *pPlayer, spritetype *pEnemy)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = 0;
    for (int i = 0; i < nVantageSpots; i++)
    {
        if (nVantageFails[i] >= 2 || VantageTaken(pBot, i))
            continue;
        const vec3_t *pSpot = &vantageSpot[i];
        const int nDist = BotDist(pSprite->x, pSprite->y, pSpot->x, pSpot->y);
        if (nDist > 3500 || nDist < 200 || BotDist(pEnemy->x, pEnemy->y, pSpot->x, pSpot->y) < 700)
            continue;
        if (pEnemy->z - pSpot->z < (40<<8)) // not above it
            continue;
        if (!cansee(pSpot->x, pSpot->y, pSpot->z - (40<<8), vantageSect[i], pEnemy->x, pEnemy->y, SpriteCenterZ(pEnemy), pEnemy->sectnum))
            continue;
        const int nScore = vantageValue[i] * 100 / (100 + nDist / 40);
        if (nScore > nBestScore)
            nBest = i, nBestScore = nScore;
    }
    if (nBest < 0 || !SetGoal(pBot, vantageSpot[nBest].x, vantageSpot[nBest].y, vantageSpot[nBest].z, vantageSect[nBest]))
        return false;
    pBot->nState = kBotStateHighGround; // keep the path SetState would clear
    pBot->nStateTime = nNow;
    pBot->nVantage = nBest;
    return true;
}

// Run somewhere out of sight, preferably to health.
static bool PlanFlee(BOT *pBot, PLAYER *pPlayer, spritetype *pEnemy)
{
    spritetype *pSprite = pPlayer->pSprite;
    for (int s = 0; s < numsectors; s++)
    {
        const int nDist = BotDist(sectCenter[s].x, sectCenter[s].y, pEnemy->x, pEnemy->y);
        if (nDist < 2500)
            nSectPenalty[s] = 500;
        else if (nDist < 6000 && cansee(pEnemy->x, pEnemy->y, pEnemy->z - (40<<8), pEnemy->sectnum, sectCenter[s].x, sectCenter[s].y, getflorzofslope(s, sectCenter[s].x, sectCenter[s].y) - (32<<8), s))
            nSectPenalty[s] = 200;
    }
    nSectPenalty[pSprite->sectnum] = 0;
    bool bOk = false;
    int nValue;
    const int nItem = FindBestItem(pBot, pPlayer, &nValue, pEnemy->x, pEnemy->y);
    if (nItem >= 0 && ItemValue(pPlayer, &sprite[nItem]) >= 30)
    {
        bOk = SetGoal(pBot, sprite[nItem].x, sprite[nItem].y, sprite[nItem].z, sprite[nItem].sectnum);
        if (bOk)
            pBot->nGoalSprite = nItem;
    }
    for (int nTry = 0; !bOk && nTry < 8; nTry++)
    {
        int x, y, nSector;
        if (nItemSpots > 0 && (nTry & 1))
        {
            const int i = BotRand(pBot, nItemSpots);
            x = itemSpot[i].x, y = itemSpot[i].y, nSector = itemSpotSect[i];
        }
        else if (nCampSpots > 0)
        {
            const int i = BotRand(pBot, nCampSpots);
            x = campSpot[i].x, y = campSpot[i].y, nSector = campSect[i];
        }
        else
            break;
        if (BotDist(x, y, pEnemy->x, pEnemy->y) < BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y) + 1500)
            continue;
        bOk = SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector);
    }
    BotClearPenalties();
    return bOk;
}

static void BotSay(BOT *pBot, const char *pzLine, int nDelay)
{
    if (!gBotTaunts || pBot->pzTalk)
        return;
    pBot->pzTalk = pzLine;
    pBot->nTalkAt = nNow + nDelay;
}

// How a player in its place would size things up.
struct BOTVIEW
{
    int nMyHealth;  // health and some of the armor
    int nMyPower;   // its best weapon, powerups
    int nThreat;    // the enemies close by it knows of: their guns, how hurt they look, whether they've seen it
    int nRatio;     // its strength against the threat, 100 even
    int nEnemies;
    bool bEnemyQuad;
    int nScoreDiff; // its frags minus the best of the others
    bool bNearEnd;  // somebody is close to the frag limit
    int nMomentum;  // +1 just got a kill, -1 just got hurt badly
};

static int EnemyHealthGuess(BOT *pBot, int p) // from the hits it saw land
{
    return ClipRange(100 - pBot->nHurt[p], 15, 100);
}

static bool FacingMe(spritetype *pOther, spritetype *pMe, int nRange)
{
    return klabs(AngDiff(pOther->ang, getangle(pMe->x - pOther->x, pMe->y - pOther->y))) < nRange;
}

static void BotSizeUp(BOT *pBot, PLAYER *pPlayer, BOTVIEW *pView)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int nArmor = (pPlayer->armor[0] + pPlayer->armor[1] + pPlayer->armor[2]) >> 4;
    pView->nMyHealth = (pPlayer->pXSprite->health >> 4) + nArmor / 3;
    int nScore;
    BestWeapon(pPlayer, 2500, &nScore);
    pView->nMyPower = nScore / 10 + 10;
    if (powerupCheck(pPlayer, kPwUpTwoGuns))
        pView->nMyPower *= 2;
    if (powerupCheck(pPlayer, kPwUpDeathMask))
        pView->nMyPower *= 4;
    pView->nThreat = 0;
    pView->nEnemies = 0;
    pView->bEnemyQuad = false;
    int nBest = -999;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pOther = &gPlayer[p];
        if (pOther == pPlayer)
            continue;
        if (pOther->fragCount > nBest && (gGameOptions.nGameType != kGameTypeTeams || IsEnemyPlayer(pPlayer, pOther)))
            nBest = pOther->fragCount;
        if (!IsEnemyPlayer(pPlayer, pOther) || pOther->pXSprite->health <= 0)
            continue;
        BOTMEMORY *pMem = &pBot->mem[p];
        if (!pMem->bValid || nNow - pMem->nTime > 30*6 || BotDist(pSprite->x, pSprite->y, pMem->x, pMem->y) > 5000)
            continue;
        const int nPower = pMem->bSeen ? PlayerPower(pOther) : 50;
        if (pMem->bSeen && powerupCheck(pOther, kPwUpTwoGuns))
            pView->bEnemyQuad = true;
        int nStrength = nPower * EnemyHealthGuess(pBot, p) / 100;
        if (pMem->bSeen && FacingMe(pOther->pSprite, pSprite, 200))
            nStrength += nStrength / 3; // it has seen it
        pView->nThreat += ClipLow(nStrength, 5);
        pView->nEnemies++;
    }
    const int nMine = pView->nMyPower * ClipHigh(pView->nMyHealth, 200) / 100;
    pView->nRatio = pView->nThreat ? nMine * 100 / pView->nThreat : 300;
    pView->nScoreDiff = nBest == -999 ? 0 : pPlayer->fragCount - nBest;
    pView->bNearEnd = gPlayerRoundScoreLimit > 0 && max(nBest, pPlayer->fragCount) >= gPlayerRoundScoreLimit - 2;
    pView->nMomentum = (pBot->nLastKill && nNow - pBot->nLastKill < 30*10 ? 1 : 0) - (pBot->nLastPain && nNow - pBot->nLastPain < 30*5 ? 1 : 0);
}

// Its target is busy with somebody else, and neither looks its way: let them fight, then
// finish whoever is left (or both).
static bool OthersFighting(BOT *pBot, PLAYER *pPlayer, spritetype *pTarget)
{
    PLAYER *pTargetPlayer = SpritePlayer(pTarget->index);
    if (!pTargetPlayer || FacingMe(pTarget, pPlayer->pSprite, 250))
        return false;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pOther = &gPlayer[p];
        if (pOther == pPlayer || pOther == pTargetPlayer || pOther->pXSprite->health <= 0 || !IsEnemyPlayer(pPlayer, pOther))
            continue;
        spritetype *pOtherSprite = pOther->pSprite;
        if (BotDist(pOtherSprite->x, pOtherSprite->y, pTarget->x, pTarget->y) > 3500 || FacingMe(pOtherSprite, pPlayer->pSprite, 250))
            continue;
        const bool bOtherOnTarget = FacingMe(pOtherSprite, pTarget, 120) || (botIsBot(p) && gBot[p].nTarget == pTarget->index);
        const bool bTargetOnOther = FacingMe(pTarget, pOtherSprite, 150) || pTargetPlayer->input.buttonFlags.shoot;
        if (bOtherOnTarget && bTargetOnOther)
            return true;
    }
    UNREFERENCED_PARAMETER(pBot);
    return false;
}

// the human it plays with (co-op, teams), NULL none: offline the player, in a network game the
// closest person on its side (or the host, after the host's orders)
static PLAYER *TeamHuman(PLAYER *pPlayer)
{
    if (gBotNet && !bOrderByHost)
    {
        PLAYER *pBest = NULL;
        int nBest = 0x7fffffff;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            PLAYER *pOther = &gPlayer[p];
            if (botIsBot(p) || pOther == pPlayer || !pOther->pXSprite || !pOther->pSprite || IsEnemyPlayer(pPlayer, pOther))
                continue;
            if (!Bstrncmp(gProfile[p].name, "spectator", MAXPLAYERNAME))
                continue;
            const int nDist = BotDist(pPlayer->pSprite->x, pPlayer->pSprite->y, pOther->pSprite->x, pOther->pSprite->y) + (pOther->pXSprite->health > 0 ? 0 : 20000);
            if (nDist < nBest)
                nBest = nDist, pBest = pOther;
        }
        return pBest;
    }
    PLAYER *pHuman = &gPlayer[myconnectindex];
    if (pHuman == pPlayer || IsEnemyPlayer(pPlayer, pHuman) || !pHuman->pXSprite)
        return NULL;
    return pHuman;
}

void botOrder(int nOrder)
{
    if (!BotThinksHere())
        return;
    bOrderByHost = true;
    nBotOrder = ClipRange(nOrder, kBotOrderFree, kBotOrderHold);
    PLAYER *pHuman = &gPlayer[myconnectindex];
    if (nBotOrder == kBotOrderHold && pHuman->pSprite)
        nHoldX = pHuman->pSprite->x, nHoldY = pHuman->pSprite->y, nHoldZ = pHuman->pSprite->z, nHoldSect = pHuman->pSprite->sectnum;
    static const char *pzOrders[] = { "Bots: going ahead", "Bots: following you", "Bots: holding this spot" };
    viewSetMessage(pzOrders[nBotOrder], 8, MESSAGE_PRIORITY_NORMAL);
    for (int i = 0; i < kMaxPlayers; i++)
        if (gBot[i].bActive)
            gBot[i].nNextThink = 0; // think again now
}

static int FindItemOfType(int nType)
{
    for (int i = headspritestat[kStatItem]; i >= 0; i = nextspritestat[i])
        if (sprite[i].type == nType)
            return i;
    return -1;
}

// Something to do for the match type or its orders: where to go, how much it matters, whether to stay there.
static bool BotObjective(BOT *pBot, PLAYER *pPlayer, int *px, int *py, int *pz, int *pSector, int *pScore, bool *pbStay)
{
    spritetype *pSprite = pPlayer->pSprite;
    *pbStay = false;
    // orders from the human on its side
    PLAYER *pHuman = TeamHuman(pPlayer);
    if (pHuman && pHuman->pXSprite->health > 0)
    {
        spritetype *pHumanSprite = pHuman->pSprite;
        if (nBotOrder == kBotOrderHold && nHoldSect >= 0)
        {
            *px = nHoldX + BotRand(pBot, 400) - 200, *py = nHoldY + BotRand(pBot, 400) - 200, *pz = nHoldZ;
            short nSector = nHoldSect;
            updatesector(*px, *py, &nSector);
            if (nSector < 0)
                *px = nHoldX, *py = nHoldY, nSector = nHoldSect;
            *pSector = nSector;
            *pScore = 140;
            *pbStay = true;
            return true;
        }
        if (nBotOrder == kBotOrderFollow)
        {
            const int nDist = BotDist(pSprite->x, pSprite->y, pHumanSprite->x, pHumanSprite->y);
            const bool bSee = cansee(pSprite->x, pSprite->y, pPlayer->zView, pSprite->sectnum, pHumanSprite->x, pHumanSprite->y, pHuman->zView, pHumanSprite->sectnum);
            if (nDist > 900 || !bSee)
            {
                *px = pHumanSprite->x, *py = pHumanSprite->y, *pz = pHumanSprite->z, *pSector = pHumanSprite->sectnum;
                *pScore = nDist > 3000 || !bSee ? 160 : 90;
                return true;
            }
            // close by: hang about near it, a step to the side
            const int nAng = (pHumanSprite->ang + 1024 + BotRand(pBot, 900) - 450) & 2047;
            int x = pHumanSprite->x + mulscale30(Cos(nAng), 400), y = pHumanSprite->y + mulscale30(Sin(nAng), 400);
            short nSector = pHumanSprite->sectnum;
            updatesector(x, y, &nSector);
            if (nSector < 0)
                x = pHumanSprite->x, y = pHumanSprite->y, nSector = pHumanSprite->sectnum;
            *px = x, *py = y, *pz = pHumanSprite->z, *pSector = nSector;
            *pScore = 55;
            *pbStay = true;
            return true;
        }
    }
    // king of the hill: get on it and stay there
    int hx, hy, hr;
    if (modesHill(&hx, &hy, &hr))
    {
        short nSector = -1;
        int x = hx + BotRand(pBot, hr) - hr / 2, y = hy + BotRand(pBot, hr) - hr / 2;
        updatesector(x, y, &nSector);
        if (nSector < 0)
        {
            x = hx, y = hy;
            updatesector(x, y, &nSector);
        }
        if (nSector >= 0)
        {
            *px = x, *py = y, *pz = getflorzofslope(nSector, x, y), *pSector = nSector;
            *pScore = 70 + pBot->persona.nAggression / 3 + pBot->persona.nCamping / 4;
            *pbStay = true;
            return true;
        }
    }
    // capture the flag
    if (gGameOptions.nGameType == kGameTypeTeams && !(gGameOptions.uNetGameFlags & kNetGameFlagNoTeamFlags))
    {
        const int nTeam = pPlayer->teamId & 1;
        const int nOwnBase = FindItemOfType(nTeam == 0 ? kItemFlagABase : kItemFlagBBase);
        const int nEnemyBase = FindItemOfType(nTeam == 0 ? kItemFlagBBase : kItemFlagABase);
        const int nOwnBit = nTeam == 0 ? 1 : 2, nEnemyBit = nTeam == 0 ? 2 : 1;
        if (nOwnBase >= 0 && nEnemyBase >= 0)
        {
            const bool bOwnHome = sprite[nOwnBase].extra > 0 && xsprite[sprite[nOwnBase].extra].state;
            const bool bEnemyHome = sprite[nEnemyBase].extra > 0 && xsprite[sprite[nEnemyBase].extra].state;
            int nGoal = -1, nScore = 0;
            bool bStay = false;
            if (pPlayer->hasFlag & nEnemyBit)
                nGoal = nOwnBase, nScore = 170; // bring it home
            else if (!bOwnHome)
            {
                // ours is gone: pick it up where it was dropped, or go after whoever has it
                const int nDropped = FindItemOfType(nTeam == 0 ? kItemFlagA : kItemFlagB);
                if (nDropped >= 0)
                    nGoal = nDropped, nScore = 130;
                else
                {
                    for (int p = connecthead; p >= 0; p = connectpoint2[p])
                        if (IsEnemyPlayer(pPlayer, &gPlayer[p]) && (gPlayer[p].hasFlag & nOwnBit) && gPlayer[p].pXSprite->health > 0)
                            nGoal = gPlayer[p].nSprite, nScore = 120;
                }
            }
            else if (bEnemyHome && (pBot->persona.nAggression > 45 || (pPlayer->nPlayer & 1)))
                nGoal = nEnemyBase, nScore = 60 + pBot->persona.nAggression / 2; // attack
            else
            {
                const int nEnemyDropped = FindItemOfType(nTeam == 0 ? kItemFlagB : kItemFlagA);
                if (nEnemyDropped >= 0)
                    nGoal = nEnemyDropped, nScore = 110;
                else
                    nGoal = nOwnBase, nScore = 40 + pBot->persona.nCamping / 2, bStay = true; // defend
            }
            if (nGoal >= 0)
            {
                spritetype *pGoal = &sprite[nGoal];
                int x = pGoal->x, y = pGoal->y;
                if (bStay)
                {
                    const int nAng = BotRand(pBot, 2048);
                    x += mulscale30(Cos(nAng), 500), y += mulscale30(Sin(nAng), 500);
                }
                short nSector = pGoal->sectnum;
                updatesector(x, y, &nSector);
                if (nSector < 0)
                    x = pGoal->x, y = pGoal->y, nSector = pGoal->sectnum;
                *px = x, *py = y, *pz = pGoal->z, *pSector = nSector;
                *pScore = nScore;
                *pbStay = bStay;
                return true;
            }
        }
    }
    return false;
}

struct BOTOPTION { int nScore, nId; };

static void SortOptions(BOTOPTION *pOptions, int nCount)
{
    for (int i = 1; i < nCount; i++)
        for (int j = i; j > 0 && pOptions[j].nScore > pOptions[j-1].nScore; j--)
            swap(&pOptions[j], &pOptions[j-1]);
}

enum { kOptFight, kOptFlee, kOptHigh, kOptFlank, kOptWait, kOptCollect, kOptHunt, kOptAmbush, kOptCamp, kOptRoam, kOptObjective };

static void BotThink(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    const BOTPERSONA *pPersona = &pBot->persona;
    const int nTactics = pBot->skill.nTactics;
    BOTVIEW view;
    BotSizeUp(pBot, pPlayer, &view);

    // somebody it can see: weigh up fighting, getting away, getting up high, going round, waiting
    if (pBot->nTarget >= 0)
    {
        spritetype *pEnemy = &sprite[pBot->nTarget];
        PLAYER *pEnemyPlayer = SpritePlayer(pBot->nTarget);
        const int nDist = BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y);
        if (pBot->nState == kBotStateFlee && nNow - pBot->nStateTime < 30*5 && pBot->nGoalSect >= 0)
            return; // keep running for a while
        if (pBot->nState == kBotStateHighGround && pBot->nGoalSect >= 0 && nNow - pBot->nStateTime < 30*7)
            return; // on its way up there
        if (nNow < pBot->nThirdPartyUntil)
            return; // still letting them fight
        const bool bUnaware = !FacingMe(pEnemy, pSprite, 300);
        BOTOPTION options[5];
        int nOptions = 0;
        const int nJuggernaut = modesJuggernaut();
        const int nFight = 50 + pPersona->nAggression / 2 + ClipRange((view.nRatio - 100) / 3, -40, 40) + view.nMomentum * 12
            + (nJuggernaut == pPlayer->nPlayer ? 30 : 0) + (pPlayer->hasFlag ? -25 : 0)
            + (bUnaware ? 10 : 0) + (view.nScoreDiff < 0 ? 8 : 0) + (pBot->nState == kBotStateFight ? 10 : 0) + BotRand(pBot, 15);
        options[nOptions++] = { nFight, kOptFight };
        if (pEnemyPlayer && !powerupCheck(pPlayer, kPwUpDeathMask))
        {
            const int nFlee = ClipLow(55 - view.nMyHealth, 0) * (100 + pPersona->nFlee) / 100 + ClipRange((80 - view.nRatio) / 2, 0, 50)
                + (view.bEnemyQuad ? 45 : 0) + (view.bNearEnd && view.nScoreDiff > 0 ? 15 : 0) + (view.nEnemies >= 2 ? 15 : 0)
                - (nDist < 700 ? 20 : 0) + nTactics / 10 + BotRand(pBot, 15);
            options[nOptions++] = { nFlee, kOptFlee };
        }
        if (nNow >= pBot->nHoldUntil && nNow - pBot->nLastHighTry > 45 && nDist > 1200)
        {
            const int nHigh = pPersona->nHighGround / 2 + (pSprite->z - pEnemy->z > (48<<8) ? 25 : 0) + ClipRange((100 - view.nRatio) / 4, 0, 20) + BotRand(pBot, 15);
            options[nOptions++] = { nHigh, kOptHigh };
        }
        if (pEnemyPlayer && nDist > 4500 && bUnaware && pBot->nState != kBotStateFight)
            options[nOptions++] = { pPersona->nFlank * nTactics / 150 + 20 + BotRand(pBot, 15), kOptFlank };
        if (pEnemyPlayer && nTactics > 30 && view.nMyHealth > 30 && OthersFighting(pBot, pPlayer, pEnemy))
            options[nOptions++] = { 30 + nTactics / 3 + pPersona->nAmbush / 5 - pPersona->nAggression / 5 + BotRand(pBot, 15), kOptWait };
        SortOptions(options, nOptions);
        for (int i = 0; i < nOptions; i++)
        {
            switch (options[i].nId)
            {
            case kOptFlee:
                if (options[i].nScore < 45 || !PlanFlee(pBot, pPlayer, pEnemy))
                    continue;
                {
                    const int nGoalSprite = pBot->nGoalSprite;
                    pBot->nState = kBotStateFlee; // keep the path SetState would clear
                    pBot->nStateTime = nNow;
                    pBot->nGoalSprite = nGoalSprite;
                }
                return;
            case kOptHigh:
                pBot->nLastHighTry = nNow;
                if (!PlanHighGround(pBot, pPlayer, pEnemy))
                    continue;
                return;
            case kOptFlank:
                if (!PlanFlank(pBot, pPlayer, &pBot->mem[pEnemyPlayer->nPlayer]))
                    continue;
                pBot->nState = kBotStateFlank;
                pBot->nStateTime = nNow;
                return;
            case kOptWait:
                pBot->nThirdPartyUntil = nNow + 45 + BotRand(pBot, 60);
                SetState(pBot, kBotStateFight);
                return;
            case kOptFight:
            default:
                if (pBot->nState != kBotStateFight)
                {
                    SetState(pBot, kBotStateFight);
                    pBot->nStrafeUntil = 0;
                }
                return;
            }
        }
        return;
    }

    // up on a good spot after a fight: stay a while, watching
    if (nNow < pBot->nHoldUntil && pBot->nVantage >= 0 && BotDist(pSprite->x, pSprite->y, vantageSpot[pBot->nVantage].x, vantageSpot[pBot->nVantage].y) < 400)
        return;

    // keep doing what it does while it still makes sense
    switch (pBot->nState)
    {
    case kBotStateAmbush:
    case kBotStateCamp:
        if (pBot->nWaitUntil && nNow > pBot->nWaitUntil)
            break;
        if (pBot->nGoalSect >= 0 || pBot->nWaitUntil)
            return;
        break;
    case kBotStateFlank:
    case kBotStateHunt:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 400) && nNow - pBot->nStateTime < 30*25)
            return;
        if (pBot->nState == kBotStateHunt && GoalReached(pBot, pSprite, 400) && !pBot->nSearchUntil)
        {
            pBot->nSearchUntil = nNow + 45 + BotRand(pBot, 45); // look around a bit
            return;
        }
        if (pBot->nSearchUntil && nNow < pBot->nSearchUntil)
            return;
        break;
    case kBotStateFlee:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 200) && nNow - pBot->nStateTime < 30*8)
            return;
        break;
    case kBotStateObjective:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 250) && nNow - pBot->nStateTime < 30*3)
            return; // things move (players, flags): look again every few seconds
        break;
    case kBotStateCollect:
        if (nBotOrder == kBotOrderFollow && TeamHuman(pPlayer) && TeamHuman(pPlayer)->pXSprite->health > 0
            && BotDist(pSprite->x, pSprite->y, TeamHuman(pPlayer)->pSprite->x, TeamHuman(pPlayer)->pSprite->y) > 3000)
            break; // it's getting left behind
        if (pBot->nGoalSprite >= 0 && sprite[pBot->nGoalSprite].statnum == kStatItem && pBot->nGoalSect >= 0)
        {
            // standing on it and it's still there: it can't take it (full up), forget it for a while
            if (GoalReached(pBot, pSprite, 80))
            {
                if (!pBot->nAtGoalSince)
                    pBot->nAtGoalSince = nNow;
                else if (nNow - pBot->nAtGoalSince > 20)
                {
                    IgnoreItem(pBot, pBot->nGoalSprite);
                    break;
                }
            }
            else
                pBot->nAtGoalSince = 0;
            if (nNow - pBot->nStateTime < 30*15)
                return;
            IgnoreItem(pBot, pBot->nGoalSprite, true); // couldn't get there
        }
        break;
    case kBotStateRoam:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 300) && nNow - pBot->nStateTime < 30*20)
        {
            int nValue;
            FindBestItem(pBot, pPlayer, &nValue, INT32_MIN, 0);
            if (nValue < 60)
                return;
        }
        break;
    }
    pBot->nSearchUntil = 0;

    // nobody in sight: weigh up what to do next
    const int nHealth = pPlayer->pXSprite->health >> 4;
    int nValue;
    const int nItem = FindBestItem(pBot, pPlayer, &nValue, INT32_MIN, 0);
    const int p = RecentMemory(pBot, 30*5);
    BOTMEMORY *pMem = p >= 0 ? &pBot->mem[p] : NULL;
    const int nMemDist = pMem ? BotDist(pSprite->x, pSprite->y, pMem->x, pMem->y) : 0;
    const bool bComing = pMem && (pMem->vx || pMem->vy) && dmulscale16(pMem->vx, pSprite->x - pMem->x, pMem->vy, pSprite->y - pMem->y) > 0;
    // a fight going on somewhere (two others it heard close together): easy pickings
    bool bFightHeard = false;
    for (int a = connecthead; a >= 0 && !bFightHeard && pMem; a = connectpoint2[a])
        if (a != p && pBot->mem[a].bValid && nNow - pBot->mem[a].nTime < 30*3 && BotDist(pBot->mem[a].x, pBot->mem[a].y, pMem->x, pMem->y) < 3000)
            bFightHeard = true;
    BOTOPTION options[8];
    int nOptions = 0;
    // told to follow: only items close to the human
    PLAYER *pFollow = nBotOrder == kBotOrderFollow ? TeamHuman(pPlayer) : NULL;
    if (pFollow && (!pFollow->pXSprite || pFollow->pXSprite->health <= 0))
        pFollow = NULL;
    const bool bItemNear = nItem >= 0 && (!pFollow || BotDist(sprite[nItem].x, sprite[nItem].y, pFollow->pSprite->x, pFollow->pSprite->y) < 2500);
    if (nItem >= 0 && bItemNear)
    {
        const bool bUrgent = nValue >= 80 - pPersona->nAggression / 4;
        options[nOptions++] = { nValue * 2 / 3 + (bUrgent ? 35 : 0) + (nHealth < 50 ? 20 : 0) + BotRand(pBot, 10), kOptCollect };
    }
    if (pMem)
    {
        const int nHunt = 25 + pPersona->nAggression * 3 / 4 + ClipRange((view.nRatio - 100) / 4, -30, 30) + view.nMomentum * 10
            + (bFightHeard ? 15 : 0) + (view.nScoreDiff < 0 ? 8 : 0) - (nHealth < pPersona->nFlee + 10 ? 40 : 0) + BotRand(pBot, 15);
        options[nOptions++] = { nHunt, kOptHunt };
        if (bComing && nMemDist < 9000)
            options[nOptions++] = { 20 + pPersona->nAmbush * (nTactics + 40) / 100 + (nHealth < 60 ? 10 : 0) + BotRand(pBot, 20), kOptAmbush };
        if (nMemDist > 2500)
            options[nOptions++] = { 15 + pPersona->nFlank * (nTactics + 30) / 100 + BotRand(pBot, 20), kOptFlank };
    }
    if (pBot->nState != kBotStateCamp && (!pBot->nLastCamp || nNow - pBot->nLastCamp > 30*25))
        options[nOptions++] = { pPersona->nCamping * 2 / 3 + (view.bNearEnd && view.nScoreDiff > 0 ? 20 : 0) + (nHealth < 50 ? 10 : 0) + BotRand(pBot, 15), kOptCamp };
    int ox = 0, oy = 0, oz = 0, nObjSect = -1, nObjScore = 0;
    bool bObjStay = false;
    if (nOptions < 6 && BotObjective(pBot, pPlayer, &ox, &oy, &oz, &nObjSect, &nObjScore, &bObjStay))
        options[nOptions++] = { nObjScore + BotRand(pBot, 10), kOptObjective };
    // told to follow: wandering off isn't on
    if (!pFollow)
        options[nOptions++] = { 30 + BotRand(pBot, 10), kOptRoam };
    SortOptions(options, nOptions);
    for (int i = 0; i < nOptions; i++)
    {
        switch (options[i].nId)
        {
        case kOptCollect:
            SetState(pBot, kBotStateCollect);
            if (SetGoal(pBot, sprite[nItem].x, sprite[nItem].y, sprite[nItem].z, sprite[nItem].sectnum))
            {
                pBot->nGoalSprite = nItem;
                return;
            }
            IgnoreItem(pBot, nItem, true);
            continue;
        case kOptAmbush:
            if (!PlanAmbush(pBot, pPlayer, pMem))
                continue;
            {
                const int nWatch = pBot->nWatchAng;
                pBot->nState = kBotStateAmbush;
                pBot->nStateTime = nNow;
                pBot->nWatchAng = nWatch;
                pBot->nWaitUntil = 0;
            }
            return;
        case kOptFlank:
            if (!PlanFlank(pBot, pPlayer, pMem))
                continue;
            pBot->nState = kBotStateFlank;
            pBot->nStateTime = nNow;
            return;
        case kOptHunt:
        {
            SetState(pBot, kBotStateHunt);
            // where it was going
            int x = pMem->x + ((pMem->vx * 30) >> 12), y = pMem->y + ((pMem->vy * 30) >> 12);
            short nSector = pMem->nSector;
            updatesector(x, y, &nSector);
            if (nSector < 0)
                x = pMem->x, y = pMem->y, nSector = pMem->nSector;
            if (SetGoal(pBot, x, y, pMem->z, nSector) || SetGoal(pBot, pMem->x, pMem->y, pMem->z, pMem->nSector))
                return;
            continue;
        }
        case kOptCamp:
            if (pFollow)
                continue;
            SetState(pBot, kBotStateCamp);
            if (PickCampSpot(pBot, pSprite))
                return;
            continue;
        case kOptObjective:
            if (bObjStay && BotDist(pSprite->x, pSprite->y, ox, oy) < 600)
            {
                // there already: wait and watch
                pBot->nState = -1;
                SetState(pBot, kBotStateCamp);
                pBot->nGoalX = pSprite->x, pBot->nGoalY = pSprite->y, pBot->nGoalZ = pSprite->z, pBot->nGoalSect = pSprite->sectnum;
                pBot->nWaitUntil = nNow + 30*3 + BotRand(pBot, 30*3);
                pBot->nWatchAng = BotRand(pBot, 2048);
                pBot->nLastCamp = 0;
                return;
            }
            pBot->nState = -1;
            SetState(pBot, kBotStateObjective);
            if (SetGoal(pBot, ox, oy, oz, nObjSect))
                return;
            if (pFollow)
            {
                // no way it knows to the human: head straight for it and find one
                pBot->nGoalX = ox, pBot->nGoalY = oy, pBot->nGoalZ = oz, pBot->nGoalSect = nObjSect;
                pBot->nPathGoalSect = nObjSect;
                pBot->nPathLen = pBot->nPathPos = 0;
                pBot->nRepathTime = nNow;
                return;
            }
            continue;
        case kOptRoam:
        default:
            break;
        }
        break;
    }
    SetState(pBot, kBotStateRoam);
    if (PickRoamGoal(pBot, pPlayer))
        return;
    // no way it knows anywhere (a lift that's away, stacked rooms, a door opened elsewhere):
    // wander towards a neighbouring area like a lost player would; jumping and pressing use
    // when it gets stuck, the game may well take it somewhere
    const int nSector = pSprite->sectnum;
    for (int nTry = 0; nTry < 8; nTry++)
    {
        const int w = sector[nSector].wallptr + BotRand(pBot, sector[nSector].wallnum);
        const int nNext = wall[w].nextsector;
        int x = (wall[w].x + wall[wall[w].point2].x) >> 1, y = (wall[w].y + wall[wall[w].point2].y) >> 1;
        if (nNext < 0 && nTry < 6)
            continue;
        if (nNext >= 0 && inside(sectCenter[nNext].x, sectCenter[nNext].y, nNext))
            x = sectCenter[nNext].x, y = sectCenter[nNext].y;
        pBot->nGoalX = x;
        pBot->nGoalY = y;
        pBot->nGoalZ = pSprite->z;
        pBot->nGoalSect = nNext >= 0 ? nNext : nSector;
        pBot->nPathGoalSect = pBot->nGoalSect;
        pBot->nPathLen = pBot->nPathPos = 0;
        pBot->nRepathTime = nNow;
        return;
    }
}

//
// moving, turning and shooting
//

// Getting a lift to the height it needs: wait while it moves, start it (use on it when standing
// on it, use on its wall from beside it), or walk over to its switch and back. Returns true
// while it takes care of moving.
static bool LiftAction(BOT *pBot, PLAYER *pPlayer, int nPortal, int tx, int ty, int *pMoveAng, int *pMoveSpeed, int *pLookAng, bool *pUse)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int nLift = EdgeLift(nPortal);
    const int zWant = LiftFloorFor(nPortal);
    if (nLift < 0 || zWant == INT32_MIN)
        return false;
    if (pBot->nLiftWall != nPortal)
        pBot->nLiftWall = nPortal, pBot->nLiftSince = nNow;
    if (nNow - pBot->nLiftSince > 30*20)
    {
        // it never came: another way
        BlockWall(pBot, nPortal);
        pBot->nLiftWall = -1;
        if (pBot->nGoalSect >= 0)
            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY);
        return false;
    }
    XSECTOR *pXSector = &xsector[sector[nLift].extra];
    const bool bOnLift = pSprite->sectnum == nLift;
    const int nAngTo = getangle(tx - pSprite->x, ty - pSprite->y);
    *pLookAng = nAngTo;
    if (!bOnLift && BotDist(pSprite->x, pSprite->y, tx, ty) > 300)
    {
        // walk up to it first
        *pMoveAng = nAngTo;
        *pMoveSpeed = 2048;
        return true;
    }
    *pMoveSpeed = 0;
    if (bOnLift)
    {
        // stay in the middle of the lift so it doesn't step off as it moves
        const int cx = sectCenter[nLift].x, cy = sectCenter[nLift].y;
        if (inside(cx, cy, nLift) && BotDist(pSprite->x, pSprite->y, cx, cy) > 200)
            *pMoveAng = getangle(cx - pSprite->x, cy - pSprite->y), *pMoveSpeed = 1024;
    }
    if (pXSector->busy)
        return true; // on its way: wait
    if (nNow - pBot->nLiftSince < 10)
        return true; // give it a moment (somebody else may have called it)
    if ((nNow + pBot->nPlayer) % 20)
        return true;
    if (bOnLift && pXSector->Push)
    {
        *pUse = true;
        return true;
    }
    if (!bOnLift && pXSector->Wallpush)
    {
        if (klabs(AngDiff(pSprite->ang, nAngTo)) < 128)
            *pUse = UseCheck(pPlayer) >= 0;
        return true;
    }
    // its switch: the closest one it can walk to
    const int nChannel = pXSector->rxID & 1023;
    int nBest = -1, nBestDist = 0x7fffffff;
    for (int i = 0; i < nOpeners[nChannel]; i++)
    {
        const int nDist = BotDist(pSprite->x, pSprite->y, openers[nChannel][i].x, openers[nChannel][i].y);
        if (nDist < nBestDist)
            nBest = i, nBestDist = nDist;
    }
    if (nBest < 0 || pBot->bDetour)
        return true;
    const OPENER *pOpener = &openers[nChannel][nBest];
    pBot->nSavedX = pBot->nGoalX, pBot->nSavedY = pBot->nGoalY, pBot->nSavedZ = pBot->nGoalZ, pBot->nSavedSect = pBot->nGoalSect;
    if (SetGoal(pBot, pOpener->x, pOpener->y, getflorzofslope(pOpener->nSector, pOpener->x, pOpener->y), pOpener->nSector))
    {
        pBot->bDetour = true;
        pBot->nDetourAng = pOpener->nAng;
        pBot->nLiftSince = nNow; // the waiting starts again when it comes back
    }
    else
        SetGoal(pBot, pBot->nSavedX, pBot->nSavedY, pBot->nSavedZ, pBot->nSavedSect);
    return true;
}

static void FollowPath(BOT *pBot, spritetype *pSprite, int *tx, int *ty, int *pEdge, int *pPortal)
{
    *pEdge = kEdgeWalk;
    *pPortal = -1;
    for (int i = pBot->nPathPos; i < pBot->nPathLen; i++)
    {
        if (wall[pBot->path[i]].nextsector == pSprite->sectnum)
        {
            pBot->nPathPos = i + 1;
            break;
        }
    }
    if (pBot->nPathPos >= pBot->nPathLen)
    {
        if (pBot->nPathLen > 0 && pSprite->sectnum != pBot->nPathGoalSect && pBot->nGoalSect >= 0 && nNow - pBot->nRepathTime > 10)
            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY); // a long way, the next part of it
        *tx = pBot->nGoalX;
        *ty = pBot->nGoalY;
        return;
    }
    const int w = pBot->path[pBot->nPathPos];
    if (sectorofwall(w) != pSprite->sectnum)
    {
        // pushed off its way
        if (nNow - pBot->nRepathTime > 8 && pBot->nGoalSect >= 0)
        {
            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY);
            if (pBot->nPathLen == 0 && pSprite->sectnum != pBot->nGoalSect)
                pBot->nGoalSect = -1;
        }
        *tx = pBot->nGoalX;
        *ty = pBot->nGoalY;
        if (pBot->nPathPos < pBot->nPathLen)
        {
            const int w2 = pBot->path[pBot->nPathPos];
            PortalPoint(w2, pSprite->x, pSprite->y, tx, ty);
        }
        return;
    }
    int nx, ny;
    if (pBot->nPathPos + 1 < pBot->nPathLen)
    {
        const int w2 = pBot->path[pBot->nPathPos + 1];
        nx = (wall[w2].x + wall[wall[w2].point2].x) >> 1;
        ny = (wall[w2].y + wall[wall[w2].point2].y) >> 1;
    }
    else
        nx = pBot->nGoalX, ny = pBot->nGoalY;
    PortalPoint(w, (pSprite->x + nx) >> 1, (pSprite->y + ny) >> 1, tx, ty);
    *pEdge = EdgeType(w);
    if (*pEdge == kEdgeBlocked && EdgeTypePlan(w) == kEdgeLift)
        *pEdge = kEdgeLift;
    *pPortal = w;
    // close to the opening: aim past it so it doesn't slow down in the doorway
    if (*pEdge == kEdgeWalk && BotDist(pSprite->x, pSprite->y, *tx, *ty) < 256)
    {
        int px = *tx, py = *ty;
        *tx = px + (nx - px) / 4;
        *ty = py + (ny - py) / 4;
    }
}

static bool StayAbove(BOT *pBot)
{
    if (nNow >= pBot->nAboveDecideAt)
    {
        pBot->bStayAbove = BotChance(pBot, 50 + pBot->persona.nHighGround / 2);
        pBot->nAboveDecideAt = nNow + 60 + BotRand(pBot, 60);
    }
    return pBot->bStayAbove;
}

// a drop (more than a jump back up) right in front, that way
static bool LedgeAhead(spritetype *pSprite, int nAng)
{
    const int zHere = getflorzofslope(pSprite->sectnum, pSprite->x, pSprite->y);
    for (int nDist = 96; nDist <= 224; nDist += 128)
    {
        const int x = pSprite->x + mulscale30(Cos(nAng), nDist), y = pSprite->y + mulscale30(Sin(nAng), nDist);
        short nSector = pSprite->sectnum;
        updatesector(x, y, &nSector);
        if (nSector < 0)
            return false; // a wall: the feelers deal with those
        if (getflorzofslope(nSector, x, y) - zHere > (56<<8) && !(sector[nSector].extra > 0 && xsector[sector[nSector].extra].Underwater))
            return true;
    }
    return false;
}

// how far it can walk that way before bumping into a wall (or someone)
static int ProbeDist(spritetype *pSprite, int nAng, int z)
{
    hitdata_t hit;
    vec3_t pos = { pSprite->x, pSprite->y, z };
    const int nCstat = pSprite->cstat;
    pSprite->cstat &= ~CSTAT_SPRITE_BLOCK_HITSCAN;
    hitscangoal.x = hitscangoal.y = 0x1fffffff;
    hitscan(&pos, pSprite->sectnum, Cos(nAng) >> 16, Sin(nAng) >> 16, 0, &hit, CLIPMASK0);
    pSprite->cstat = nCstat;
    if (hit.sect < 0)
        return 0x7fffffff;
    return approxDist(hit.xyz.x - pos.x, hit.xyz.y - pos.y);
}

// Feelers: walking straight at a corner, turn a little to one side until the way is clear.
static int AvoidWalls(BOT *pBot, spritetype *pSprite, int nAng, int nWant)
{
    const int z = pSprite->z - (30<<8);
    const int nNeed = ClipRange(nWant, 96, 320);
    if (ProbeDist(pSprite, nAng, z) >= nNeed)
        return nAng;
    int nSide = nNow < pBot->nAvoidUntil ? pBot->nAvoidSide : 0;
    if (!nSide)
    {
        // the side with more room
        nSide = ProbeDist(pSprite, (nAng + 256) & 2047, z) >= ProbeDist(pSprite, (nAng - 256) & 2047, z) ? 1 : -1;
    }
    static const int nOffsets[] = { 96, 192, 320, 448, 576 };
    for (int nOff : nOffsets)
    {
        for (int s = 0; s < 2; s++)
        {
            const int nTry = (nAng + (s ? -nSide : nSide) * nOff) & 2047;
            if (ProbeDist(pSprite, nTry, z) >= nNeed)
            {
                pBot->nAvoidSide = s ? -nSide : nSide;
                pBot->nAvoidUntil = nNow + 15;
                return nTry;
            }
        }
    }
    return nAng;
}

static int GaussRand(BOT *pBot, int nRange) // roughly normal, -nRange..nRange
{
    return (BotRand(pBot, nRange + 1) + BotRand(pBot, nRange + 1) + BotRand(pBot, nRange + 1)) * 2 / 3 - nRange;
}

static void BotTalk(BOT *pBot, PLAYER *pPlayer)
{
    if (!pBot->pzTalk || nNow < pBot->nTalkAt)
        return;
    char buffer[128];
    Bsnprintf(buffer, sizeof(buffer), "%s: %s", gProfile[pPlayer->nPlayer].name, pBot->pzTalk);
    viewSetMessage(buffer, 0, MESSAGE_PRIORITY_NORMAL);
    if (gBotNet)
        netBroadcastBotTalk(pPlayer->nPlayer, pBot->pzTalk, -1);
    pBot->pzTalk = NULL;
}

//
// the career
//

void careerDescribe(int nStage, char *pzLine1, char *pzLine2, int nSize)
{
    nStage = ClipRange(nStage, 1, kCareerStages);
    const CAREERSTAGE *pStage = &gCareer[nStage - 1];
    const char *pzMap = gEpisodeCount > 4 && pStage->nLevel - 1 < gEpisodeInfo[4].nLevels ? gEpisodeInfo[4].levelsInfo[pStage->nLevel - 1].Title : "";
    char zGoal[32] = "";
    if (pStage->nMode == kChosenModeGunGame)
        Bsnprintf(zGoal, sizeof(zGoal), "ALL 10 WEAPONS");
    else if (pStage->nMode == kChosenModeLastManStanding)
        Bsnprintf(zGoal, sizeof(zGoal), "%d LIVES", pStage->nLives);
    else if (pStage->nMode == kChosenModeKingOfTheHill)
        Bsnprintf(zGoal, sizeof(zGoal), "%d POINTS", pStage->nFrags);
    else
        Bsnprintf(zGoal, sizeof(zGoal), "%d FRAGS", pStage->nFrags);
    Bsnprintf(pzLine1, nSize, "STAGE %d/%d: %s ON %s, %s", nStage, kCareerStages,
        pStage->nMode == kChosenModeNormal ? "BLOODBATH" : modesName(pStage->nMode), pzMap, zGoal);
    int n = Bsnprintf(pzLine2, nSize, "%s: ", gBotSkillNames[(int)pStage->nSkill]);
    for (int i = 0; i < 6 && pStage->pzNames[i] && n < nSize - 1; i++)
        n += Bsnprintf(pzLine2 + n, nSize - n, "%s%s", i ? ", " : "", pStage->pzNames[i]);
    Bstrupr(pzLine1);
    Bstrupr(pzLine2);
}

// "Name=count,Name=count": who killed the human how often over the career
static void AddRival(const char *pzName, int nKills)
{
    char names[8][MAXPLAYERNAME];
    int counts[8], n = 0;
    char buffer[sizeof(gCareerRivals)];
    Bstrncpyz(buffer, gCareerRivals, sizeof(buffer));
    for (char *pzTok = strtok(buffer, ","); pzTok && n < 8; pzTok = strtok(NULL, ","))
    {
        char *pEq = strchr(pzTok, '=');
        if (!pEq)
            continue;
        *pEq = 0;
        Bstrncpyz(names[n], pzTok, MAXPLAYERNAME);
        counts[n++] = atoi(pEq + 1);
    }
    int i = 0;
    while (i < n && Bstrcasecmp(names[i], pzName))
        i++;
    if (i == n)
    {
        if (n == 8) // keep the 8 worst
        {
            i = 0;
            for (int j = 1; j < n; j++)
                if (counts[j] < counts[i])
                    i = j;
        }
        else
            n++;
        Bstrncpyz(names[i], pzName, MAXPLAYERNAME);
        counts[i] = 0;
    }
    counts[i] += nKills;
    int nLen = 0;
    gCareerRivals[0] = 0;
    for (int j = 0; j < n; j++)
        nLen += Bsnprintf(gCareerRivals + nLen, sizeof(gCareerRivals) - nLen, "%s%s=%d", j ? "," : "", names[j], counts[j]);
}

void careerNemesis(char *pzBuffer, int nSize)
{
    char buffer[sizeof(gCareerRivals)];
    Bstrncpyz(buffer, gCareerRivals, sizeof(buffer));
    char zBest[MAXPLAYERNAME] = "";
    int nBest = 0;
    for (char *pzTok = strtok(buffer, ","); pzTok; pzTok = strtok(NULL, ","))
    {
        char *pEq = strchr(pzTok, '=');
        if (!pEq)
            continue;
        *pEq = 0;
        if (atoi(pEq + 1) > nBest)
            nBest = atoi(pEq + 1), Bstrncpyz(zBest, pzTok, sizeof(zBest));
    }
    if (nBest)
        Bsnprintf(pzBuffer, nSize, "NEMESIS: %s (KILLED YOU %d TIMES)", zBest, nBest);
    else
        pzBuffer[0] = 0;
    Bstrupr(pzBuffer);
}

void careerPlay(void)
{
    const int nStage = ClipRange(gCareerStage, 1, kCareerStages);
    const CAREERSTAGE *pStage = &gCareer[nStage - 1];
    BotMatchSetMap(4, pStage->nLevel - 1);
    BotMatchSetMode(pStage->nGameType, pStage->nMode);
    BotMatchSetLimits(pStage->nFrags, 0);
    BotMatchSetLives(pStage->nLives ? pStage->nLives : 3);
    BotMatchResetMutators();
    gBotWatchMode = false;
    gBotCareer = nStage; // botSetupMatch takes the opponents from it
    StartBotMatch(NULL);
}

void careerReset(void)
{
    gCareerStage = 1;
    gCareerWins = gCareerMatches = gCareerKills = gCareerDeaths = gCareerBestStreak = 0;
    gCareerRivals[0] = 0;
    CONFIG_WriteSetup(2);
}

static void CareerMatchOver(void)
{
    PLAYER *pMe = &gPlayer[myconnectindex];
    // did the human win? the match type's winner, or the most frags/points
    int nWinner = modesWinner();
    if (nWinner < 0 && gChosenRules.nMode != kChosenModeSurvival)
    {
        int nBest = INT32_MIN;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (gPlayer[p].fragCount > nBest)
                nBest = gPlayer[p].fragCount, nWinner = p;
    }
    const bool bWon = nWinner == myconnectindex;
    int nKills = 0, nWorst = -1, nWorstKills = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (p == myconnectindex)
            continue;
        nKills += pMe->fragInfo[p];
        if (gPlayer[p].fragInfo[myconnectindex] > 0)
            AddRival(gProfile[p].name, gPlayer[p].fragInfo[myconnectindex]);
        if (gPlayer[p].fragInfo[myconnectindex] > nWorstKills)
            nWorst = p, nWorstKills = gPlayer[p].fragInfo[myconnectindex];
    }
    UNREFERENCED_PARAMETER(nWorst);
    gCareerMatches++;
    gCareerKills += nKills;
    gCareerDeaths += modesDeaths(myconnectindex);
    gCareerBestStreak = max(gCareerBestStreak, modesBestStreak(myconnectindex));
    char buffer[96];
    if (bWon)
    {
        gCareerWins++;
        if (gBotCareer >= kCareerStages)
            Bsnprintf(buffer, sizeof(buffer), "You beat the whole ladder! Champion of the Chosen!");
        else
        {
            gCareerStage = ClipHigh(gBotCareer + 1, kCareerStages);
            Bsnprintf(buffer, sizeof(buffer), "Stage %d won! Stage %d is next (CAREER in the bots menu)", gBotCareer, gCareerStage);
        }
    }
    else
        Bsnprintf(buffer, sizeof(buffer), "Stage %d lost. Try it again from CAREER in the bots menu", gBotCareer);
    viewSetMessage(buffer, 8, MESSAGE_PRIORITY_NORMAL);
    CONFIG_WriteSetup(2);
}

static void BotMatchFrame(void)
{
    static bool bWasEnding;
    if (gPlayerRoundEnding && !bWasEnding && gBotCareer > 0)
        CareerMatchOver();
    bWasEnding = gPlayerRoundEnding;
    if (!gBotWatchMode || gPaused)
        return;
    gPlayer[myconnectindex].fragCount = 0; // the spectator doesn't play (it "dies" every spawn)
    // the camera: stay with a bot a while, then go where the action is
    static int nNextSwitch, nDeadSince;
    PLAYER *pView = gView;
    const bool bDead = !pView || !pView->pXSprite || pView->pXSprite->health <= 0 || pView->nPlayer == myconnectindex;
    if (bDead && !nDeadSince)
        nDeadSince = gLevelTime;
    else if (!bDead)
        nDeadSince = 0;
    if (gLevelTime < nNextSwitch && !(bDead && gLevelTime - nDeadSince > 45) && gLevelTime >= 30)
        return;
    int nBest = -1, nBestScore = -1;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (!botIsBot(p) || !gPlayer[p].pXSprite || gPlayer[p].pXSprite->health <= 0)
            continue;
        int nScore = 10 + (gBot[p].nTarget >= 0 ? 100 : 0) + (gBot[p].nState == kBotStateFight ? 50 : 0) + gPlayer[p].fragCount;
        if (pView && p == pView->nPlayer)
            nScore -= 40; // something else for a change
        nScore += (int)(((uint32_t)(gLevelTime * 2654435761u) >> (p * 3)) & 31);
        if (nScore > nBestScore)
            nBest = p, nBestScore = nScore;
    }
    if (nBest >= 0)
    {
        gViewIndex = nBest;
        gView = &gPlayer[nBest];
    }
    nNextSwitch = gLevelTime + 30*12;
    nDeadSince = 0;
}

// adaptive difficulty: the human doing well makes the bots a bit sharper, doing badly a bit slower
static void BotAdapt(void)
{
    static int nLastCheck;
    if (!gBotAdaptive || gLevelTime - nLastCheck < 30*10 || gLevelTime < nLastCheck)
    {
        if (gLevelTime < nLastCheck)
            nLastCheck = gLevelTime;
        return;
    }
    nLastCheck = gLevelTime;
    int nBots = 0, nBotFrags = 0, nPeople = 0, nPeopleFrags = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (botIsBot(p))
            nBots++, nBotFrags += gPlayer[p].fragCount;
        else if (Bstrncmp(gProfile[p].name, "spectator", MAXPLAYERNAME))
            nPeople++, nPeopleFrags += gPlayer[p].fragCount;
    }
    if (!nBots || !nPeople)
        return;
    const int nDiff = (nPeopleFrags * nBots - nBotFrags * nPeople) / nPeople; // the people ahead of the average bot, times bots
    if (nDiff >= nBots * 2)
        nAdapt = ClipLow(nAdapt - 5, 60);
    else if (nDiff <= -nBots * 2)
        nAdapt = ClipHigh(nAdapt + 5, 160);
}

void botGetInput(int nPlayer, GINPUT *pInput)
{
    Bmemset(pInput, 0, sizeof(GINPUT));
    if (!botIsBot(nPlayer))
        return;
    BOT *pBot = &gBot[nPlayer];
    PLAYER *pPlayer = &gPlayer[nPlayer];
    spritetype *pSprite = pPlayer->pSprite;
    const bool bFirst = nPlayer == FirstBot();
    if (bFirst && gBotMatch)
        BotMatchFrame();
    // offline the menu pauses the match; in a network game the host's menu doesn't stop anybody
    if (!pSprite || !pPlayer->pXSprite || gPaused || (gGameMenuMgr.m_bActive && gBotMatch) || gEndGameMgr.at0 || gPlayerRoundEnding)
        return;
    nNow = gLevelTime;
    pInput->syncFlags.run = 1;
    if (bFirst)
        BotAdapt();
    BotTalk(pBot, pPlayer);

    // dead: wait a moment, then respawn
    if (pPlayer->pXSprite->health <= 0)
    {
        if (!pBot->bWasDead)
        {
            pBot->bWasDead = true;
            pBot->nRespawnDelay = 25 + BotRand(pBot, 50);
            if (pPlayer->fraggerId >= 0 && pPlayer->fraggerId != pPlayer->nSprite && BotChance(pBot, 30))
                BotSay(pBot, pzBotDeathLines[BotRand(pBot, ARRAY_SSIZE(pzBotDeathLines))], 20 + BotRand(pBot, 30));
        }
        pBot->nState = kBotStateDead;
        if (pPlayer->deathTime > pBot->nRespawnDelay * kTicsPerFrame && !(nNow & 7))
            pInput->keyFlags.action = 1;
        return;
    }
    if (pBot->bWasDead)
    {
        pBot->bWasDead = false;
        pBot->nHoldUntil = 0;
        pBot->nVantage = -1;
        pBot->bDetour = false;
        pBot->nLiftWall = -1;
        pBot->nState = -1;
        SetState(pBot, kBotStateRoam);
        pBot->nTarget = -1;
        pBot->nLastHealth = pPlayer->pXSprite->health;
        pBot->nStuckCount = 0;
        Bmemset(pBot->mem, 0, sizeof(pBot->mem));
        pBot->nThirdPartyUntil = 0;
    }
    if (pPlayer->fragCount > pBot->nLastFrags)
    {
        const int nVictim = gPlayerLastVictim;
        const bool bPayback = gBotGrudges && nVictim >= 0 && nVictim < kMaxPlayers && gPlayer[nVictim].fragInfo[pPlayer->nPlayer] - pPlayer->fragInfo[nVictim] >= 1;
        if (bPayback && BotChance(pBot, 60))
            BotSay(pBot, pzBotGrudgeLines[BotRand(pBot, ARRAY_SSIZE(pzBotGrudgeLines))], 15 + BotRand(pBot, 30));
        else if (BotChance(pBot, 35))
        {
            BotSay(pBot, pzBotKillLines[BotRand(pBot, ARRAY_SSIZE(pzBotKillLines))], 15 + BotRand(pBot, 40));
            if (gBotTaunts)
            {
                const int nSound = 4400 + BotRand(pBot, 10);
                sfxPlay3DSound(pSprite, nSound, -1, 0);
                if (gBotNet)
                    netBroadcastBotTalk(pPlayer->nPlayer, "", nSound);
            }
        }
        pBot->nLastFrags = pPlayer->fragCount;
        pBot->nLastKill = nNow;
    }

    // look and listen (half the bots one frame, the others the next)
    const int nOldTarget = pBot->nTarget;
    if (((nNow + nPlayer) & 1) == 0 || (pBot->nTarget >= 0 && !SpriteAlive(pBot->nTarget)))
    {
        pBot->nTarget = BotPerceive(pBot, pPlayer);
        if (pBot->nTarget >= 0 && pBot->nTarget != nOldTarget)
        {
            // a new enemy: it takes a moment to react, longer at the edge of its view
            spritetype *pEnemy = &sprite[pBot->nTarget];
            const int nOff = klabs(AngDiff(pSprite->ang, getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y)));
            int nReact = pBot->skill.nReaction;
            if (gBotAdaptive)
                nReact = nReact * nAdapt / 100;
            nReact += nReact * nOff / 700;
            if (pBot->nState == kBotStateAmbush || pBot->nState == kBotStateCamp)
                nReact /= 2; // was expecting it
            nReact += GaussRand(pBot, nReact / 3);
            pBot->nReactAt = nNow + ClipLow(nReact, 1);
            pBot->nTargetSince = nNow;
            pBot->nTrackFrames = 0;
            pBot->nAimErrGoal = GaussRand(pBot, pBot->skill.nAimError * 2);
            pBot->nAimErr = pBot->nAimErrGoal;
            pBot->nNextThink = nNow; // rethink now
        }
    }
    else if (pBot->nTarget >= 0 && !SpriteAlive(pBot->nTarget))
        pBot->nTarget = -1;

    // hurt by something it doesn't see: turn round towards whoever it'd guess
    const int nHealth = pPlayer->pXSprite->health;
    if (pBot->nLastHealth - nHealth >= (25<<4) || (nHealth < (35<<4) && nHealth < pBot->nLastHealth))
        pBot->nLastPain = nNow; // that hurt
    // the hits it sees its own shots make (a player sees the blood fly)
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pOther = &gPlayer[p];
        if (!IsEnemyPlayer(pPlayer, pOther))
            continue;
        const int nOtherHealth = pOther->pXSprite->health >> 4;
        if (nOtherHealth > pBot->nEnemyHealth[p] + 20 || nOtherHealth <= 0)
            pBot->nHurt[p] = 0; // back from the dead (or healed up a lot)
        else if (nOtherHealth < pBot->nEnemyHealth[p] && pBot->nTarget == pOther->nSprite && nNow - pBot->nLastShot < 12)
            pBot->nHurt[p] += pBot->nEnemyHealth[p] - nOtherHealth;
        pBot->nEnemyHealth[p] = nOtherHealth;
    }
    if (nHealth < pBot->nLastHealth && pBot->nTarget < 0)
    {
        pBot->nHurtTime = nNow;
        int nBest = -1, nBestDist = 0x7fffffff;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            PLAYER *pOther = &gPlayer[p];
            if (!IsEnemyPlayer(pPlayer, pOther) || pOther->pXSprite->health <= 0)
                continue;
            const int nDist = BotDist(pSprite->x, pSprite->y, pOther->pSprite->x, pOther->pSprite->y);
            if (nDist < nBestDist && (pOther->input.buttonFlags.shoot || pOther->input.buttonFlags.shoot2 || nDist < 3000))
                nBest = p, nBestDist = nDist;
        }
        if (nBest >= 0)
        {
            Remember(pBot, nBest, gPlayer[nBest].pSprite, false, nBestDist / 12);
            pBot->nWatchAng = getangle(pBot->mem[nBest].x - pSprite->x, pBot->mem[nBest].y - pSprite->y);
            if (pBot->nState == kBotStateRoam || pBot->nState == kBotStateCollect || pBot->nState == kBotStateCamp)
                pBot->nNextThink = nNow;
        }
    }
    pBot->nLastHealth = nHealth;

    if (nNow >= pBot->nNextThink)
    {
        BotThink(pBot, pPlayer);
        pBot->nNextThink = nNow + 6 + BotRand(pBot, 6);
    }

    // where to go and where to look
    int nMoveAng = pSprite->ang, nMoveSpeed = 0;
    int nLookAng = pSprite->ang;
    int nLookZ = INT32_MIN;
    bool bWantJump = false, bWantCrouch = false, bWantUse = false, bPathMove = false;
    spritetype *pEnemy = pBot->nTarget >= 0 ? &sprite[pBot->nTarget] : NULL;
    int nEnemyDist = pEnemy ? BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y) : 0;
    const bool bOnGround = pPlayer->pXSprite->height < 16 && pPlayer->posture != kPostureSwim;

    int nEdge = kEdgeWalk, nPortal = -1;
    int tx = pSprite->x, ty = pSprite->y;
    const bool bHasGoal = pBot->nGoalSect >= 0;
    if (bHasGoal)
    {
        FollowPath(pBot, pSprite, &tx, &ty, &nEdge, &nPortal);
        const int nDist = BotDist(pSprite->x, pSprite->y, tx, ty);
        if (pBot->nGoalSect >= 0 && (nDist > 48 || pBot->nPathPos < pBot->nPathLen))
        {
            nMoveAng = getangle(tx - pSprite->x, ty - pSprite->y), bPathMove = true;
            nMoveSpeed = 2048;
            nLookAng = nMoveAng;
        }
        if (pBot->bDetour && GoalReached(pBot, pSprite, 100))
        {
            // at the lift's switch: face it, push it, back to where it was going
            nMoveSpeed = 0;
            nLookAng = pBot->nDetourAng;
            if (klabs(AngDiff(pSprite->ang, pBot->nDetourAng)) < 100)
            {
                bWantUse = UseCheck(pPlayer) >= 0;
                pBot->bDetour = false;
                SetGoal(pBot, pBot->nSavedX, pBot->nSavedY, pBot->nSavedZ, pBot->nSavedSect);
            }
        }
        else if (nEdge == kEdgeLift && nPortal >= 0)
            LiftAction(pBot, pPlayer, nPortal, tx, ty, &nMoveAng, &nMoveSpeed, &nLookAng, &bWantUse);
        else if (nPortal >= 0 && BotDist(pSprite->x, pSprite->y, tx, ty) < 500)
        {
            if (nEdge == kEdgeJump && bOnGround)
                bWantJump = true;
            else if (nEdge == kEdgeDoor)
            {
                const int nDoor = DoorState(nPortal);
                if (nDoor != kDoorOpen)
                {
                    // walk up to it, face it and open it; wait while it moves
                    nLookAng = nMoveAng;
                    if (nDoor == kDoorClosed && !((nNow + nPlayer) % 12) && klabs(AngDiff(pSprite->ang, nMoveAng)) < 200)
                        bWantUse = UseCheck(pPlayer) >= 0;
                    if (BotDist(pSprite->x, pSprite->y, tx, ty) < 160)
                        nMoveSpeed = 0;
                }
            }
        }
    }

    const bool bHolding = nNow < pBot->nHoldUntil && pBot->nVantage >= 0;
    switch (pBot->nState)
    {
    case kBotStateHighGround:
        if (pBot->nGoalSect >= 0 && GoalReached(pBot, pSprite, 140))
        {
            // made it: hold it
            pBot->nHoldUntil = nNow + 30*6 + pBot->persona.nHighGround * 6;
            pBot->nState = kBotStateFight;
            pBot->nStateTime = nNow;
            pBot->nGoalSect = -1;
            pBot->nPathLen = 0;
        }
        else if (pBot->nGoalSect < 0)
            pBot->nState = kBotStateFight;
        break;
    case kBotStateAmbush:
    case kBotStateCamp:
        if (pBot->nGoalSect >= 0 && GoalReached(pBot, pSprite, 120))
        {
            if (!pBot->nWaitUntil)
            {
                int nPatience = pBot->nState == kBotStateAmbush ? 30*3 + pBot->persona.nAmbush * 2 : 30*4 + pBot->persona.nCamping * 3;
                pBot->nWaitUntil = nNow + nPatience + BotRand(pBot, 30*3);
            }
            nMoveSpeed = 0;
            // watch the way in, glancing about now and then
            nLookAng = pBot->nWatchAng;
            if (pBot->nState == kBotStateCamp && ((nNow / 60) & 3) == 3)
                nLookAng = (pBot->nWatchAng + ((nNow / 60) & 4 ? 300 : -300)) & 2047;
            bWantCrouch = pBot->persona.nCamping > 50 && pBot->nState == kBotStateCamp;
        }
        break;
    case kBotStateHunt:
        if (pBot->nSearchUntil && nNow < pBot->nSearchUntil)
        {
            nMoveSpeed = 0;
            nLookAng = (pSprite->ang + 70) & 2047; // turn round looking for it
        }
        break;
    case kBotStateFlee:
        if (pEnemy && nEnemyDist < 3000 && BotChance(pBot, 60))
            nLookAng = getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y); // backpedal, shooting
        if (bOnGround && BotChance(pBot, pBot->persona.nJump / 12))
            bWantJump = true;
        break;
    }
    if (bHolding && !pEnemy && pBot->nState != kBotStateHighGround)
    {
        const vec3_t *pSpot = &vantageSpot[pBot->nVantage];
        if (BotDist(pSprite->x, pSprite->y, pSpot->x, pSpot->y) < 250)
        {
            nMoveSpeed = 0;
            nLookAng = (pBot->nWatchAng + (((nNow / 50) & 3) == 3 ? 250 : 0)) & 2047;
        }
    }
    // walking about, it glances to the sides now and then instead of staring down the corridor
    if (!pEnemy && nMoveSpeed && pBot->nState != kBotStateFight && nEdge != kEdgeDoor && nNow >= pBot->nUnstickUntil)
    {
        if (nNow >= pBot->nGlanceNext)
        {
            pBot->nGlance = BotChance(pBot, 40) ? GaussRand(pBot, 260) : 0;
            pBot->nGlanceNext = nNow + 20 + BotRand(pBot, 60);
        }
        nLookAng = (nLookAng + pBot->nGlance) & 2047;
    }
    if (pBot->nState != kBotStateFight && pBot->nHurtTime && nNow - pBot->nHurtTime < 20 && !pEnemy)
        nLookAng = pBot->nWatchAng;

    // fighting
    int nWeapon = pPlayer->curWeapon;
    bool bFire = false, bAltFire = false;
    if (pEnemy)
    {
        PLAYER *pEnemyPlayer = SpritePlayer(pEnemy->index);
        const int nAngTo = getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y);
        // weapon for the distance
        if (nNow >= pBot->nWeaponNext && !pBot->nFireHold)
        {
            int nBestScore;
            bTargetAbove = pSprite->z - pEnemy->z > (48<<8);
            const int nBest = BestWeapon(pPlayer, nEnemyDist, &nBestScore);
            const int nCurScore = WeaponScore(pPlayer, nWeapon, nEnemyDist);
            bTargetAbove = false;
            if (nBest && nBest != nWeapon && (nBestScore > nCurScore * 5 / 4 || !nCurScore))
            {
                pInput->newWeapon = nBest;
                pBot->nWeaponNext = nNow + 45 + BotRand(pBot, 60);
            }
        }
        if (pBot->nState == kBotStateFight)
        {
            bPathMove = false;
            const BOTWEAPON *pWeapon = &gBotWeapon[ClipRange(nWeapon, 0, kWeaponMax-1)];
            int nWant = pWeapon->nBestDist * (130 - pBot->persona.nAggression * 6 / 10) / 100;
            if (pEnemyPlayer && powerupCheck(pEnemyPlayer, kPwUpDeathMask))
                nWant = max(nWant, 4000); // can't hurt it: keep away
            const bool bSeeNow = nNow - pBot->mem[pEnemyPlayer ? pEnemyPlayer->nPlayer : 0].nTime < 3 || !pEnemyPlayer;
            if (bHolding)
            {
                // holding a high spot: stay on it, back to it if it drifted off
                pBot->nWatchAng = getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y);
                const vec3_t *pSpot = &vantageSpot[pBot->nVantage];
                const int nOff = BotDist(pSprite->x, pSprite->y, pSpot->x, pSpot->y);
                nMoveSpeed = 0;
                if (nOff > 220)
                    nMoveAng = getangle(pSpot->x - pSprite->x, pSpot->y - pSprite->y), nMoveSpeed = 2048;
            }
            else if (bSeeNow && pEnemy->z - pSprite->z > (64<<8) && nWeapon != kWeaponPitchfork && nWeapon != kWeaponSprayCan
                && StayAbove(pBot) && nEnemyDist < 9000)
            {
                // it's down below and in sight: shoot down at it rather than jump after it
                nMoveSpeed = 0;
            }
            else if (nEnemyDist > nWant + 700 || !bSeeNow)
            {
                // close in; the path finds the way round things
                if (pBot->nGoalSect != pEnemy->sectnum || nNow - pBot->nRepathTime > 20)
                    SetGoal(pBot, pEnemy->x, pEnemy->y, pEnemy->z, pEnemy->sectnum);
                if (pBot->nGoalSect >= 0)
                {
                    FollowPath(pBot, pSprite, &tx, &ty, &nEdge, &nPortal);
                    nMoveAng = getangle(tx - pSprite->x, ty - pSprite->y), bPathMove = true;
                }
                else
                    nMoveAng = nAngTo;
                nMoveSpeed = 2048;
            }
            else if (nEnemyDist < nWant - 500)
            {
                nMoveAng = (nAngTo + 1024) & 2047; // back off
                nMoveSpeed = 2048;
            }
            else
                nMoveSpeed = 0;
            // dodge sideways; standing still at the right distance looks like a turret, so at
            // that distance it always moves one way or the other
            if (nNow >= pBot->nStrafeUntil)
            {
                const int nStrafe = pBot->persona.nStrafe;
                pBot->nStrafeDir = (BotChance(pBot, nStrafe) || !nMoveSpeed) ? (BotChance(pBot, 50) ? 1 : -1) : 0;
                pBot->nStrafeUntil = nNow + 10 + BotRand(pBot, 40 - nStrafe / 4);
            }
            if (pBot->nStrafeDir)
            {
                const int nSideAng = (nAngTo + pBot->nStrafeDir * 512) & 2047;
                if (nMoveSpeed)
                {
                    // blend the dodge into the movement
                    const int nDiff = AngDiff(nMoveAng, nSideAng);
                    nMoveAng = (nMoveAng + nDiff / 2) & 2047;
                }
                else
                {
                    nMoveAng = nSideAng;
                    nMoveSpeed = 2048;
                }
            }
            if (bOnGround && nNow >= pBot->nJumpNext && !bHolding)
            {
                if (BotChance(pBot, pBot->persona.nJump))
                    bWantJump = true;
                pBot->nJumpNext = nNow + 20 + BotRand(pBot, 50);
            }
            if (nNow < pBot->nCrouchUntil)
                bWantCrouch = true;
            else if (BotChance(pBot, 1) && nEnemyDist > 2000 && pBot->persona.nCamping > 40)
                pBot->nCrouchUntil = nNow + 20 + BotRand(pBot, 30);
        }

        if (nNow >= pBot->nReactAt)
        {
            // aim: where it is, plus a lead for slow shots, plus a wobble that settles while tracking
            const BOTWEAPON *pWeapon = &gBotWeapon[ClipRange(nWeapon, 0, kWeaponMax-1)];
            int ax = pEnemy->x, ay = pEnemy->y;
            if (pWeapon->nLeadSpeed)
            {
                const int t = divscale12(nEnemyDist, pWeapon->nLeadSpeed) * pBot->skill.nLead / 100;
                ax += (xvel[pEnemy->index] * t) >> 12;
                ay += (yvel[pEnemy->index] * t) >> 12;
            }
            pBot->nTrackFrames++;
            if (nNow >= pBot->nAimErrNext)
            {
                int nErr = pBot->skill.nAimError;
                nErr = nErr * ClipLow(100 - pBot->nTrackFrames, 35) / 100;
                if (gBotAdaptive)
                    nErr = nErr * nAdapt / 100;
                const int nSideSpeed = klabs(dmulscale30(xvel[pEnemy->index], Sin(nAngTo), -yvel[pEnemy->index], Cos(nAngTo)));
                nErr += nErr * ClipHigh(nSideSpeed >> 14, 8) / 8;
                if (!bOnGround)
                    nErr += nErr / 3;
                pBot->nAimErrGoal = GaussRand(pBot, nErr);
                pBot->nAimErrZ = GaussRand(pBot, nErr / 3);
                pBot->nAimErrNext = nNow + 6 + BotRand(pBot, 14);
            }
            pBot->nAimErr += (pBot->nAimErrGoal - pBot->nAimErr) / 4;
            nLookAng = (getangle(ax - pSprite->x, ay - pSprite->y) + pBot->nAimErr) & 2047;
            nLookZ = SpriteCenterZ(pEnemy);
            // shoot when it thinks it's on target
            const int nOff = klabs(AngDiff(pSprite->ang, nLookAng));
            const bool bSeeNow = !pEnemyPlayer || nNow - pBot->mem[pEnemyPlayer->nPlayer].nTime < 3;
            int nCone = pWeapon->nCone;
            if (nEnemyDist < 1500)
                nCone += nCone / 2;
            bFire = bSeeNow && nOff < nCone && nEnemyDist <= pWeapon->nMaxDist + 2000 && nEnemyDist >= pWeapon->nMinDist * 3 / 4;
            if (nWeapon == kWeaponPitchfork && nEnemyDist > 1000)
                bFire = false;
            // not a machine: now and then it stops shooting for a moment (re-aiming, nerves)
            if (nNow < pBot->nFirePauseUntil)
                bFire = false;
            else if (bFire && nWeapon != kWeaponTNT && BotRand(pBot, 1000) < (100 - pBot->skill.nTrigger) * 2)
                pBot->nFirePauseUntil = nNow + 8 + BotRand(pBot, 16);
            if (bFire)
            {
                switch (nWeapon)
                {
                case kWeaponShotgun:
                    bAltFire = nEnemyDist < 1200 && BotChance(pBot, 40);
                    break;
                case kWeaponTommy:
                    bAltFire = nEnemyDist < 1800 && BotChance(pBot, 8);
                    break;
                case kWeaponTesla:
                    bAltFire = BotChance(pBot, 6);
                    break;
                case kWeaponFlare:
                    bAltFire = nEnemyDist > 2500 && BotChance(pBot, 10);
                    break;
                }
            }
            // friendly fire in teams: don't shoot through a team mate
            if (bFire && gGameOptions.nGameType == kGameTypeTeams)
            {
                for (int p = connecthead; p >= 0 && bFire; p = connectpoint2[p])
                {
                    PLAYER *pMate = &gPlayer[p];
                    if (pMate == pPlayer || IsEnemyPlayer(pPlayer, pMate) || pMate->pXSprite->health <= 0)
                        continue;
                    const int nMateDist = BotDist(pSprite->x, pSprite->y, pMate->pSprite->x, pMate->pSprite->y);
                    if (nMateDist < nEnemyDist && klabs(AngDiff(pSprite->ang, getangle(pMate->pSprite->x - pSprite->x, pMate->pSprite->y - pSprite->y))) < 60)
                        bFire = false;
                }
            }
        }
        else
            nLookAng = pSprite->ang; // hasn't reacted yet
    }
    else
    {
        pBot->nTrackFrames = 0;
        if (nNow >= pBot->nWeaponNext && !pBot->nFireHold)
        {
            // something good in hand for whatever comes next
            const int nBest = BestWeapon(pPlayer, 2500, NULL);
            if (nBest && nBest != nWeapon && WeaponScore(pPlayer, nBest, 2500) > WeaponScore(pPlayer, nWeapon, 2500) * 5 / 4)
                pInput->newWeapon = nBest;
            pBot->nWeaponNext = nNow + 60 + BotRand(pBot, 60);
        }
    }

    // letting two others fight it out: watch, don't give itself away, unless it gets noticed
    if (pEnemy && nNow < pBot->nThirdPartyUntil)
    {
        if (FacingMe(pEnemy, pSprite, 250))
            pBot->nThirdPartyUntil = 0;
        else
        {
            bFire = false;
            if (pBot->nState == kBotStateFight)
                nMoveSpeed = 0;
        }
    }
    if (bFire)
        pBot->nLastShot = nNow;

    // TNT: hold to throw further, then let go
    if (nWeapon == kWeaponTNT)
    {
        if (pBot->nFireHold > 0)
        {
            bFire = true;
            if (--pBot->nFireHold == 0)
            {
                bFire = false;
                pBot->nFireRelease = nNow + 25;
            }
        }
        else if (bFire && nNow >= pBot->nFireRelease)
        {
            pBot->nFireHold = ClipRange((nEnemyDist - 800) / 140, 2, 26);
            bAltFire = false;
        }
        else
            bFire = false;
    }
    else
        pBot->nFireHold = 0;

    // stuck against something, or getting nowhere: jump, open, sidestep, then another way
    if (nMoveSpeed && bPathMove)
    {
        bool bStuck = false;
        // press "use" on whatever is in front (doors, turning walls, switches), but never on a level exit
        const bool bDoorNear = UseCheck(pPlayer) >= 0;
        const int nTargetDist = BotDist(pSprite->x, pSprite->y, tx, ty);
        if (BotDist(tx, ty, pBot->nProgressX, pBot->nProgressY) > 256 || !pBot->nProgressTime)
        {
            pBot->nProgressX = tx, pBot->nProgressY = ty;
            pBot->nProgressDist = nTargetDist;
            pBot->nProgressTime = nNow;
        }
        else if (nTargetDist < pBot->nProgressDist - 48)
        {
            pBot->nProgressDist = nTargetDist;
            pBot->nProgressTime = nNow;
        }
        else if (nNow - pBot->nProgressTime > 30)
        {
            bStuck = true;
            pBot->nProgressTime = nNow;
        }
        if (nNow >= pBot->nStuckCheck)
        {
            const int nMoved = BotDist(pSprite->x, pSprite->y, pBot->nStuckX, pBot->nStuckY);
            if (nMoved < 96 && pBot->nStuckCheck)
                bStuck = true;
            else if (nMoved >= 96 && nNow - pBot->nProgressTime < 20)
                pBot->nStuckCount = 0;
            pBot->nStuckX = pSprite->x;
            pBot->nStuckY = pSprite->y;
            pBot->nStuckCheck = nNow + 15;
        }
        {
            if (bStuck)
            {
                pBot->nStuckCount++;
                switch (pBot->nStuckCount)
                {
                case 1:
                    bWantJump = true;
                    bWantUse = bDoorNear;
                    break;
                case 2:
                    pBot->nUnstickUntil = nNow + 12; // step back
                    pBot->nUnstickStrafe = 1024;
                    bWantUse = bDoorNear;
                    break;
                case 3:
                    pBot->nUnstickUntil = nNow + 15; // and to the side
                    pBot->nUnstickStrafe = BotChance(pBot, 50) ? 512 : -512;
                    bWantUse = bDoorNear;
                    break;
                case 4:
                    if (nPortal >= 0)
                    {
                        BlockWall(pBot, nPortal);
                        if (pBot->nGoalSect >= 0)
                            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY);
                    }
                    break;
                default:
                    pBot->nStuckCount = 0;
                    if (pBot->nGoalSprite >= 0)
                        IgnoreItem(pBot, pBot->nGoalSprite, true);
                    if (pBot->nState == kBotStateCamp && pBot->nCampSpot >= 0 && nCampFails[pBot->nCampSpot] < 255)
                        nCampFails[pBot->nCampSpot]++;
                    pBot->nState = -1;
                    SetState(pBot, kBotStateRoam);
                    pBot->nNextThink = nNow;
                    break;
                }
            }
        }
    }
    else
    {
        pBot->nStuckCheck = 0;
        pBot->nProgressTime = 0;
        if (!nMoveSpeed)
            pBot->nStuckCount = 0;
    }
    if (nMoveSpeed && nNow >= pBot->nUnstickUntil)
        nMoveAng = AvoidWalls(pBot, pSprite, nMoveAng, bPathMove ? BotDist(pSprite->x, pSprite->y, tx, ty) : 400);
    if (nNow < pBot->nUnstickUntil)
    {
        nMoveAng = (nMoveAng + pBot->nUnstickStrafe) & 2047;
        nMoveSpeed = 2048;
    }
    // don't step off a ledge unless the way it is going leads down there
    if (nMoveSpeed && pPlayer->posture != kPostureSwim)
    {
        const bool bMeantDrop = bPathMove && nPortal >= 0 && BotDist(pSprite->x, pSprite->y, tx, ty) < 400 && !bHolding;
        if (!bMeantDrop && LedgeAhead(pSprite, nMoveAng))
        {
            const int nOther = (nMoveAng + 1024) & 2047;
            if (pBot->nStrafeDir && !LedgeAhead(pSprite, nOther))
                nMoveAng = nOther, pBot->nStrafeDir = -pBot->nStrafeDir;
            else
                nMoveSpeed = 0;
        }
    }

    // swimming: up or down towards where it is going
    if (pPlayer->posture == kPostureSwim)
    {
        const int nWantZ = nLookZ != INT32_MIN ? nLookZ : (pBot->nGoalSect >= 0 ? pBot->nGoalZ - (32<<8) : pSprite->z);
        if (nWantZ < pSprite->z - (16<<8))
            bWantJump = true;
        else if (nWantZ > pSprite->z + (16<<8))
            bWantCrouch = true;
    }

    // turn like a hand on a mouse: fast for big turns, settling at the end, never instantly
    const int nTurnDiff = AngDiff(pSprite->ang, nLookAng);
    int nTurn = nTurnDiff * 45 / 100;
    if (klabs(nTurnDiff) < 6)
        nTurn = nTurnDiff;
    nTurn = ClipRange(nTurn, -pBot->skill.nTurnSpeed, pBot->skill.nTurnSpeed);
    pInput->q16turn = fix16_from_int(nTurn);

    // look up or down
    int nWantLook = 0;
    if (nLookZ != INT32_MIN && nEnemyDist > 0)
    {
        // the look angle whose aim slope (horiz) points at the target: horiz = 100*tan(look), slope dz = -horiz*dist/8
        const float fHoriz = float(pPlayer->zWeapon - nLookZ) * 8.f / float(nEnemyDist);
        nWantLook = int(atanf(fHoriz / 100.f) * (1024.f / fPI)) + pBot->nAimErrZ;
        nWantLook = ClipRange(nWantLook, -340, 280);
    }
    const int nLook = fix16_to_int(pPlayer->q16look);
    const int nLookStep = ClipRange((nWantLook - nLook) / 3, -30, 30);
    pInput->q16mlook = fix16_from_int(nLookStep) >> 3;

    // keys
    if (nMoveSpeed)
    {
        const int nRel = (nMoveAng - (pSprite->ang + nTurn)) & 2047;
        pInput->forward = mulscale30(nMoveSpeed, Cos(nRel));
        pInput->strafe = -mulscale30(nMoveSpeed, Sin(nRel));
    }
    pInput->buttonFlags.jump = bWantJump;
    pInput->buttonFlags.crouch = bWantCrouch && !bWantJump;
    pInput->buttonFlags.shoot = bFire && !bAltFire;
    pInput->buttonFlags.shoot2 = bFire && bAltFire;
    if (bWantUse)
        pInput->keyFlags.action = 1;
    // a medkit when it's hurt and safe
    if (pPlayer->packSlots[kPackMedKit].curAmount > 0 && (nHealth >> 4) < 45 && (!pEnemy || nEnemyDist > 5000))
        pInput->useFlags.useMedKit = 1;

    if (gBotDebug == 2 && (nNow % 60) == nPlayer)
    {
        char buffer[160];
        BotDescribe(nPlayer, buffer, sizeof(buffer));
        const int nNext = pBot->nPathPos < pBot->nPathLen ? pBot->path[pBot->nPathPos] : -1;
        LOG_F(INFO, "Bot %s; at %d,%d,%d sector %d, opening %d (%d) to %d, goal sector %d, move %d look %d", buffer, (int)pSprite->x, (int)pSprite->y, (int)pSprite->z, (int)pSprite->sectnum,
            nNext, nNext >= 0 ? EdgeType(nNext) : -1, nNext >= 0 ? (int)wall[nNext].nextsector : -1, pBot->nGoalSect, nMoveSpeed, (int)pSprite->ang);
    }
    if (gBotWatch == nPlayer) // see through its eyes
    {
        gViewIndex = nPlayer;
        gView = pPlayer;
    }
}

//
// debugging aid: what every bot is up to
//

static void BotDescribe(int i, char *pzBuffer, int nSize)
{
    BOT *pBot = &gBot[i];
    PLAYER *pPlayer = &gPlayer[i];
    const char *pzTarget = "-";
    if (pBot->nTarget >= 0)
    {
        PLAYER *pTarget = SpritePlayer(pBot->nTarget);
        pzTarget = pTarget ? gProfile[pTarget->nPlayer].name : "monster";
    }
    const int nGoalDist = pBot->nGoalSect >= 0 && pPlayer->pSprite ? BotDist(pPlayer->pSprite->x, pPlayer->pSprite->y, pBot->nGoalX, pBot->nGoalY) : -1;
    Bsnprintf(pzBuffer, nSize, "%s (%s %s): %s%s, hp %d, weapon %d, target %s, path %d/%d, goal %d away, frags %d, stuck %d", gProfile[i].name,
        gBotSkillNames[pBot->nSkill], gBotPersonaNames[pBot->nPersona], pBot->nState >= 0 && pBot->nState < kBotStateMax ? pzBotStateNames[pBot->nState] : "?",
        pBot->bDetour ? " (to a lift switch)" : (pBot->nLiftWall >= 0 && nNow - pBot->nLiftSince < 30 ? " (lift)" : ""), pPlayer->pXSprite ? pPlayer->pXSprite->health >> 4 : 0, pPlayer->curWeapon, pzTarget, pBot->nPathPos, pBot->nPathLen, nGoalDist,
        pPlayer->fragCount, pBot->nStuckCount);
}

void botDrawDebug(void)
{
    if (!BotThinksHere() || !gBotDebug)
        return;
    int y = windowxy1.y + 30;
    for (int i = connecthead; i >= 0; i = connectpoint2[i])
    {
        if (!botIsBot(i))
            continue;
        char buffer[160];
        BotDescribe(i, buffer, sizeof(buffer));
        Bstrupr(buffer);
        printext256(windowxy1.x + 4 + 1, y + 1, 0, -1, buffer, xdim <= 640);
        printext256(windowxy1.x + 4, y, 31, -1, buffer, xdim <= 640);
        y += xdim <= 640 ? 8 : 10;
    }
}

//
// settings kept in the cfg
//

static int osdcmd_bot_match(osdcmdptr_t parm)
{
    if (gNetMode != NETWORK_NONE && myconnectindex != connecthead)
    {
        OSD_Printf("bot_match: only the host starts the game\n");
        return OSDCMD_OK;
    }
    if (parm->numparms >= 1)
        BotMatchSetLimits(Batol(parm->parms[0]), parm->numparms >= 2 ? Batol(parm->parms[1]) : -1);
    if (parm->numparms >= 4)
        BotMatchSetMap(Batol(parm->parms[2]) - 1, Batol(parm->parms[3]) - 1);
    if (parm->numparms >= 6)
        BotMatchSetMode(Batol(parm->parms[4]), Batol(parm->parms[5]));
    if (gNetMode != NETWORK_NONE) // the host of a network game: start it, with bot_net_count bots
        StartNetGame(NULL);
    else
        StartBotMatch(NULL);
    return OSDCMD_OK;
}

static int osdcmd_bot_rules(osdcmdptr_t parm) // the next bot_match's newer mutators and match rules
{
    if (parm->numparms < 5)
        return OSDCMD_SHOWHELP;
    BotMatchSetRules(Batol(parm->parms[0]), Batol(parm->parms[1]), Batol(parm->parms[2]), Batol(parm->parms[3]), Batol(parm->parms[4]));
    return OSDCMD_OK;
}

static int osdcmd_bot_sector(osdcmdptr_t parm) // map debugging: how bots see a sector's openings
{
    if (parm->numparms < 1)
        return OSDCMD_SHOWHELP;
    const int s = Batol(parm->parms[0]);
    if (s < 0 || s >= numsectors)
        return OSDCMD_OK;
    XSECTOR *pX = sector[s].extra > 0 ? &xsector[sector[s].extra] : NULL;
    if (pX)
        LOG_F(INFO, "sector %d: on floor %d off floor %d", s, pX->onFloorZ, pX->offFloorZ);
    LOG_F(INFO, "sector %d: type %d floor %d ceiling %d door %d lift %d x %d rx %d tx %d state %d busy %d push %d wallpush %d locked %d key %d",
        s, (int)sector[s].type, (int)sector[s].floorz, (int)sector[s].ceilingz, bDoorSect[s], IsLift(s), (int)sector[s].extra, pX ? pX->rxID : -1, pX ? pX->txID : -1,
        pX ? pX->state : -1, pX ? pX->busy : -1, pX ? pX->Push : -1, pX ? pX->Wallpush : -1, pX ? pX->locked : -1, pX ? pX->Key : -1);
    for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
    {
        XWALL *pXW = wall[w].extra > 0 ? &xwall[wall[w].extra] : NULL;
        LOG_F(INFO, "  wall %d -> sector %d edge %d plan %d cstat %d fails %d xwall %d push %d tx %d rx %d", w, (int)wall[w].nextsector,
            (int)wall[w].nextsector >= 0 ? EdgeType(w) : -1, (int)wall[w].nextsector >= 0 ? EdgeTypePlan(w) : -1, (int)wall[w].cstat, nWallFails[w], (int)wall[w].extra, pXW ? pXW->triggerPush : -1, pXW ? pXW->txID : -1, pXW ? pXW->rxID : -1);
    }
    for (int i = headspritesect[s]; i >= 0; i = nextspritesect[i])
        LOG_F(INFO, "  sprite %d type %d stat %d cstat %d xsprite %d push %d tx %d", i, (int)sprite[i].type, (int)sprite[i].statnum, (int)sprite[i].cstat, (int)sprite[i].extra,
            (int)sprite[i].extra > 0 ? xsprite[(int)sprite[i].extra].Push : -1, (int)sprite[i].extra > 0 ? xsprite[(int)sprite[i].extra].txID : -1);
    return OSDCMD_OK;
}

static int osdcmd_bot_order(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !BotThinksHere())
        return OSDCMD_SHOWHELP;
    if (!Bstrcasecmp(parm->parms[0], "follow"))
        botOrder(kBotOrderFollow);
    else if (!Bstrcasecmp(parm->parms[0], "hold"))
        botOrder(kBotOrderHold);
    else
        botOrder(kBotOrderFree);
    return OSDCMD_OK;
}

void botRegisterCvars(void)
{
    OSD_RegisterFunction("bot_order", "bot_order <follow|hold|free>: orders for the bots on your side", osdcmd_bot_order);
    OSD_RegisterFunction("bot_sector", "bot_sector <sector>: writes how bots see that sector's openings to the log", osdcmd_bot_sector);
    OSD_RegisterFunction("bot_match", "bot_match [frag limit] [time limit] [episode] [level] [game type 1-3] [mode 0-7]: starts a bot match with the BOTS menu's settings", osdcmd_bot_match);
    OSD_RegisterFunction("bot_rules", "bot_rules <size 0-2> <barrels 0-2> <akimbo 0-1> <rounds 1-9> <vote 0-1>: player size, TNT barrels, akimbo everything, rounds and the map vote of the next bot_match", osdcmd_bot_rules);
    static osdcvardata_t cvars[] = {
        { "bot_count", "number of bots in a bot match", (void *)&gBotCount, CVAR_INT, 1, kMaxPlayers - 1 },
        { "bot_net_count", "number of bots the host adds to a network game (MULTIPLAYER > BOTS)", (void *)&gBotNetCount, CVAR_INT, 0, kMaxPlayers - 1 },
        { "bot_skill", "bot difficulty (0: easy, 1: normal, 2: hard, 3: nightmare)", (void *)&gBotSkill, CVAR_INT, 0, kBotSkillMax - 1 },
        { "bot_personality", "bot personality (0: mixed, 1: balanced, 2: rusher, 3: camper, 4: trickster, 5: coward, 6: jumper)", (void *)&gBotPersona, CVAR_INT, 0, kBotPersonaMax - 1 },
        { "bot_accuracy", "bot aim, percent of the difficulty's", (void *)&gBotAccuracy, CVAR_INT, 10, 300 },
        { "bot_reaction", "bot reaction time, percent of the difficulty's", (void *)&gBotReaction, CVAR_INT, 10, 300 },
        { "bot_aggression", "bot aggression, percent of the personality's", (void *)&gBotAggression, CVAR_INT, 0, 200 },
        { "bot_jumping", "how much bots jump around, percent of the personality's", (void *)&gBotJumping, CVAR_INT, 0, 200 },
        { "bot_ambush", "how often bots ambush, percent of the personality's", (void *)&gBotAmbush, CVAR_INT, 0, 200 },
        { "bot_flank", "how often bots go round to flank, percent of the personality's", (void *)&gBotFlank, CVAR_INT, 0, 200 },
        { "bot_camping", "how much bots camp, percent of the personality's", (void *)&gBotCamping, CVAR_INT, 0, 200 },
        { "bot_taunts", "bots talk and taunt", (void *)&gBotTaunts, CVAR_BOOL, 0, 1 },
        { "career_stage", "the career stage you're on", (void *)&gCareerStage, CVAR_INT, 1, kCareerStages },
        { "career_wins", "career: matches won", (void *)&gCareerWins, CVAR_INT, 0, 1000000 },
        { "career_matches", "career: matches played", (void *)&gCareerMatches, CVAR_INT, 0, 1000000 },
        { "career_kills", "career: your kills", (void *)&gCareerKills, CVAR_INT, 0, 10000000 },
        { "career_deaths", "career: your deaths", (void *)&gCareerDeaths, CVAR_INT, 0, 10000000 },
        { "career_beststreak", "career: your longest killing streak", (void *)&gCareerBestStreak, CVAR_INT, 0, 1000000 },
        { "career_rivals", "career: who killed you how often", (void *)gCareerRivals, CVAR_STRING, 0, sizeof(gCareerRivals) },
        { "bot_tuning", "version of the bot presets the fine tuning was made for", (void *)&gBotTuning, CVAR_INT, 0, 100 },
        { "bot_grudges", "bots hold grudges against whoever kills them a lot, and hunt them", (void *)&gBotGrudges, CVAR_BOOL, 0, 1 },
        { "bot_adaptive", "bots slowly get better or worse to keep the match close", (void *)&gBotAdaptive, CVAR_BOOL, 0, 1 },
        { "bot_highground", "how much bots go for high places that see a lot, percent of the personality's", (void *)&gBotHighGround, CVAR_INT, 0, 200 },
        { "bot_handicap", "how much damage bots take (0: much less, 2: normal, 4: much more)", (void *)&gBotHandicap, CVAR_INT, 0, 4 },
        { "bot_cultists", "half the bots look like cultists", (void *)&gBotCultists, CVAR_BOOL, 0, 1 },
        { "bot_debug", "show what every bot is doing (1: on screen, 2: also in the log every 2 seconds)", (void *)&gBotDebug, CVAR_INT, 0, 2 },
        { "bot_watch", "watch the game through a bot's eyes (0: off, 1-7: that bot)", (void *)&gBotWatch, CVAR_INT|CVAR_NOSAVE, 0, kBotMaxBots },
    };
    for (auto &cv : cvars)
        OSD_RegisterCvar(&cv, osdcmd_cvar_set);
#define BOTEACHCVARS(n) \
        { "bot_skill_" #n, "this bot's difficulty (-1: same as bot_skill)", (void *)&gBotEachSkill[n-1], CVAR_INT, -1, kBotSkillMax - 1 }, \
        { "bot_personality_" #n, "this bot's personality (-1: same as bot_personality)", (void *)&gBotEachPersona[n-1], CVAR_INT, -1, kBotPersonaMax - 1 }, \
        { "bot_name_" #n, "this bot's name (empty: a random one)", (void *)gBotEachName[n-1], CVAR_STRING, 0, sizeof(gBotEachName[n-1]) }, \
        { "bot_team_" #n, "this bot's team in teams games (0: auto, 1: blue, 2: red)", (void *)&gBotEachTeam[n-1], CVAR_INT, 0, 2 }, \
        { "bot_handicap_" #n, "how much damage this bot takes (-1: same as bot_handicap, 0: much less ... 4: much more)", (void *)&gBotEachHandicap[n-1], CVAR_INT, -1, 4 }, \
        { "bot_accuracy_" #n, "this bot's aim, percent of bot_accuracy", (void *)&gBotEachTune[n-1][kBotTuneAim], CVAR_INT, 10, 300 }, \
        { "bot_reaction_" #n, "this bot's reaction time, percent of bot_reaction", (void *)&gBotEachTune[n-1][kBotTuneReaction], CVAR_INT, 10, 300 }, \
        { "bot_aggression_" #n, "this bot's aggression, percent of bot_aggression", (void *)&gBotEachTune[n-1][kBotTuneAggression], CVAR_INT, 0, 300 }, \
        { "bot_jumping_" #n, "this bot's jumping, percent of bot_jumping", (void *)&gBotEachTune[n-1][kBotTuneJumping], CVAR_INT, 0, 300 }, \
        { "bot_ambush_" #n, "this bot's ambushes, percent of bot_ambush", (void *)&gBotEachTune[n-1][kBotTuneAmbush], CVAR_INT, 0, 300 }, \
        { "bot_flank_" #n, "this bot's flanking, percent of bot_flank", (void *)&gBotEachTune[n-1][kBotTuneFlank], CVAR_INT, 0, 300 }, \
        { "bot_camping_" #n, "this bot's camping, percent of bot_camping", (void *)&gBotEachTune[n-1][kBotTuneCamping], CVAR_INT, 0, 300 }, \
        { "bot_highground_" #n, "this bot's going for high ground, percent of bot_highground", (void *)&gBotEachTune[n-1][kBotTuneHighGround], CVAR_INT, 0, 300 },
    static osdcvardata_t eachCvars[] = {
        BOTEACHCVARS(1) BOTEACHCVARS(2) BOTEACHCVARS(3) BOTEACHCVARS(4) BOTEACHCVARS(5) BOTEACHCVARS(6) BOTEACHCVARS(7)
    };
#undef BOTEACHCVARS
    for (auto &cv : eachCvars)
        OSD_RegisterCvar(&cv, osdcmd_cvar_set);
}
