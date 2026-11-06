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

class CGameMenu;
class CGameMenuItemChain;

extern CGameMenu menuOnline;
extern CGameMenu menuCoopLoad;
extern CGameMenu menuCoopSave;
extern CGameMenu menuMods;
extern CGameMenu menuLanGames;
extern CGameMenu menuBots;
extern CGameMenuItemChain itemNetworkBots;
extern CGameMenu menuMatchStats;
extern CGameMenu menuBotOrders;

// menu.cpp: bot matches use the multiplayer game setup
void SetupBotMatchMenu(CGameMenuItemChain *pItem);
void SetupBotsMenu(CGameMenuItemChain *pItem);
void StartBotMatch(CGameMenuItemChain *pItem);
void BotMatchInfo(char *pzBuffer, int nSize);
void BotMatchSetLimits(int nFrags, int nMinutes);
void BotMatchSetMap(int nEpisode, int nLevel);
void BotMatchSetMode(int nGameType, int nMode);
void BotMatchSetLives(int nLives);
void BotMatchResetMutators(void);
void BotMatchSetRules(int nSize, int nBarrels, int bAkimbo, int nRounds, int bVote);
int NetMenuGameType(void);      // the GAME SETTINGS choice: kGameTypeCoop, ...
bool NetMenuAutoTeams(void);

// bots in network games and teams: MULTIPLAYER > BOTS, SET TEAMS
extern CGameMenu menuTeams;
void SetupBotsNetMenu(CGameMenuItemChain *pItem);
void SetupTeamsMenu(CGameMenuItemChain *pItem);
void BotsNetSummary(char *pzBuffer, int nSize);

void SetupChosenMenus(void);
void SetupOnlineMenu(CGameMenuItemChain *pItem);
void SetupCoopLoadMenu(CGameMenuItemChain *pItem);
void SetupCoopSaveMenu(CGameMenuItemChain *pItem);
void OpenCoopSlotsMenu(int nType);
void SetupModsMenu(CGameMenuItemChain *pItem);
void SetupNetworkHostAddress(void);

class CGameMenuItemText;
extern CGameMenuItemText itemMainVersion;
extern CGameMenuItemText itemNetworkHostAddress;
extern CGameMenuItemText itemNetworkHostHelp;
void SetupLanGamesMenu(CGameMenuItemChain *pItem);

// the vote for the next map, and players waiting for the host (chosenmodes.cpp)
void menuOpenMapVote(void);
void menuCloseMapVote(void);
void menuLeaveNetStart(void);
