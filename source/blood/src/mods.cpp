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

#include "compat.h"

#ifdef _WIN32
# include "windows_inc.h"
#elif defined __APPLE__
# include <TargetConditionals.h>
# include <mach-o/dyld.h>
# include <spawn.h>
#elif !defined __ANDROID__
# include <spawn.h>
# include <unistd.h>
#endif

#include "build.h"
#include "baselayer.h"
#include "common.h"
#include "common_game.h"
#include "blood.h"
#include "getopt.h"
#include "globals.h"
#include "timer.h"
#include "levels.h"
#include "mods.h"
#include "view.h"
#include "gorykill.h"

#if !defined _WIN32 && !defined __ANDROID__ && !(defined __APPLE__ && TARGET_OS_IPHONE)
extern char **environ;
# define MODS_CAN_RESPAWN
#endif

void QuitGame(void);

namespace
{

const char kSelectFile[] = "mods.cfg";
// mods.cfg is renamed to this while the game starts: if it is still there at the next start, the
// mod made the game fail and is switched off
const char kTryingFile[] = "mods.cfg.starting";
bool bFailedLastTime;
char szFailedName[40];

MODENTRY gMods[kMaxModEntries];
int gModCount;
MODENTRY gActive;
bool bActive;

bool isDir(const char *pzPath)
{
    struct Bstat st;
    return !Bstat(pzPath, &st) && (st.st_mode & S_IFMT) == S_IFDIR;
}

bool hasExtension(const char *pzName, const char *pzExt)
{
    size_t const n = strlen(pzName), e = strlen(pzExt);
    return n > e && !Bstrcasecmp(pzName + n - e, pzExt);
}

// does this .ini describe episodes (like BLOOD.INI, CRYPTIC.INI)?
bool isEpisodeIni(const char *pzPath)
{
    FILE *fp = fopen(pzPath, "rb");
    if (!fp)
        return false;
    char line[256];
    bool bFound = false;
    while (!bFound && fgets(line, sizeof(line), fp))
        bFound = !Bstrncasecmp(line, "[Episode1]", 10);
    fclose(fp);
    return bFound;
}

const char *iniDescription(const char *pzIni)
{
    return INIDescriptionName(pzIni);
}

void addEntry(int nKind, const char *pzName, const char *pzPath, const char *pzIni)
{
    if (gModCount >= kMaxModEntries)
        return;
    for (int i = 0; i < gModCount; i++)
        if (!Bstrcasecmp(gMods[i].szName, pzName))
            return;  // the same mod in two places: the first one wins, like the search path
    MODENTRY &m = gMods[gModCount++];
    m = {};
    m.nKind = nKind;
    Bstrncpyz(m.szName, pzName, sizeof(m.szName));
    Bstrncpyz(m.szPath, pzPath, sizeof(m.szPath));
    Bstrncpyz(m.szIni, pzIni ? pzIni : "", sizeof(m.szIni));
}

// Blood Extra Crispy's folder (its EXTRAC.RFF): the path is kept from the game folder, as the game's
// file loading wants it ("mods/BEC_1.2")
bool addExtraCrispy(const char *pzPath, const char *pzRel)
{
    bool bFound = false;
    if (BDIR *dir = Bopendir(pzPath))
    {
        while (Bdirent *d = Breaddir(dir))
            if (!Bstrcasecmp(d->name, "EXTRAC.RFF"))
                bFound = true;
        Bclosedir(dir);
    }
    if (bFound)
        addEntry(kModExtraCrispy, "Blood Extra Crispy", pzRel, NULL);
    return bFound;
}

// a folder: its first episode .ini, if any
void addFolder(const char *pzPath, const char *pzFolder)
{
    char ini[BMAX_PATH] = "";
    if (BDIR *dir = Bopendir(pzPath))
    {
        while (Bdirent *d = Breaddir(dir))
        {
            char file[BMAX_PATH];
            Bsnprintf(file, sizeof(file), "%s/%s", pzPath, d->name);
            if (hasExtension(d->name, ".ini") && isEpisodeIni(file))
            {
                Bstrncpyz(ini, d->name, sizeof(ini));
                break;
            }
        }
        Bclosedir(dir);
    }
    const char *pzDesc = ini[0] ? iniDescription(ini) : NULL;
    char name[64];
    Bstrncpyz(name, pzDesc ? pzDesc : pzFolder, sizeof(name));
    addEntry(kModFolder, name, pzPath, ini);
}

void scanDir(const char *pzRoot, const char *pzSub, bool bGroups)
{
    char base[BMAX_PATH];
    Bsnprintf(base, sizeof(base), "%s/%s", pzRoot, pzSub);
    BDIR *dir = Bopendir(base);
    if (!dir)
        return;
    while (Bdirent *d = Breaddir(dir))
    {
        if (d->name[0] == '.')
            continue;
        char path[BMAX_PATH];
        Bsnprintf(path, sizeof(path), "%s/%s", base, d->name);
        char rel[BMAX_PATH];
        Bsnprintf(rel, sizeof(rel), "%s/%s", pzSub, d->name);
        if (isDir(path) && !addExtraCrispy(path, rel))
            addFolder(path, d->name);
        else if (bGroups && (hasExtension(d->name, ".zip") || hasExtension(d->name, ".grp") || hasExtension(d->name, ".pk3")))
        {
            char name[64];
            Bstrncpyz(name, d->name, sizeof(name));
            if (char *dot = strrchr(name, '.'))
                *dot = '\0';
            addEntry(kModGroup, name, path, NULL);
        }
    }
    Bclosedir(dir);
}

void absolutePath(const char *pzIn, char *pzOut, int nSize)
{
#ifdef _WIN32
    if (!_fullpath(pzOut, pzIn, nSize))
        Bstrncpyz(pzOut, pzIn, nSize);
#else
    char *p = realpath(pzIn, NULL);
    Bstrncpyz(pzOut, p ? p : pzIn, nSize);
    free(p);
#endif
}

} // namespace

const char *modCurrentName(void)
{
    if (bActive)
        return gActive.szName;
    if (pINISelected && Bstrcasecmp(pINISelected->zName, "BLOOD.INI"))
        return pINISelected->zName;
    return "";
}

#ifdef EDUKE32_IOS
// iPad/Android: there is no download folder that brings these along, so make them (with a note
// saying what goes in) where the player copies the game files
static void makeGameFolders(void)
{
    static const struct { const char *pzFolder, *pzNote, *pzText; } folders[] = {
        { "movie", "movie/Put the movie files here.txt",
          "Copy the files from the MOVIE folder of the Blood CD here for the cutscenes:\n"
          "CS1.SMK to CS6.SMK with their .WAV files, GTI.SMK and LOGO.SMK.\n" },
        { "addons", "addons/Put add-on episodes here.txt",
          "Add-on episodes go here, one folder each.\n"
          "Cryptic Passage: a folder \"cryptic\" with CRYPTIC.INI, the CP*.MAP maps and CPART07.AR_ / CPART15.AR_.\n"
          "Choose them in the main menu: MODS.\n" },
        { "mods", "mods/Put mods here.txt",
          "Mods go here: a folder each (maps, .def texture packs, voxels, an episode .ini, ...) or .zip, .grp and .pk3 files.\n"
          "Choose them in the main menu: MODS.\n" },
    };
    for (auto const &f : folders)
    {
        if (isDir(f.pzFolder))
            continue;
        Bmkdir(f.pzFolder, S_IRWXU);
        if (FILE *fp = fopen(f.pzNote, "w"))
        {
            fputs(f.pzText, fp);
            fclose(fp);
        }
    }
}
#endif

void modApplySelection(bool bCommandLine)
{
#ifdef EDUKE32_IOS
    makeGameFolders();
#endif
    FILE *fp = fopen(kTryingFile, "r");
    if (fp)
    {
        char line[BMAX_PATH + 16];
        while (fgets(line, sizeof(line), fp))
            if (!strncmp(line, "name=", 5))
            {
                line[strcspn(line, "\r\n")] = '\0';
                Bstrncpyz(szFailedName, line + 5, sizeof(szFailedName));
            }
        fclose(fp);
        remove(kTryingFile);
        bFailedLastTime = true;
        LOG_F(ERROR, "Mods: \"%s\" stopped the game from starting last time, playing the original game", szFailedName);
        return;
    }
    fp = fopen(kSelectFile, "r");
    if (!fp)
        return;
    MODENTRY m = {};
    char line[BMAX_PATH + 16];
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';
        char *pValue = strchr(line, '=');
        if (!pValue)
            continue;
        *pValue++ = '\0';
        if (!strcmp(line, "name")) Bstrncpyz(m.szName, pValue, sizeof(m.szName));
        else if (!strcmp(line, "path")) Bstrncpyz(m.szPath, pValue, sizeof(m.szPath));
        else if (!strcmp(line, "ini")) Bstrncpyz(m.szIni, pValue, sizeof(m.szIni));
        else if (!strcmp(line, "kind")) m.nKind = atoi(pValue);
    }
    fclose(fp);
    if (m.nKind == kModNone || !m.szName[0])
        return;
    if (bCommandLine)
    {
        LOG_F(INFO, "Mods: \"%s\" is selected, but the command line chooses the game files", m.szName);
        return;
    }
    switch (m.nKind)
    {
    case kModFolder:
        if (!isDir(m.szPath))
        {
            LOG_F(ERROR, "Mods: the folder of \"%s\" is gone (%s)", m.szName, m.szPath);
            return;
        }
        G_AddPath(m.szPath);
        break;
    case kModGroup:
        G_AddGroup(m.szPath);
        break;
    case kModIni:
        break;
    case kModExtraCrispy:
        if (!isDir(m.szPath))
        {
            LOG_F(ERROR, "Mods: the folder of \"%s\" is gone (%s)", m.szName, m.szPath);
            return;
        }
        goryKillSetModDir(m.szPath);
        break;
    default:
        return;
    }
    if (m.szIni[0])
        levelOverrideINI(m.szIni);
    gActive = m;
    bActive = true;
    rename(kSelectFile, kTryingFile);
    LOG_F(INFO, "Mods: playing \"%s\"", m.szName);
}

void modStartupFinished(void)
{
    if (bActive)
        rename(kTryingFile, kSelectFile);
    if (bFailedLastTime)
    {
        char buffer[96];
        Bsnprintf(buffer, sizeof(buffer), "The mod \"%s\" didn't load and was switched off", szFailedName);
        viewSetMessage(buffer);
    }
}

int modScan(void)
{
    gModCount = 0;
    addEntry(kModNone, "Original game", "", NULL);

    // episode .ini files next to the game files (CRYPTIC.INI, ...)
    for (auto pIter = pINIChain; pIter; pIter = pIter->pNext)
    {
        if (!Bstrcasecmp(pIter->zName, "BLOOD.INI"))
            continue;
        const char *pzDesc = iniDescription(pIter->zName);
        if (bActive && !Bstrcasecmp(pIter->zName, gActive.szIni) && gActive.nKind != kModIni)
            continue;  // that is the active mod folder's .ini
        addEntry(kModIni, pzDesc ? pzDesc : pIter->zName, "", pIter->zName);
    }

    char roots[2][BMAX_PATH];
    int nRoots = 0;
    if (Bgetcwd(roots[nRoots], BMAX_PATH))
        nRoots++;
    if (char *pzApp = Bgetappdir())
    {
        absolutePath(pzApp, roots[nRoots], BMAX_PATH);
        if (!nRoots || Bstrcmp(roots[0], roots[nRoots]))
            nRoots++;
        Xfree(pzApp);
    }
    // Blood Extra Crispy's folder put straight into the game folder
    if (BDIR *dir = Bopendir("."))
    {
        while (Bdirent *d = Breaddir(dir))
            if (d->name[0] != '.' && isDir(d->name))
                addExtraCrispy(d->name, d->name);
        Bclosedir(dir);
    }
    for (int i = 0; i < nRoots; i++)
    {
        scanDir(roots[i], "addons", false);
        scanDir(roots[i], "mods", true);
    }
    return gModCount;
}

MODENTRY const *modEntry(int nIndex)
{
    return (nIndex >= 0 && nIndex < gModCount) ? &gMods[nIndex] : NULL;
}

bool modIsActive(MODENTRY const *pMod)
{
    if (!bActive)
        return pMod->nKind == kModNone && !Bstrcasecmp(modCurrentName(), "");
    return pMod->nKind == gActive.nKind && !Bstrcasecmp(pMod->szName, gActive.szName);
}

bool modSelect(int nIndex)
{
    MODENTRY const *pMod = modEntry(nIndex);
    if (!pMod)
        return false;
    FILE *fp = fopen(kSelectFile, "w");
    if (!fp)
    {
        LOG_F(ERROR, "Mods: could not write %s", kSelectFile);
        return false;
    }
    char path[BMAX_PATH] = "";
    if (pMod->nKind == kModExtraCrispy)
        Bstrncpyz(path, pMod->szPath, sizeof(path));  // from the game folder
    else if (pMod->szPath[0])
        absolutePath(pMod->szPath, path, sizeof(path));
    fprintf(fp, "; the mod NotBlood loads at startup (chosen in the MODS menu)\n");
    fprintf(fp, "name=%s\nkind=%d\npath=%s\nini=%s\n", pMod->szName, pMod->nKind, path, pMod->szIni);
    fclose(fp);
    // a plain episode .ini is also remembered like the startup window does
    if (pMod->nKind == kModNone || pMod->nKind == kModIni)
        Bstrncpyz(gSetup.lastini, pMod->nKind == kModIni ? pMod->szIni : "BLOOD.INI", sizeof(gSetup.lastini));
    LOG_F(INFO, "Mods: \"%s\" selected, restarting", pMod->szName);
    return true;
}

void modRestart(const char *pzName)
{
    char text[96];
    Bsnprintf(text, sizeof(text), "Loading %s", pzName);
#ifdef MODS_CAN_RESPAWN
    char exe[BMAX_PATH] = "";
# ifdef __APPLE__
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size))
        exe[0] = '\0';
# else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    exe[n > 0 ? n : 0] = '\0';
# endif
    if (exe[0])
    {
        viewLoadingScreen(gMenuPicnum, "Mods", NULL, text);
        videoNextPage();
        pid_t pid;
        if (!posix_spawn(&pid, exe, NULL, NULL, (char *const *)margv, environ))
            QuitGame();
        LOG_F(ERROR, "Mods: could not start %s again", exe);
    }
#elif defined _WIN32
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe, MAX_PATH))
    {
        viewLoadingScreen(gMenuPicnum, "Mods", NULL, text);
        videoNextPage();
        // the old window is still open for a moment, so skip the "already running" question
        wchar_t cmd[4096];
        _snwprintf(cmd, ARRAY_SIZE(cmd), L"%ls -noinstancechecking", GetCommandLineW());
        cmd[ARRAY_SIZE(cmd)-1] = 0;
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            QuitGame();
        }
        LOG_F(ERROR, "Mods: could not start the game again");
    }
#endif
#ifdef __ANDROID__
    // a fresh process: Android would otherwise start the next game in this one, with its old state
    {
        extern void android_restartapp(void);
        viewLoadingScreen(gMenuPicnum, "Mods", NULL, text);
        videoNextPage();
        android_restartapp();
        for (uint32_t nStart = timerGetTicks(); timerGetTicks() - nStart < 3000;)
            handleevents();
        exit(0);  // the restart didn't come: at least the next start is a fresh one
    }
#endif
    // iPad: apps can't start themselves again
    Bsnprintf(text, sizeof(text), "Open NotBlood again to play %s", pzName);
    for (uint32_t nStart = timerGetTicks(); timerGetTicks() - nStart < 4000;)
    {
        handleevents();
        viewLoadingScreen(gMenuPicnum, "Mods", NULL, text);
        videoNextPage();
    }
    QuitGame();
}
