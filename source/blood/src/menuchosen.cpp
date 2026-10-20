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

// Menus added by NotBlood Chosen (online lobbies, ...). menu.cpp links to them and calls
// SetupChosenMenus() from SetupMenus().

#include "compat.h"
#include "build.h"
#include "mmulti.h"
#include "common_game.h"
#include "blood.h"
#include "bot.h"
#include "chosenmodes.h"
#include "coopsave.h"
#include "fx_man.h"
#include "gamemenu.h"
#include "globals.h"
#include "menu.h"
#include "menuchosen.h"
#include "mods.h"
#include "netrelay.h"
#include "keyboard.h"
#include "timer.h"
#include "config.h"
#include "network.h"
#include "online.h"
#include "screen.h"
#include "sound.h"
#include "view.h"
#include "player.h"

extern CGameMenuItemQAV itemBloodQAV;

// ---- ONLINE ---------------------------------------------------------------------------------

CGameMenu menuOnline;
CGameMenu menuOnlineLobbies;

static void OnlineHost(CGameMenuItemChain *);
static void StartOnlineNetGame(NETWORKMODE nMode);
static void OnlineJoinByName(CGameMenuItemChain *);
static void OnlineJoinListed(CGameMenuItemChain *);
static void SetupOnlineLobbies(CGameMenuItemChain *);
static void UpdateOnlineStatus(CGameMenuItem *);

static char zOnlineStatus[64];
static char zOnlineDashboard[80];

CGameMenuItemTitle itemOnlineTitle("ONLINE", 1, 160, 20, 2038);
CGameMenuItemZEdit itemOnlineServer("SERVER:", 3, 46, 42, 228, gOnlineServer, sizeof(gOnlineServer), 0, NULL, 0);
CGameMenuItemZEdit itemOnlineKey("SERVER KEY:", 3, 46, 52, 228, gOnlineKey, sizeof(gOnlineKey), 0, NULL, 0);
CGameMenuItemZEdit itemOnlineLobby("LOBBY NAME:", 3, 46, 62, 228, gOnlineLobby, sizeof(gOnlineLobby), 0, NULL, 0);
CGameMenuItemSlider itemOnlinePlayers("PLAYERS:", 3, 46, 72, 228, 2, 2, kMaxPlayers, 1, NULL, -1, -1, kMenuSliderValue);
CGameMenuItemChain itemOnlineHost("HOST A LOBBY", 3, 0, 90, 320, 1, NULL, -1, OnlineHost, 0);
CGameMenuItemChain itemOnlineFind("FIND LOBBIES", 3, 0, 102, 320, 1, &menuOnlineLobbies, -1, SetupOnlineLobbies, 0);
CGameMenuItemChain itemOnlineJoin("JOIN LOBBY BY NAME", 3, 0, 114, 320, 1, NULL, -1, OnlineJoinByName, 0);
CGameMenuItemZBool itemOnlineLanStats("SHOW MY LAN GAMES ON THE DASHBOARD:", 3, 46, 132, 228, false, NULL, NULL, NULL);
CGameMenuItemText itemOnlineStatus(zOnlineStatus, 3, 160, 150, 1);
CGameMenuItemText itemOnlineDashboard(zOnlineDashboard, 3, 160, 165, 1);

#define kMaxListedLobbies 8
static char zLobbyNames[kMaxListedLobbies][25];
static char zLobbyInfo[kMaxListedLobbies][64];
static char zLobbyText[kMaxListedLobbies][25];

CGameMenuItemTitle itemOnlineLobbiesTitle("LOBBIES", 1, 160, 20, 2038);
CGameMenuItemChain itemOnlineLobby0(zLobbyText[0], 3, 30, 45, 260, 0, NULL, -1, OnlineJoinListed, 0, 0, zLobbyInfo[0]);
CGameMenuItemChain itemOnlineLobby1(zLobbyText[1], 3, 30, 57, 260, 0, NULL, -1, OnlineJoinListed, 1, 0, zLobbyInfo[1]);
CGameMenuItemChain itemOnlineLobby2(zLobbyText[2], 3, 30, 69, 260, 0, NULL, -1, OnlineJoinListed, 2, 0, zLobbyInfo[2]);
CGameMenuItemChain itemOnlineLobby3(zLobbyText[3], 3, 30, 81, 260, 0, NULL, -1, OnlineJoinListed, 3, 0, zLobbyInfo[3]);
CGameMenuItemChain itemOnlineLobby4(zLobbyText[4], 3, 30, 93, 260, 0, NULL, -1, OnlineJoinListed, 4, 0, zLobbyInfo[4]);
CGameMenuItemChain itemOnlineLobby5(zLobbyText[5], 3, 30, 105, 260, 0, NULL, -1, OnlineJoinListed, 5, 0, zLobbyInfo[5]);
CGameMenuItemChain itemOnlineLobby6(zLobbyText[6], 3, 30, 117, 260, 0, NULL, -1, OnlineJoinListed, 6, 0, zLobbyInfo[6]);
CGameMenuItemChain itemOnlineLobby7(zLobbyText[7], 3, 30, 129, 260, 0, NULL, -1, OnlineJoinListed, 7, 0, zLobbyInfo[7]);
CGameMenuItemChain itemOnlineRefresh("REFRESH", 3, 0, 148, 320, 1, NULL, -1, SetupOnlineLobbies, 0);
CGameMenuItemText itemOnlineLobbiesStatus(zOnlineStatus, 3, 160, 165, 1);

static CGameMenuItemChain *const pLobbyItems[kMaxListedLobbies] = {
    &itemOnlineLobby0, &itemOnlineLobby1, &itemOnlineLobby2, &itemOnlineLobby3,
    &itemOnlineLobby4, &itemOnlineLobby5, &itemOnlineLobby6, &itemOnlineLobby7,
};

static void SetOnlineStatus(const char *pzText)
{
    Bstrncpyz(zOnlineStatus, pzText, sizeof(zOnlineStatus));
    Bstrupr(zOnlineStatus);
}

static void UpdateOnlineStatus(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gOnlineReportLan = itemOnlineLanStats.at20;
    if (gOnlineServer[0])
        Bsnprintf(zOnlineDashboard, sizeof(zOnlineDashboard), "LIVE STATS: %s IN A WEB BROWSER", gOnlineServer);
    else
        zOnlineDashboard[0] = 0;
    Bstrupr(zOnlineDashboard);
}

static void StartOnlineNetGame(NETWORKMODE nMode)
{
    sndStopSong();
    FX_StopAllSounds();
    UpdateDacs(0, true);
    gNetMode = nMode;
    netInitialize(false);
    gGameMenuMgr.Deactivate();
    gQuitGame = gRestartGame = true;
}

static void OnlineHost(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gNetPort = strtoul(zNetPortBuffer, NULL, 10);
    if (!gNetPort)
        gNetPort = kNetDefaultPort;
    if (!onlineHostLobby(itemOnlinePlayers.nValue, false))
    {
        SetOnlineStatus(onlineStatusText());
        return;
    }
    gNetPlayers = itemOnlinePlayers.nValue;
    StartOnlineNetGame(NETWORK_SERVER);
}

static void OnlineJoin(const char *pzLobby)
{
    if (!onlineJoinLobby(pzLobby, false))
    {
        SetOnlineStatus(onlineStatusText());
        return;
    }
    StartOnlineNetGame(NETWORK_CLIENT);
}

static void OnlineJoinByName(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    OnlineJoin(gOnlineLobby);
}

static void OnlineJoinListed(CGameMenuItemChain *pItem)
{
    int i = ClipRange(pItem->at30, 0, kMaxListedLobbies-1);
    OnlineJoin(zLobbyNames[i]);
}

static void SetupOnlineLobbies(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    int nCount = onlineListLobbies(zLobbyNames, zLobbyInfo, kMaxListedLobbies);
    SetOnlineStatus(onlineStatusText());
    for (int i = 0; i < kMaxListedLobbies; i++)
    {
        bool bShow = i < nCount;
        Bstrncpyz(zLobbyText[i], bShow ? zLobbyNames[i] : "", sizeof(zLobbyText[i]));
        Bstrupr(zLobbyText[i]);
        Bstrupr(zLobbyInfo[i]);
        if (!bShow)
            zLobbyInfo[i][0] = 0;
        pLobbyItems[i]->bEnable = bShow;
        pLobbyItems[i]->bNoDraw = !bShow;
    }
    menuOnlineLobbies.SetFocusItem(nCount > 0 ? (CGameMenuItem *)pLobbyItems[0] : (CGameMenuItem *)&itemOnlineRefresh);
}

void SetupOnlineMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    itemOnlineLanStats.at20 = gOnlineReportLan != 0;
    SetOnlineStatus(onlineStatusText());
}

// ---- CO-OP SAVES ---------------------------------------------------------------------------

CGameMenu menuCoopLoad;
CGameMenu menuCoopSlots;
CGameMenu menuCoopSave;

static void SetupCoopSlots(CGameMenuItemChain *);
static void CoopSlotChosen(CGameMenuItemChain *);
static void CoopSaveChosen(CGameMenuItemChain *);
static void UpdateCoopOptions(CGameMenuItemZBool *);

static int nCoopSlotsType;
static char zCoopSlotsTitle[24];
static char zCoopSlotText[kCoopSlots+1][40];
static char zCoopSlotInfo[kCoopSlots+1][24];
static char zCoopSaveText[kCoopSlots][40];
static char zCoopSaveInfo[kCoopSlots][24];

CGameMenuItemTitle itemCoopLoadTitle("LOAD CO-OP", 1, 160, 20, 2038);
CGameMenuItemChain itemCoopLoadMid("CONTINUE WHERE YOU SAVED", 1, 0, 45, 320, 1, &menuCoopSlots, -1, SetupCoopSlots, kCoopSaveMid);
CGameMenuItemText itemCoopLoadMidHelp("EVERYTHING AS IT WAS: POSITIONS, KEYS, ENEMIES", 3, 160, 63, 1);
CGameMenuItemChain itemCoopLoadCarry("RESTART LEVEL WITH YOUR GEAR", 1, 0, 82, 320, 1, &menuCoopSlots, -1, SetupCoopSlots, kCoopSaveCarry);
CGameMenuItemText itemCoopLoadCarryHelp("THE LEVEL STARTS OVER, EVERYBODY KEEPS WEAPONS AND ITEMS", 3, 160, 100, 1);
CGameMenuItemZBool itemCoopLoadOrder("NEW PLAYERS GET:", 3, 46, 125, 228, false, UpdateCoopOptions, "LEFTOVER GEAR", "PITCHFORK ONLY");
CGameMenuItemZBool itemCoopLoadLeave("AUTOSAVE WHEN A PLAYER LEAVES:", 3, 46, 137, 228, true, UpdateCoopOptions, NULL, NULL);
CGameMenuItemText itemCoopLoadHelp1("PLAYERS GET THEIR GEAR BY NAME. NEW PLAYERS", 3, 160, 157, 1);
CGameMenuItemText itemCoopLoadHelp2("START AT THE LEVEL START.", 3, 160, 167, 1);

CGameMenuItemTitle itemCoopSlotsTitle(zCoopSlotsTitle, 1, 160, 20, 2038);
#define COOP_SLOT_ITEM(n) CGameMenuItemChain itemCoopSlot##n(zCoopSlotText[n], 3, 20, 42 + n*13, 280, 0, NULL, -1, CoopSlotChosen, n, 0, zCoopSlotInfo[n]);
COOP_SLOT_ITEM(0) COOP_SLOT_ITEM(1) COOP_SLOT_ITEM(2) COOP_SLOT_ITEM(3) COOP_SLOT_ITEM(4)
COOP_SLOT_ITEM(5) COOP_SLOT_ITEM(6) COOP_SLOT_ITEM(7) COOP_SLOT_ITEM(8)
static CGameMenuItemChain *const pCoopSlotItems[kCoopSlots+1] = { &itemCoopSlot0, &itemCoopSlot1, &itemCoopSlot2, &itemCoopSlot3,
    &itemCoopSlot4, &itemCoopSlot5, &itemCoopSlot6, &itemCoopSlot7, &itemCoopSlot8 };

CGameMenuItemTitle itemCoopSaveTitle("SAVE CO-OP", 1, 160, 20, 2038);
#define COOP_SAVE_ITEM(n) CGameMenuItemChain itemCoopSave##n(zCoopSaveText[n], 3, 20, 45 + n*13, 280, 0, NULL, -1, CoopSaveChosen, n, 0, zCoopSaveInfo[n]);
COOP_SAVE_ITEM(0) COOP_SAVE_ITEM(1) COOP_SAVE_ITEM(2) COOP_SAVE_ITEM(3)
COOP_SAVE_ITEM(4) COOP_SAVE_ITEM(5) COOP_SAVE_ITEM(6) COOP_SAVE_ITEM(7)
static CGameMenuItemChain *const pCoopSaveItems[kCoopSlots] = { &itemCoopSave0, &itemCoopSave1, &itemCoopSave2, &itemCoopSave3,
    &itemCoopSave4, &itemCoopSave5, &itemCoopSave6, &itemCoopSave7 };
CGameMenuItemText itemCoopSaveHelp("SAVED ON EVERY PLAYER'S MACHINE", 3, 160, 160, 1);

static void UpdateCoopOptions(CGameMenuItemZBool *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gCoopMatchOrder = itemCoopLoadOrder.at20;
    gCoopAutosaveLeave = itemCoopLoadLeave.at20;
}

static void describeSlot(int nType, int nSlot, char *pzText, int nTextSize, char *pzInfo, int nInfoSize)
{
    char level[64];
    bool bSave = coopDescribeSave(nType, nSlot, level, sizeof(level), pzInfo, nInfoSize);
    const char *pzName = nSlot < kCoopSlots ? NULL : (nType == kCoopSaveCarry ? "LEVEL START" : "PLAYER LEFT");
    char slot[16];
    if (pzName)
        Bsnprintf(slot, sizeof(slot), "%s:", pzName);
    else
        Bsnprintf(slot, sizeof(slot), "%d.", nSlot+1);
    Bsnprintf(pzText, nTextSize, "%s %s", slot, bSave ? level : "EMPTY");
    if (!bSave)
        pzInfo[0] = 0;
    Bstrupr(pzInfo);
}

void SetupCoopLoadMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    itemCoopLoadOrder.at20 = gCoopMatchOrder != 0;
    itemCoopLoadLeave.at20 = gCoopAutosaveLeave != 0;
}

static void SetupCoopSlots(CGameMenuItemChain *pItem)
{
    nCoopSlotsType = pItem->at30;
    Bstrncpyz(zCoopSlotsTitle, nCoopSlotsType == kCoopSaveMid ? "CONTINUE" : "RESTART LEVEL", sizeof(zCoopSlotsTitle));
    int nFocus = -1;
    for (int i = 0; i <= kCoopSlots; i++)
    {
        describeSlot(nCoopSlotsType, i, zCoopSlotText[i], sizeof(zCoopSlotText[i]), zCoopSlotInfo[i], sizeof(zCoopSlotInfo[i]));
        pCoopSlotItems[i]->bEnable = zCoopSlotInfo[i][0] != 0;
        if (nFocus < 0 && pCoopSlotItems[i]->bEnable)
            nFocus = i;
    }
    menuCoopSlots.SetFocusItem(pCoopSlotItems[nFocus < 0 ? 0 : nFocus]);
}

void OpenCoopSlotsMenu(int nType)
{
    CGameMenuItemChain *pItem = nType == kCoopSaveMid ? &itemCoopLoadMid : &itemCoopLoadCarry;
    SetupCoopSlots(pItem);
    gGameMenuMgr.Push(&menuCoopSlots, -1);
}

static void CoopSlotChosen(CGameMenuItemChain *pItem)
{
    if (numplayers > 1 && myconnectindex != connecthead)
    {
        viewSetMessage("Only the host can load a co-op game");
        return;
    }
    int const nSlot = ClipRange(pItem->at30, 0, kCoopSlots);
    bool const bOk = nCoopSlotsType == kCoopSaveMid ? coopLoadMidLevel(nSlot) : coopContinueGame(nSlot);
    if (!bOk)
        viewSetMessage("Could not load the co-op save");
}

void SetupCoopSaveMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    for (int i = 0; i < kCoopSlots; i++)
        describeSlot(kCoopSaveMid, i, zCoopSaveText[i], sizeof(zCoopSaveText[i]), zCoopSaveInfo[i], sizeof(zCoopSaveInfo[i]));
}

static void CoopSaveChosen(CGameMenuItemChain *pItem)
{
    coopRequestSave(ClipRange(pItem->at30, 0, kCoopSlots-1));
    gGameMenuMgr.Deactivate();
}

// ---- FIND LAN GAMES --------------------------------------------------------------------------

CGameMenu menuLanGames;

static void LanGameChosen(CGameMenuItemChain *);
void SetupLanGamesMenu(CGameMenuItemChain *);

#define kMaxLanGames 8
static LANGAME lanGames[kMaxLanGames];
static char zLanText[kMaxLanGames][40];
static char zLanInfo[kMaxLanGames][24];
static char zLanStatus[64];

CGameMenuItemTitle itemLanTitle("LAN GAMES", 1, 160, 20, 2038);
#define LAN_ITEM(n) CGameMenuItemChain itemLan##n(zLanText[n], 3, 30, 45 + n*12, 260, 0, NULL, -1, LanGameChosen, n, 0, zLanInfo[n]);
LAN_ITEM(0) LAN_ITEM(1) LAN_ITEM(2) LAN_ITEM(3) LAN_ITEM(4) LAN_ITEM(5) LAN_ITEM(6) LAN_ITEM(7)
static CGameMenuItemChain *const pLanItems[kMaxLanGames] = { &itemLan0, &itemLan1, &itemLan2, &itemLan3, &itemLan4, &itemLan5, &itemLan6, &itemLan7 };
CGameMenuItemChain itemLanRefresh("LOOK AGAIN", 3, 0, 150, 320, 1, NULL, -1, SetupLanGamesMenu, 0);
CGameMenuItemText itemLanStatus(zLanStatus, 3, 160, 168, 1);

void SetupLanGamesMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    lanScanStart();
    int nCount = 0;
    for (uint32_t nStart = timerGetTicks(); timerGetTicks() - nStart < 1500;)
    {
        handleevents();
        nCount = lanScanPoll(lanGames, kMaxLanGames);
        viewLoadingScreen(gMenuPicnum, "Multiplayer", NULL, "Looking for games on this network");
        videoNextPage();
    }
    nCount = lanScanPoll(lanGames, kMaxLanGames);
    lanScanStop();
    for (int i = 0; i < kMaxLanGames; i++)
    {
        bool const bShow = i < nCount;
        pLanItems[i]->bNoDraw = !bShow;
        pLanItems[i]->bEnable = bShow;
        zLanText[i][0] = zLanInfo[i][0] = 0;
        if (!bShow)
            continue;
        LANGAME const &g = lanGames[i];
        LOG_F(INFO, "LAN game: %s at %s:%d, %s, mod \"%s\", %d/%d players", g.szHost, g.szAddress, g.nPort, g.szVersion, g.szMod, g.nPlayers, g.nMaxPlayers);
        bool const bSameVersion = !Bstrcasecmp(g.szVersion, onlineVersion());
        bool const bSameMod = !Bstrcasecmp(g.szMod, modCurrentName());
        Bsnprintf(zLanText[i], sizeof(zLanText[i]), "%s  %s", g.szHost, g.szAddress);
        if (!bSameVersion)
            Bsnprintf(zLanInfo[i], sizeof(zLanInfo[i]), "OTHER VERSION");
        else if (!bSameMod)
            Bsnprintf(zLanInfo[i], sizeof(zLanInfo[i]), "OTHER MOD");
        else
            Bsnprintf(zLanInfo[i], sizeof(zLanInfo[i]), "%d/%d", g.nPlayers, g.nMaxPlayers);
        Bstrupr(zLanText[i]);
        pLanItems[i]->bEnable = bSameVersion && bSameMod;
    }
    char address[32];
    netLocalAddress(address, sizeof(address));
    if (nCount)
        Bsnprintf(zLanStatus, sizeof(zLanStatus), "YOUR ADDRESS: %s", address);
    else
        Bsnprintf(zLanStatus, sizeof(zLanStatus), "NO GAMES FOUND - HOST ONE FIRST");
    menuLanGames.SetFocusItem(nCount && pLanItems[0]->bEnable ? (CGameMenuItem *)pLanItems[0] : (CGameMenuItem *)&itemLanRefresh);
}

static void LanGameChosen(CGameMenuItemChain *pItem)
{
    LANGAME const &g = lanGames[ClipRange(pItem->at30, 0, kMaxLanGames-1)];
    Bstrncpyz(gNetAddress, g.szAddress, sizeof(gNetAddress));
    gNetPort = g.nPort ? g.nPort : kNetDefaultPort;
    StartOnlineNetGame(NETWORK_CLIENT);
}

// HOST A GAME: the address the others need
static char zHostAddress[48];
CGameMenuItemText itemNetworkHostAddress(zHostAddress, 3, 160, 125, 1);
CGameMenuItemText itemNetworkHostHelp("OTHERS FIND THIS GAME WITH FIND LAN GAMES", 3, 160, 137, 1);

void SetupNetworkHostAddress(void)
{
    char address[32];
    netLocalAddress(address, sizeof(address));
    Bsnprintf(zHostAddress, sizeof(zHostAddress), "YOUR ADDRESS: %s", address);
}

// main menu: which build this is
static char zVersionText[48];
CGameMenuItemText itemMainVersion(zVersionText, 3, 160, 186, 1);

// ---- MODS ----------------------------------------------------------------------------------

CGameMenu menuMods;

static void ModChosen(CGameMenuItemChain *);

static char zModText[kMaxModEntries][40];
static char zModInfo[kMaxModEntries][16];

CGameMenuItemTitle itemModsTitle("MODS", 1, 160, 20, 2038);
#define MOD_ITEM(n) CGameMenuItemChain itemMod##n(zModText[n], 3, 30, 38 + n*11, 260, 0, NULL, -1, ModChosen, n, 0, zModInfo[n]);
MOD_ITEM(0) MOD_ITEM(1) MOD_ITEM(2) MOD_ITEM(3) MOD_ITEM(4) MOD_ITEM(5)
MOD_ITEM(6) MOD_ITEM(7) MOD_ITEM(8) MOD_ITEM(9) MOD_ITEM(10) MOD_ITEM(11)
static CGameMenuItemChain *const pModItems[kMaxModEntries] = { &itemMod0, &itemMod1, &itemMod2, &itemMod3, &itemMod4, &itemMod5,
    &itemMod6, &itemMod7, &itemMod8, &itemMod9, &itemMod10, &itemMod11 };
CGameMenuItemText itemModsHelp1("ADD-ONS GO IN THE ADDONS FOLDER, MODS IN MODS", 3, 160, 175, 1);
CGameMenuItemText itemModsHelp2("CHOOSING ONE RESTARTS THE GAME", 3, 160, 185, 1);

void SetupModsMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    int const nCount = modScan();
    int nFocus = 0;
    for (int i = 0; i < kMaxModEntries; i++)
    {
        MODENTRY const *pMod = i < nCount ? modEntry(i) : NULL;
        pModItems[i]->bEnable = pMod != NULL;
        pModItems[i]->bNoDraw = pMod == NULL;
        zModText[i][0] = zModInfo[i][0] = 0;
        if (!pMod)
            continue;
        Bstrncpyz(zModText[i], pMod->szName, sizeof(zModText[i]));
        Bstrupr(zModText[i]);
        if (modIsActive(pMod))
        {
            Bstrncpyz(zModInfo[i], "PLAYING", sizeof(zModInfo[i]));
            nFocus = i;
        }
        else
            Bstrncpyz(zModInfo[i], pMod->nKind == kModFolder ? (pMod->szIni[0] ? "EPISODES" : "FOLDER") :
                pMod->nKind == kModGroup ? "PACK" : pMod->nKind == kModIni ? "EPISODES" : pMod->nKind == kModExtraCrispy ? "GORY KILLS" : "",
                sizeof(zModInfo[i]));
    }
    menuMods.SetFocusItem(pModItems[nFocus]);
}

static void ModChosen(CGameMenuItemChain *pItem)
{
    MODENTRY const *pMod = modEntry(pItem->at30);
    if (!pMod)
        return;
    if (modIsActive(pMod))
    {
        gGameMenuMgr.Deactivate();
        return;
    }
    if (modSelect(pItem->at30))
        modRestart(pMod->szName);
    else
        viewSetMessage("Could not save the mod choice");
}

// ---- MATCH STATS, BOT ORDERS (the game's menu during a match) ------------------------------

CGameMenu menuMatchStats;
CGameMenu menuBotOrders;

static void DrawMatchStats(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    modesDrawStats(40);
}

static void BotOrder(CGameMenuItemChain *pItem)
{
    botOrder(pItem->at30);
    gGameMenuMgr.Deactivate();
}

CGameMenuItemTitle itemMatchStatsTitle("MATCH STATS", 1, 160, 20, 2038);
CGameMenuItemText itemMatchStatsTable("", 3, 160, 40, 1);
static void MatchStatsBack(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gGameMenuMgr.PostPop();
}
CGameMenuItemChain itemMatchStatsBack("BACK", 1, 0, 170, 320, 1, NULL, -1, MatchStatsBack, 0);

CGameMenuItemTitle itemBotOrdersTitle("BOT ORDERS", 1, 160, 20, 2038);
CGameMenuItemChain itemBotOrderFollow("FOLLOW ME", 1, 0, 60, 320, 1, NULL, -1, BotOrder, kBotOrderFollow);
CGameMenuItemChain itemBotOrderHold("HOLD THIS SPOT", 1, 0, 80, 320, 1, NULL, -1, BotOrder, kBotOrderHold);
CGameMenuItemChain itemBotOrderFree("GO AHEAD", 1, 0, 100, 320, 1, NULL, -1, BotOrder, kBotOrderFree);
CGameMenuItemText itemBotOrdersHelp1("FOR THE BOTS ON YOUR SIDE (CO-OP AND TEAMS)", 3, 160, 130, 1);
CGameMenuItemText itemBotOrdersHelp2("ALSO: CONSOLE bot_order follow|hold|free", 3, 160, 140, 1);

// ---- BOTS ---------------------------------------------------------------------------------
// The same menu for a match against bots (MULTIPLAYER > PLAY WITH BOTS) and for the bots of a
// network game (MULTIPLAYER > BOTS in the game setup); in a network game it only sets them up.

CGameMenu menuBots;
CGameMenu menuBotsTuning;
CGameMenu menuBotsEach;
CGameMenu menuTeams;

static bool bBotsNet; // opened from a network game's setup

static void BotsSlider(CGameMenuItemSlider *pItem);
static void BotsSetSkill(CGameMenuItemZCycle *pItem);
static void BotsSetPersona(CGameMenuItemZCycle *pItem);
static void BotsSetEach(CGameMenuItemZCycle *pItem);
static void BotsEachSlider(CGameMenuItemSlider *pItem);
static void BotsEachName(CGameMenuItemZEdit *pItem, CGameMenuEvent *pEvent);
static void BotsEachReset(CGameMenuItemChain *pItem);

static void BotsSetupEach(CGameMenuItemChain *pItem);
static void BotsToggle(CGameMenuItemZBool *pItem);
static void BotsSetHandicap(CGameMenuItemZCycle *pItem);
static void UpdateBotsInfo(CGameMenuItem *pItem);

static const char *pzBotsEachSkill[kBotSkillMax+1] = { "SAME AS ALL", "EASY", "NORMAL", "HARD", "NIGHTMARE" };
static const char *pzBotsEachPersona[kBotPersonaMax+1] = { "SAME AS ALL", "MIXED", "BALANCED", "RUSHER", "CAMPER", "TRICKSTER", "COWARD", "JUMPER" };
static const char *pzBotsHandicap[5] = { "MUCH LESS", "LESS", "NORMAL", "MORE", "MUCH MORE" };
static const char *pzBotsEachHandicap[6] = { "SAME AS ALL", "MUCH LESS", "LESS", "NORMAL", "MORE", "MUCH MORE" };
static const char *pzBotsTeams[3] = { "AUTO", "BLUE", "RED" };
static const char *pzBotsNumbers[kBotMaxBots] = { "1", "2", "3", "4", "5", "6", "7" };
static char zBotsInfo[80], zBotsInfo2[80];

CGameMenuItemChain itemNetworkBots("PLAY WITH BOTS", 1, 0, 118, 320, 1, &menuBots, -1, SetupBotsMenu, 0);

static void BotsStart(CGameMenuItemChain *pItem);
static void BotsWatch(CGameMenuItemChain *pItem);
static void SetupCareerMenu(CGameMenuItemChain *pItem);

CGameMenu menuCareer;

CGameMenuItemTitle itemBotsTitle("BOTS", 1, 160, 20, 2038);
CGameMenuItemSlider itemBotsCount("BOTS:", 3, 66, 38, 180, &gBotCount, 1, kBotMaxBots, 1, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemZCycle itemBotsSkill("DIFFICULTY:", 3, 66, 48, 180, 0, BotsSetSkill, gBotSkillNames, kBotSkillMax, 0);
CGameMenuItemZCycle itemBotsPersona("PERSONALITY:", 3, 66, 58, 180, 0, BotsSetPersona, gBotPersonaNames, kBotPersonaMax, 0);
CGameMenuItemChain itemBotsEach("SET EACH BOT", 3, 0, 72, 320, 1, &menuBotsEach, -1, BotsSetupEach, 0);
CGameMenuItemChain itemBotsTuning("FINE TUNING (ALL BOTS)", 3, 0, 82, 320, 1, &menuBotsTuning, -1, NULL, 0);
CGameMenuItemChain itemBotsTeams("SET TEAMS", 3, 0, 92, 320, 1, &menuTeams, -1, SetupTeamsMenu, 0);
CGameMenuItemChain itemBotsMatch("MATCH SETTINGS", 3, 0, 102, 320, 1, &menuNetStart, 1, NULL, 0);
CGameMenuItemChain itemBotsCareer("CAREER", 3, 0, 116, 320, 1, &menuCareer, -1, SetupCareerMenu, 0);
CGameMenuItemChain itemBotsWatch("WATCH BOTS", 3, 0, 126, 320, 1, NULL, -1, BotsWatch, 0);
CGameMenuItemText itemBotsInfo(zBotsInfo, 3, 160, 140, 1);
CGameMenuItemText itemBotsInfo2(zBotsInfo2, 3, 160, 150, 1);
CGameMenuItemChain itemBotsStart("START MATCH", 1, 0, 160, 320, 1, NULL, -1, BotsStart, 0);

static char zCareer1[80], zCareer2[96], zCareer3[80], zCareer4[80];
static bool bCareerResetArmed;
static void CareerPlay(CGameMenuItemChain *pItem);
static void CareerReset(CGameMenuItemChain *pItem);
static void UpdateCareer(CGameMenuItem *pItem);
CGameMenuItemTitle itemCareerTitle("CAREER", 1, 160, 20, 2038);
CGameMenuItemText itemCareer1(zCareer1, 3, 160, 44, 1);
CGameMenuItemText itemCareer2(zCareer2, 3, 160, 56, 1);
CGameMenuItemText itemCareer3(zCareer3, 3, 160, 76, 1);
CGameMenuItemText itemCareer4(zCareer4, 3, 160, 88, 1);
CGameMenuItemChain itemCareerPlay("PLAY THIS STAGE", 1, 0, 112, 320, 1, NULL, -1, CareerPlay, 0);
CGameMenuItemChain itemCareerReset("START THE CAREER OVER", 3, 0, 140, 320, 1, NULL, -1, CareerReset, 0);
CGameMenuItemText itemCareerHelp("WIN A STAGE TO GO ON TO THE NEXT; 10 STAGES", 3, 160, 160, 1);

CGameMenuItemTitle itemBotsTuningTitle("FINE TUNING", 1, 160, 20, 2038);
CGameMenuItemSlider itemBotsAccuracy("AIM %:", 3, 66, 34, 180, &gBotAccuracy, 10, 300, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsReaction("REACTION TIME %:", 3, 66, 43, 180, &gBotReaction, 10, 300, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsAggression("AGGRESSION %:", 3, 66, 52, 180, &gBotAggression, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsJumping("JUMPING %:", 3, 66, 61, 180, &gBotJumping, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsAmbush("AMBUSHES %:", 3, 66, 70, 180, &gBotAmbush, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsFlank("FLANKING %:", 3, 66, 79, 180, &gBotFlank, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsCamping("CAMPING %:", 3, 66, 88, 180, &gBotCamping, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsHighGround("HIGH GROUND %:", 3, 66, 97, 180, &gBotHighGround, 0, 200, 10, BotsSlider, -1, -1, kMenuSliderValue);
CGameMenuItemZCycle itemBotsHandicap("BOTS TAKE DAMAGE:", 3, 66, 107, 180, 0, BotsSetHandicap, pzBotsHandicap, 5, 0);
CGameMenuItemZBool itemBotsGrudges("GRUDGES:", 3, 66, 116, 180, true, BotsToggle, NULL, NULL);
CGameMenuItemZBool itemBotsAdaptive("ADAPTIVE DIFFICULTY:", 3, 66, 125, 180, false, BotsToggle, NULL, NULL);
CGameMenuItemZBool itemBotsTaunts("TAUNTS:", 3, 66, 134, 180, true, BotsToggle, NULL, NULL);
CGameMenuItemZBool itemBotsCultists("LOOK:", 3, 66, 143, 180, true, BotsToggle, "CULTISTS/CALEB", "CALEB");
CGameMenuItemZBool itemBotsDebug("SHOW WHAT BOTS DO:", 3, 66, 152, 180, false, BotsToggle, NULL, NULL);
CGameMenuItemText itemBotsTuningHelp1("FOR ALL BOTS: 100% IS THE DIFFICULTY'S OR PERSONALITY'S OWN", 3, 160, 166, 1);
CGameMenuItemText itemBotsTuningHelp2("AIM ABOVE 100% IS BETTER, REACTION ABOVE 100% IS SLOWER", 3, 160, 176, 1);

// one bot: picked with BOT:, everything below is that bot's
CGameMenuItemTitle itemBotsEachTitle("EACH BOT", 1, 160, 20, 2038);
CGameMenuItemZCycle itemBotsEachWhich("BOT:", 3, 66, 32, 180, 0, BotsSetEach, pzBotsNumbers, kBotMaxBots, 0);
CGameMenuItemZEdit itemBotsEachName("NAME:", 3, 66, 41, 180, gBotEachName[0], sizeof(gBotEachName[0]), 0, BotsEachName, 0);
CGameMenuItemZCycle itemBotsEachSkill("DIFFICULTY:", 3, 66, 50, 180, 0, BotsSetEach, pzBotsEachSkill, kBotSkillMax+1, 0);
CGameMenuItemZCycle itemBotsEachPersona("PERSONALITY:", 3, 66, 59, 180, 0, BotsSetEach, pzBotsEachPersona, kBotPersonaMax+1, 0);
CGameMenuItemZCycle itemBotsEachTeam("TEAM:", 3, 66, 68, 180, 0, BotsSetEach, pzBotsTeams, 3, 0);
CGameMenuItemZCycle itemBotsEachHandicap("TAKES DAMAGE:", 3, 66, 77, 180, 0, BotsSetEach, pzBotsEachHandicap, 6, 0);
CGameMenuItemSlider itemBotsEachAim("AIM %:", 3, 66, 87, 180, &gBotEachTune[0][kBotTuneAim], 10, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachReaction("REACTION TIME %:", 3, 66, 96, 180, &gBotEachTune[0][kBotTuneReaction], 10, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachAggression("AGGRESSION %:", 3, 66, 105, 180, &gBotEachTune[0][kBotTuneAggression], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachJumping("JUMPING %:", 3, 66, 114, 180, &gBotEachTune[0][kBotTuneJumping], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachAmbush("AMBUSHES %:", 3, 66, 123, 180, &gBotEachTune[0][kBotTuneAmbush], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachFlank("FLANKING %:", 3, 66, 132, 180, &gBotEachTune[0][kBotTuneFlank], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachCamping("CAMPING %:", 3, 66, 141, 180, &gBotEachTune[0][kBotTuneCamping], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemSlider itemBotsEachHighGround("HIGH GROUND %:", 3, 66, 150, 180, &gBotEachTune[0][kBotTuneHighGround], 0, 300, 10, BotsEachSlider, -1, -1, kMenuSliderValue);
CGameMenuItemChain itemBotsEachReset("RESET THIS BOT", 3, 0, 161, 320, 1, NULL, -1, BotsEachReset, 0);
static CGameMenuItemSlider *pBotsEachTune[kBotTuneMax] = { &itemBotsEachAim, &itemBotsEachReaction, &itemBotsEachAggression, &itemBotsEachJumping,
    &itemBotsEachAmbush, &itemBotsEachFlank, &itemBotsEachCamping, &itemBotsEachHighGround };

static void BotsSlider(CGameMenuItemSlider *pItem)
{
    if (pItem->pValue)
        *pItem->pValue = pItem->nValue;
}

static void BotsSetSkill(CGameMenuItemZCycle *pItem)
{
    gBotSkill = ClipRange(pItem->m_nFocus, 0, kBotSkillMax-1);
}

static void BotsSetPersona(CGameMenuItemZCycle *pItem)
{
    gBotPersona = ClipRange(pItem->m_nFocus, 0, kBotPersonaMax-1);
}

static void BotsSetHandicap(CGameMenuItemZCycle *pItem)
{
    gBotHandicap = ClipRange(pItem->m_nFocus, 0, 4);
}

static void BotsToggle(CGameMenuItemZBool *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gBotTaunts = itemBotsTaunts.at20;
    gBotGrudges = itemBotsGrudges.at20;
    gBotAdaptive = itemBotsAdaptive.at20;
    gBotCultists = itemBotsCultists.at20;
    gBotDebug = itemBotsDebug.at20;
}

static int nBotsEachLast = -1;

static void BotsShowEach(int nBot)
{
    itemBotsEachName.at20 = gBotEachName[nBot];
    itemBotsEachName.at30 = 0;
    itemBotsEachSkill.SetTextIndex(gBotEachSkill[nBot] + 1);
    itemBotsEachPersona.SetTextIndex(gBotEachPersona[nBot] + 1);
    itemBotsEachTeam.SetTextIndex(ClipRange(gBotEachTeam[nBot], 0, 2));
    itemBotsEachHandicap.SetTextIndex(ClipRange(gBotEachHandicap[nBot] + 1, 0, 5));
    for (int i = 0; i < kBotTuneMax; i++)
    {
        pBotsEachTune[i]->pValue = &gBotEachTune[nBot][i];
        pBotsEachTune[i]->nValue = gBotEachTune[nBot][i];
    }
    nBotsEachLast = nBot;
}

static void BotsSetEach(CGameMenuItemZCycle *pItem)
{
    const int nBot = ClipRange(itemBotsEachWhich.m_nFocus, 0, kBotMaxBots-1);
    if (pItem == &itemBotsEachWhich || nBotsEachLast != nBot)
    {
        BotsShowEach(nBot); // another bot picked: show its choices
        return;
    }
    gBotEachSkill[nBot] = itemBotsEachSkill.m_nFocus - 1;
    gBotEachPersona[nBot] = itemBotsEachPersona.m_nFocus - 1;
    gBotEachTeam[nBot] = ClipRange(itemBotsEachTeam.m_nFocus, 0, 2);
    gBotEachHandicap[nBot] = ClipRange(itemBotsEachHandicap.m_nFocus, 0, 5) - 1;
}

static void BotsEachSlider(CGameMenuItemSlider *pItem)
{
    if (pItem->pValue)
        *pItem->pValue = pItem->nValue;
}

static void BotsEachName(CGameMenuItemZEdit *pItem, CGameMenuEvent *pEvent)
{
    UNREFERENCED_PARAMETER(pEvent);
    // the name is edited in place; keep it printable
    for (char *p = pItem->at20; *p; p++)
        if ((unsigned char)*p < ' ')
            *p = ' ';
}

static void BotsEachReset(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    const int nBot = ClipRange(itemBotsEachWhich.m_nFocus, 0, kBotMaxBots-1);
    gBotEachName[nBot][0] = '\0';
    gBotEachSkill[nBot] = gBotEachPersona[nBot] = gBotEachHandicap[nBot] = -1;
    gBotEachTeam[nBot] = kBotTeamAuto;
    for (int i = 0; i < kBotTuneMax; i++)
        gBotEachTune[nBot][i] = 100;
    BotsShowEach(nBot);
}

static void BotsSetupEach(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    nBotsEachLast = -1;
    BotsSetEach(&itemBotsEachWhich);
}

// the BOTS menu's own items: the bot count and what is shown depend on where it was opened from
static void BotsMenuMode(bool bNet)
{
    bBotsNet = bNet;
    if (bNet)
    {
        itemBotsCount.pValue = &gBotNetCount;
        itemBotsCount.nRangeLow = 0;
        itemBotsCount.nRangeHigh = botNetMaxBots();
        gBotNetCount = ClipRange(gBotNetCount, 0, itemBotsCount.nRangeHigh);
        itemBotsCount.tooltip_pzTextUpper = "How many bots join the game (0: none)";
    }
    else
    {
        itemBotsCount.pValue = &gBotCount;
        itemBotsCount.nRangeLow = 1;
        itemBotsCount.nRangeHigh = kBotMaxBots;
        itemBotsCount.tooltip_pzTextUpper = "How many bots to play against";
    }
    itemBotsCount.nValue = *itemBotsCount.pValue;
    CGameMenuItem *pOffline[] = { &itemBotsMatch, &itemBotsCareer, &itemBotsWatch, &itemBotsStart };
    for (CGameMenuItem *pItem : pOffline)
        pItem->bEnable = !bNet, pItem->bNoDraw = bNet;
    itemBotsTitle.m_pzText = bNet ? "BOTS IN THIS GAME" : "BOTS";
    itemBotsSkill.SetTextIndex(ClipRange(gBotSkill, 0, kBotSkillMax-1));
    itemBotsPersona.SetTextIndex(ClipRange(gBotPersona, 0, kBotPersonaMax-1));
    itemBotsHandicap.SetTextIndex(ClipRange(gBotHandicap, 0, 4));
    itemBotsTaunts.at20 = !!gBotTaunts;
    itemBotsGrudges.at20 = !!gBotGrudges;
    itemBotsAdaptive.at20 = !!gBotAdaptive;
    itemBotsCultists.at20 = !!gBotCultists;
    itemBotsDebug.at20 = !!gBotDebug;
}

void SetupBotsMenu(CGameMenuItemChain *pItem)
{
    SetupBotMatchMenu(pItem);
    BotsMenuMode(false);
}

void SetupBotsNetMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    BotsMenuMode(true);
}

void BotsNetSummary(char *pzBuffer, int nSize)
{
    const int nBots = ClipRange(gBotNetCount, 0, botNetMaxBots());
    if (!nBots)
    {
        Bsnprintf(pzBuffer, nSize, "NONE");
        return;
    }
    bool bSame = true;
    for (int i = 0; i < nBots; i++)
        bSame &= gBotEachSkill[i] < 0 && gBotEachPersona[i] < 0;
    if (bSame)
        Bsnprintf(pzBuffer, nSize, "%d - %s %s", nBots, gBotSkillNames[ClipRange(gBotSkill, 0, kBotSkillMax-1)], gBotPersonaNames[ClipRange(gBotPersona, 0, kBotPersonaMax-1)]);
    else
        Bsnprintf(pzBuffer, nSize, "%d - SET EACH", nBots);
}

// ---- SET TEAMS ----------------------------------------------------------------------------
// everybody of a teams game: the people (in a network game the ones who joined) and the bots

#define kTeamsRows 8
static char zTeamsLabel[kTeamsRows][48];
static char zTeamsAuto[kTeamsRows][16];
static const char *pzTeamsRow[kTeamsRows][3];
static int nTeamsRowPlayer[kTeamsRows]; // a person's player number, -1 for a bot
static int nTeamsRowBot[kTeamsRows];    // a bot of SET EACH BOT, -1 for a person
static int nTeamsRows;
static char zTeamsSummary1[64], zTeamsSummary2[64];

static void TeamsSet(CGameMenuItemZCycle *pItem);
static void TeamsAllAuto(CGameMenuItemChain *pItem);
static void UpdateTeams(CGameMenuItem *pItem);

CGameMenuItemTitle itemTeamsTitle("TEAMS", 1, 160, 20, 2038);
CGameMenuItemZCycle itemTeamsRow0(zTeamsLabel[0], 3, 30, 36, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow1(zTeamsLabel[1], 3, 30, 46, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow2(zTeamsLabel[2], 3, 30, 56, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow3(zTeamsLabel[3], 3, 30, 66, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow4(zTeamsLabel[4], 3, 30, 76, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow5(zTeamsLabel[5], 3, 30, 86, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow6(zTeamsLabel[6], 3, 30, 96, 260, 0, TeamsSet, NULL, 0, 0);
CGameMenuItemZCycle itemTeamsRow7(zTeamsLabel[7], 3, 30, 106, 260, 0, TeamsSet, NULL, 0, 0);
static CGameMenuItemZCycle *pTeamsRows[kTeamsRows] = { &itemTeamsRow0, &itemTeamsRow1, &itemTeamsRow2, &itemTeamsRow3, &itemTeamsRow4, &itemTeamsRow5, &itemTeamsRow6, &itemTeamsRow7 };
CGameMenuItemText itemTeamsSummary1(zTeamsSummary1, 3, 160, 122, 1);
CGameMenuItemText itemTeamsSummary2(zTeamsSummary2, 3, 160, 131, 1);
CGameMenuItemChain itemTeamsAllAuto("ALL ON AUTO", 3, 0, 145, 320, 1, NULL, -1, TeamsAllAuto, 0);
CGameMenuItemText itemTeamsHelp1("AUTO: PLAYERS AS THEY CHOSE, BOTS FILL THE SMALLER TEAM", 3, 160, 160, 1);
CGameMenuItemText itemTeamsHelp2("EVEN IT OUT WITH EACH BOT'S DIFFICULTY (SET EACH BOT)", 3, 160, 169, 1);

static int TeamsBotCount(void)
{
    if (gNetMode == NETWORK_NONE)
        return ClipRange(gBotCount, 1, kBotMaxBots);
    return ClipRange(gBotNetCount, 0, botNetMaxBots());
}

static int TeamsPick(int nRow)
{
    if (nTeamsRowBot[nRow] >= 0)
        return ClipRange(gBotEachTeam[nTeamsRowBot[nRow]], 0, 2);
    return ClipRange(gHumanTeam[nTeamsRowPlayer[nRow]], 0, 2);
}

static void UpdateTeams(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    // where everybody on AUTO ends up: the same rules as the start of the game (botResolveTeams)
    int nTeam[kTeamsRows], nPeople[2] = { 0, 0 }, nBots[2] = { 0, 0 };
    for (int i = 0; i < nTeamsRows; i++)
    {
        if (nTeamsRowBot[i] >= 0)
            continue;
        const int p = nTeamsRowPlayer[i];
        const int nPick = TeamsPick(i);
        nTeam[i] = nPick ? nPick - 1 : ((NetMenuAutoTeams() || !gProfile[p].nTeamPreference) ? p : gProfile[p].nTeamPreference - 1) & 1;
        nPeople[nTeam[i]]++;
    }
    for (int i = 0; i < nTeamsRows; i++)
    {
        if (nTeamsRowBot[i] < 0)
            continue;
        const int nPick = TeamsPick(i);
        nTeam[i] = nPick ? nPick - 1 : ((nPeople[0] + nBots[0] <= nPeople[1] + nBots[1]) ? 0 : 1);
        nBots[nTeam[i]]++;
    }
    for (int i = 0; i < nTeamsRows; i++)
        Bsnprintf(zTeamsAuto[i], sizeof(zTeamsAuto[i]), "AUTO: %s", nTeam[i] ? "RED" : "BLUE");
    Bsnprintf(zTeamsSummary1, sizeof(zTeamsSummary1), "BLUE TEAM: %d PLAYER%s, %d BOT%s", nPeople[0], nPeople[0] == 1 ? "" : "S", nBots[0], nBots[0] == 1 ? "" : "S");
    Bsnprintf(zTeamsSummary2, sizeof(zTeamsSummary2), "RED TEAM: %d PLAYER%s, %d BOT%s", nPeople[1], nPeople[1] == 1 ? "" : "S", nBots[1], nBots[1] == 1 ? "" : "S");
}

void SetupTeamsMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    int nPeople[kMaxPlayers];
    const int nPeopleCount = botPeople(nPeople);
    nTeamsRows = 0;
    for (int i = 0; i < nPeopleCount && nTeamsRows < kTeamsRows; i++, nTeamsRows++)
    {
        const int p = nPeople[i];
        nTeamsRowPlayer[nTeamsRows] = p;
        nTeamsRowBot[nTeamsRows] = -1;
        Bsnprintf(zTeamsLabel[nTeamsRows], sizeof(zTeamsLabel[nTeamsRows]), "%s%s", gProfile[p].name, p == myconnectindex ? " (YOU)" : "");
        Bstrupr(zTeamsLabel[nTeamsRows]);
    }
    const int nBots = TeamsBotCount();
    for (int i = 0; i < nBots && nTeamsRows < kTeamsRows; i++, nTeamsRows++)
    {
        nTeamsRowPlayer[nTeamsRows] = -1;
        nTeamsRowBot[nTeamsRows] = i;
        botDescribeEach(i, zTeamsLabel[nTeamsRows], sizeof(zTeamsLabel[nTeamsRows]));
    }
    for (int i = 0; i < kTeamsRows; i++)
    {
        pzTeamsRow[i][0] = zTeamsAuto[i];
        pzTeamsRow[i][1] = "BLUE";
        pzTeamsRow[i][2] = "RED";
        CGameMenuItemZCycle *pRow = pTeamsRows[i];
        pRow->SetTextArray(pzTeamsRow[i], 3, i < nTeamsRows ? TeamsPick(i) : 0);
        pRow->bEnable = i < nTeamsRows;
        pRow->bNoDraw = !pRow->bEnable;
    }
    UpdateTeams(NULL);
}

static void TeamsSet(CGameMenuItemZCycle *pItem)
{
    for (int i = 0; i < nTeamsRows; i++)
    {
        if (pTeamsRows[i] != pItem)
            continue;
        const int nPick = ClipRange(pItem->m_nFocus, 0, 2);
        if (nTeamsRowBot[i] >= 0)
            gBotEachTeam[nTeamsRowBot[i]] = nPick;
        else
            gHumanTeam[nTeamsRowPlayer[i]] = nPick;
    }
    UpdateTeams(NULL);
}

static void TeamsAllAuto(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    for (int i = 0; i < nTeamsRows; i++)
    {
        if (nTeamsRowBot[i] >= 0)
            gBotEachTeam[nTeamsRowBot[i]] = kBotTeamAuto;
        else
            gHumanTeam[nTeamsRowPlayer[i]] = kBotTeamAuto;
        pTeamsRows[i]->SetTextIndex(0);
    }
    UpdateTeams(NULL);
}

static void BotsStart(CGameMenuItemChain *pItem)
{
    gBotWatchMode = false;
    gBotCareer = 0;
    StartBotMatch(pItem);
}

static void BotsWatch(CGameMenuItemChain *pItem)
{
    gBotCareer = 0;
    gBotWatchMode = true;
    StartBotMatch(pItem);
}

static void UpdateCareer(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    careerDescribe(gCareerStage, zCareer1, zCareer2, sizeof(zCareer1));
    Bsnprintf(zCareer3, sizeof(zCareer3), "WON %d OF %d  KILLS %d  DEATHS %d  BEST STREAK %d", gCareerWins, gCareerMatches, gCareerKills, gCareerDeaths, gCareerBestStreak);
    careerNemesis(zCareer4, sizeof(zCareer4));
    itemCareerReset.m_pzText = bCareerResetArmed ? "PRESS AGAIN TO START OVER" : "START THE CAREER OVER";
}

static void SetupCareerMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    bCareerResetArmed = false;
}

static void CareerPlay(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    careerPlay();
}

static void CareerReset(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    if (!bCareerResetArmed)
    {
        bCareerResetArmed = true;
        return;
    }
    bCareerResetArmed = false;
    careerReset();
}

static void UpdateBotsInfo(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    itemBotsTeams.bEnable = NetMenuGameType() == kGameTypeTeams;
    if (bBotsNet)
    {
        const int nPeople = botPeople(NULL);
        Bsnprintf(zBotsInfo, sizeof(zBotsInfo), "%d PLAYER%s + %d BOT%s (8 AT MOST)", nPeople, nPeople == 1 ? "" : "S", gBotNetCount, gBotNetCount == 1 ? "" : "S");
        Bsnprintf(zBotsInfo2, sizeof(zBotsInfo2), "YOUR PC PLAYS THE BOTS FOR EVERYBODY");
        return;
    }
    BotMatchInfo(zBotsInfo, sizeof(zBotsInfo));
    zBotsInfo2[0] = '\0';
}

static void UpdateEpisodeBots(CGameMenuItem *pItem)
{
    pItem->bEnable = gNetMode == NETWORK_NONE; // offline only, for now
}

void SetupChosenMenus(void)
{
    menuMatchStats.Add(&itemMatchStatsTitle, false);
    menuMatchStats.Add(&itemMatchStatsTable, false);
    menuMatchStats.Add(&itemMatchStatsBack, true);
    menuMatchStats.Add(&itemBloodQAV, false);
    itemMatchStatsTable.pPreDrawCallback = DrawMatchStats;

    menuBotOrders.Add(&itemBotOrdersTitle, false);
    menuBotOrders.Add(&itemBotOrderFollow, true);
    menuBotOrders.Add(&itemBotOrderHold, false);
    menuBotOrders.Add(&itemBotOrderFree, false);
    menuBotOrders.Add(&itemBotOrdersHelp1, false);
    menuBotOrders.Add(&itemBotOrdersHelp2, false);
    menuBotOrders.Add(&itemBloodQAV, false);
    itemBotOrderFollow.tooltip_pzTextUpper = "They stay with you and fight what comes";
    itemBotOrderHold.tooltip_pzTextUpper = "They guard where you're standing now";
    itemBotOrderFree.tooltip_pzTextUpper = "They go on their own: hunt, take objectives, collect";
    menuBots.Add(&itemBotsTitle, false);
    menuBots.Add(&itemBotsCount, true);
    menuBots.Add(&itemBotsSkill, false);
    menuBots.Add(&itemBotsPersona, false);
    menuBots.Add(&itemBotsEach, false);
    menuBots.Add(&itemBotsTuning, false);
    menuBots.Add(&itemBotsTeams, false);
    menuBots.Add(&itemBotsMatch, false);
    menuBots.Add(&itemBotsCareer, false);
    menuBots.Add(&itemBotsWatch, false);
    menuBots.Add(&itemBotsInfo, false);
    menuBots.Add(&itemBotsInfo2, false);
    menuBots.Add(&itemBotsStart, false);
    menuBots.Add(&itemBloodQAV, false);
    itemBotsInfo.pPreDrawCallback = UpdateBotsInfo;
    itemNetworkBots.pPreDrawCallback = UpdateEpisodeBots;
    itemNetworkBots.tooltip_pzTextUpper = "Bloodbath against computer players";
    itemBotsCount.tooltip_pzTextUpper = "How many bots to play against";
    itemBotsSkill.tooltip_pzTextUpper = "How well they aim, how fast they react and how smart they are";
    itemBotsPersona.tooltip_pzTextUpper = "Rusher, camper, trickster, coward, jumper or a mix";
    itemBotsEach.tooltip_pzTextUpper = "Name, difficulty, personality, team and tuning of single bots";
    itemBotsTuning.tooltip_pzTextUpper = "Adjust aim, reaction time and tactics of all bots";
    itemBotsTeams.tooltip_pzTextUpper = "Who plays on which team (teams games)";
    itemBotsMatch.tooltip_pzTextUpper = "Game type, map, frag and time limits, weapons, items, mutators";
    itemBotsStart.tooltip_pzTextUpper = "Play! (ESC pauses the match)";
    itemBotsCareer.tooltip_pzTextUpper = "A ladder of 10 matches against named bots";
    itemBotsWatch.tooltip_pzTextUpper = "A match of bots only; the camera follows the action";

    menuCareer.Add(&itemCareerTitle, false);
    menuCareer.Add(&itemCareer1, false);
    menuCareer.Add(&itemCareer2, false);
    menuCareer.Add(&itemCareer3, false);
    menuCareer.Add(&itemCareer4, false);
    menuCareer.Add(&itemCareerPlay, true);
    menuCareer.Add(&itemCareerReset, false);
    menuCareer.Add(&itemCareerHelp, false);
    menuCareer.Add(&itemBloodQAV, false);
    itemCareer1.pPreDrawCallback = UpdateCareer;

    menuBotsTuning.Add(&itemBotsTuningTitle, false);
    menuBotsTuning.Add(&itemBotsAccuracy, true);
    menuBotsTuning.Add(&itemBotsReaction, false);
    menuBotsTuning.Add(&itemBotsAggression, false);
    menuBotsTuning.Add(&itemBotsJumping, false);
    menuBotsTuning.Add(&itemBotsAmbush, false);
    menuBotsTuning.Add(&itemBotsFlank, false);
    menuBotsTuning.Add(&itemBotsCamping, false);
    menuBotsTuning.Add(&itemBotsHighGround, false);
    menuBotsTuning.Add(&itemBotsHandicap, false);
    menuBotsTuning.Add(&itemBotsGrudges, false);
    menuBotsTuning.Add(&itemBotsAdaptive, false);
    menuBotsTuning.Add(&itemBotsTaunts, false);
    menuBotsTuning.Add(&itemBotsCultists, false);
    menuBotsTuning.Add(&itemBotsDebug, false);
    menuBotsTuning.Add(&itemBotsTuningHelp1, false);
    menuBotsTuning.Add(&itemBotsTuningHelp2, false);
    menuBotsTuning.Add(&itemBloodQAV, false);
    itemBotsAmbush.tooltip_pzTextUpper = "Waiting beside a doorway you're about to come through";
    itemBotsFlank.tooltip_pzTextUpper = "Taking another way round to come at you from the side";
    itemBotsCamping.tooltip_pzTextUpper = "Staying in a good spot and waiting";
    itemBotsGrudges.tooltip_pzTextUpper = "A bot you kill a lot hunts you down and taunts you";
    itemBotsAdaptive.tooltip_pzTextUpper = "Bots slowly get better or worse to keep it close";
    itemBotsHighGround.tooltip_pzTextUpper = "Going for high places that see a lot, and holding them";
    itemBotsHandicap.tooltip_pzTextUpper = "More damage makes them easier to kill";
    itemBotsDebug.tooltip_pzTextUpper = "Lists every bot's plan at the top of the screen";

    menuBotsEach.Add(&itemBotsEachTitle, false);
    menuBotsEach.Add(&itemBotsEachWhich, true);
    menuBotsEach.Add(&itemBotsEachName, false);
    menuBotsEach.Add(&itemBotsEachSkill, false);
    menuBotsEach.Add(&itemBotsEachPersona, false);
    menuBotsEach.Add(&itemBotsEachTeam, false);
    menuBotsEach.Add(&itemBotsEachHandicap, false);
    for (int i = 0; i < kBotTuneMax; i++)
        menuBotsEach.Add(pBotsEachTune[i], false);
    menuBotsEach.Add(&itemBotsEachReset, false);
    menuBotsEach.Add(&itemBloodQAV, false);
    for (int i = 0; i < kBotTuneMax; i++)
        pBotsEachTune[i]->tooltip_pzTextUpper = "Percent of FINE TUNING for all bots (100: the same)";
    itemBotsEachName.tooltip_pzTextUpper = "Empty: a random name";
    itemBotsEachSkill.tooltip_pzTextUpper = "Same as all: the BOTS menu's difficulty";
    itemBotsEachPersona.tooltip_pzTextUpper = "Same as all: the BOTS menu's personality";
    itemBotsEachReset.tooltip_pzTextUpper = "Back to random name, same as all, 100%";
    itemBotsEachWhich.tooltip_pzTextUpper = "Pick a bot; the rest is that bot's";
    itemBotsEachTeam.tooltip_pzTextUpper = "Its team in teams games (auto: the smaller team)";
    itemBotsEachHandicap.tooltip_pzTextUpper = "More damage makes it easier to kill";

    menuTeams.Add(&itemTeamsTitle, false);
    for (int i = 0; i < kTeamsRows; i++)
        menuTeams.Add(pTeamsRows[i], i == 0);
    menuTeams.Add(&itemTeamsSummary1, false);
    menuTeams.Add(&itemTeamsSummary2, false);
    menuTeams.Add(&itemTeamsAllAuto, false);
    menuTeams.Add(&itemTeamsHelp1, false);
    menuTeams.Add(&itemTeamsHelp2, false);
    menuTeams.Add(&itemBloodQAV, false);
    itemTeamsSummary1.pPreDrawCallback = UpdateTeams;

    menuLanGames.Add(&itemLanTitle, false);
    for (int i = 0; i < kMaxLanGames; i++)
        menuLanGames.Add(pLanItems[i], i == 0);
    menuLanGames.Add(&itemLanRefresh, false);
    menuLanGames.Add(&itemLanStatus, false);
    menuLanGames.Add(&itemBloodQAV, false);

    Bstrncpyz(zVersionText, onlineVersion(), sizeof(zVersionText));
    Bstrupr(zVersionText);

    menuMods.Add(&itemModsTitle, false);
    for (int i = 0; i < kMaxModEntries; i++)
        menuMods.Add(pModItems[i], i == 0);
    menuMods.Add(&itemModsHelp1, false);
    menuMods.Add(&itemModsHelp2, false);
    menuMods.Add(&itemBloodQAV, false);

    menuCoopLoad.Add(&itemCoopLoadTitle, false);
    menuCoopLoad.Add(&itemCoopLoadMid, true);
    menuCoopLoad.Add(&itemCoopLoadMidHelp, false);
    menuCoopLoad.Add(&itemCoopLoadCarry, false);
    menuCoopLoad.Add(&itemCoopLoadCarryHelp, false);
    menuCoopLoad.Add(&itemCoopLoadOrder, false);
    menuCoopLoad.Add(&itemCoopLoadLeave, false);
    menuCoopLoad.Add(&itemCoopLoadHelp1, false);
    menuCoopLoad.Add(&itemCoopLoadHelp2, false);
    menuCoopLoad.Add(&itemBloodQAV, false);
    itemCoopLoadOrder.tooltip_pzTextUpper = "What players who aren't in the save start with";
    itemCoopLoadLeave.tooltip_pzTextUpper = "Saves the game when somebody leaves, to continue later";

    menuCoopSlots.Add(&itemCoopSlotsTitle, false);
    for (int i = 0; i <= kCoopSlots; i++)
        menuCoopSlots.Add(pCoopSlotItems[i], i == 0);
    menuCoopSlots.Add(&itemBloodQAV, false);

    menuCoopSave.Add(&itemCoopSaveTitle, false);
    for (int i = 0; i < kCoopSlots; i++)
        menuCoopSave.Add(pCoopSaveItems[i], i == 0);
    menuCoopSave.Add(&itemCoopSaveHelp, false);
    menuCoopSave.Add(&itemBloodQAV, false);

    menuOnline.Add(&itemOnlineTitle, false);
    menuOnline.Add(&itemOnlineServer, false);
    menuOnline.Add(&itemOnlineKey, false);
    menuOnline.Add(&itemOnlineLobby, false);
    menuOnline.Add(&itemOnlinePlayers, false);
    menuOnline.Add(&itemOnlineHost, true);
    menuOnline.Add(&itemOnlineFind, false);
    menuOnline.Add(&itemOnlineJoin, false);
#ifndef NOTBLOOD_WEB // no LAN games in a browser
    menuOnline.Add(&itemOnlineLanStats, false);
#endif
    menuOnline.Add(&itemOnlineStatus, false);
    menuOnline.Add(&itemOnlineDashboard, false);
    menuOnline.Add(&itemBloodQAV, false);
    itemOnlineStatus.pPreDrawCallback = UpdateOnlineStatus;
    itemOnlineServer.tooltip_pzTextUpper = "Address of the online server";
    itemOnlineKey.tooltip_pzTextUpper = "The secret key of the server";
    itemOnlineLobby.tooltip_pzTextUpper = "Name of the lobby to host or join";

    menuOnlineLobbies.Add(&itemOnlineLobbiesTitle, false);
    for (int i = 0; i < kMaxListedLobbies; i++)
        menuOnlineLobbies.Add(pLobbyItems[i], i == 0);
    menuOnlineLobbies.Add(&itemOnlineRefresh, false);
    menuOnlineLobbies.Add(&itemOnlineLobbiesStatus, false);
    menuOnlineLobbies.Add(&itemBloodQAV, false);
}
