//-------------------------------------------------------------------------
/*
This file is part of NotBlood (iOS port).

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// On-screen touch controls for iPad/iPhone:
//  - floating (or fixed) virtual stick on the left for movement
//  - drag anywhere else to turn/look (also while holding fire)
//  - configurable buttons mapped to game functions
//  - weapon picker, menu helpers (tap = mouse click, d-pad, keyboard)
//  - settings screen + drag/pinch layout editor, saved to touchcontrols.cfg

#include "compat.h"

#ifdef EDUKE32_IOS

#include "build.h"
#include "baselayer.h"
#include "iosbits.h"
#include "keyboard.h"
#include "control.h"
#include "function.h"
#include "osd.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "gamemenu.h"
#include "levels.h"
#include "player.h"
#include "view.h"
#include "touchcontrols.h"

#include <math.h>

extern char textfont[2048];

namespace
{

constexpr char kConfigFile[] = "touchcontrols.cfg";
constexpr int  kConfigVersion = 1;
constexpr int  kMaxFingers = 12;

enum TouchMode { TM_GAME, TM_MENU, TM_SETTINGS, TM_EDIT };

enum ButtonKind { BK_FUNC, BK_KEY, BK_WEAPONS, BK_KEYBOARD };

struct Button
{
    char const *name;
    char const *label;
    int kind;
    int code;    // gamefunc_* or sc_*
    float x, y;  // center: x as fraction of width, y as fraction of height
    float r;     // radius as fraction of height
    bool visible;
    bool aims;   // dragging on it also turns/looks

    // runtime
    bool held;
    int seen;    // game-function updates observed while held
    int pulse;   // keep reporting a quick tap for this many updates
};

Button const kDefaultButtons[] =
{
    { "fire",     "FIRE",      BK_FUNC,     gamefunc_Weapon_Fire,         0.885f, 0.700f, 0.095f, true,  true  },
    { "altfire",  "ALT",       BK_FUNC,     gamefunc_Weapon_Special_Fire, 0.755f, 0.820f, 0.072f, true,  true  },
    { "jump",     "JUMP",      BK_FUNC,     gamefunc_Jump,                0.935f, 0.440f, 0.065f, true,  false },
    { "crouch",   "DUCK",      BK_FUNC,     gamefunc_Crouch,              0.815f, 0.540f, 0.060f, true,  false },
    { "use",      "USE",       BK_FUNC,     gamefunc_Open,                0.695f, 0.630f, 0.060f, true,  false },
    { "nextweap", "WPN\x10",   BK_FUNC,     gamefunc_Next_Weapon,         0.945f, 0.210f, 0.050f, true,  false },
    { "prevweap", "\x11WPN",   BK_FUNC,     gamefunc_Previous_Weapon,     0.855f, 0.210f, 0.050f, true,  false },
    { "weapons",  "WPNS",      BK_WEAPONS,  -1,                           0.765f, 0.210f, 0.050f, true,  false },
    { "invuse",   "ITEM",      BK_FUNC,     gamefunc_Inventory_Use,       0.415f, 0.910f, 0.050f, true,  false },
    { "invprev",  "\x11",      BK_FUNC,     gamefunc_Inventory_Left,      0.345f, 0.910f, 0.040f, true,  false },
    { "invnext",  "\x10",      BK_FUNC,     gamefunc_Inventory_Right,     0.485f, 0.910f, 0.040f, true,  false },
    { "map",      "MAP",       BK_FUNC,     gamefunc_Map_Toggle,          0.600f, 0.075f, 0.045f, true,  false },
    { "menu",     "MENU",      BK_KEY,      sc_Escape,                    0.050f, 0.075f, 0.045f, true,  false },
    { "turn180",  "180",       BK_FUNC,     gamefunc_Turn_Around,         0.690f, 0.075f, 0.045f, false, false },
    { "run",      "RUN",       BK_FUNC,     gamefunc_Run,                 0.300f, 0.075f, 0.045f, false, false },
    { "center",   "CNTR",      BK_FUNC,     gamefunc_Aim_Center,          0.780f, 0.075f, 0.045f, false, false },
    { "qsave",    "QSAV",      BK_FUNC,     gamefunc_Quick_Save,          0.140f, 0.075f, 0.045f, false, false },
    { "qload",    "QLOD",      BK_FUNC,     gamefunc_Quick_Load,          0.220f, 0.075f, 0.045f, false, false },
    { "medkit",   "MED",       BK_FUNC,     gamefunc_MedKit,              0.560f, 0.910f, 0.040f, false, false },
    { "keyboard", "KEYB",      BK_KEYBOARD, -1,                           0.380f, 0.075f, 0.045f, false, false },
};

constexpr int kNumButtons = ARRAY_SIZE(kDefaultButtons);
constexpr int kStickItem = kNumButtons; // layout editor index for the stick

struct Stick
{
    float x = 0.14f, y = 0.72f, r = 0.11f; // fixed/hint position (same units as buttons)
};

struct Settings
{
    float lookSensX = 1.0f;
    float lookSensY = 1.0f;
    bool  invertY = false;
    float deadZone = 0.12f;
    float opacity = 0.45f;
    float buttonScale = 1.0f;
    bool  floatingStick = true;
    bool  alwaysRun = true;
    bool  fireAims = true;
    bool  autoHide = true;
};

// Menu-mode helpers (not editable)
enum MenuAction { MA_KEY, MA_SETTINGS, MA_KEYBOARD };

struct MenuButton
{
    char const *label;
    int action;
    int code;
    float x, y, r;
    bool held;
};

MenuButton g_menuButtons[] =
{
    { "BACK",   MA_KEY,      sc_Escape,     0.050f, 0.075f, 0.045f, false },
    { "TOUCH",  MA_SETTINGS, 0,             0.950f, 0.075f, 0.045f, false },
    { "KEYB",   MA_KEYBOARD, 0,             0.950f, 0.215f, 0.045f, false },
    { "\x1e",   MA_KEY,      sc_UpArrow,    0.120f, 0.665f, 0.050f, false },
    { "\x1f",   MA_KEY,      sc_DownArrow,  0.120f, 0.895f, 0.050f, false },
    { "\x11",   MA_KEY,      sc_LeftArrow,  0.045f, 0.780f, 0.050f, false },
    { "\x10",   MA_KEY,      sc_RightArrow, 0.195f, 0.780f, 0.050f, false },
    { "OK",     MA_KEY,      sc_Return,     0.930f, 0.850f, 0.065f, false },
};

constexpr int kNumMenuButtons = ARRAY_SIZE(g_menuButtons);

enum FingerRole
{
    FR_NONE,
    FR_STICK,
    FR_LOOK,
    FR_BUTTON,
    FR_WEAPONPICK,
    FR_MENUMOUSE,
    FR_MENUBUTTON,
    FR_TAPKEY,
    FR_UI,
    FR_EDIT,
};

struct Finger
{
    SDL_FingerID id;
    bool active;
    int role;
    int index;          // button / widget / layout item
    float startX, startY;
    float lastX, lastY; // output pixels
    uint32_t startTime;
    bool moved;
};

struct WeaponSlot
{
    char const *label;
    int func;
    int weapons[3];
};

WeaponSlot const kWeaponSlots[] =
{
    { "FORK",    gamefunc_Weapon_1,  { kWeaponPitchfork,  -1, -1 } },
    { "FLARE",   gamefunc_Weapon_2,  { kWeaponFlare,      -1, -1 } },
    { "SHOTGUN", gamefunc_Weapon_3,  { kWeaponShotgun,    -1, -1 } },
    { "TOMMY",   gamefunc_Weapon_4,  { kWeaponTommy,      -1, -1 } },
    { "NAPALM",  gamefunc_Weapon_5,  { kWeaponNapalm,     -1, -1 } },
    { "TNT",     gamefunc_Weapon_6,  { kWeaponTNT, kWeaponProxyTNT, kWeaponRemoteTNT } },
    { "SPRAY",   gamefunc_Weapon_7,  { kWeaponSprayCan,   -1, -1 } },
    { "TESLA",   gamefunc_Weapon_8,  { kWeaponTesla,      -1, -1 } },
    { "LEECH",   gamefunc_Weapon_9,  { kWeaponLifeLeech,  -1, -1 } },
    { "VOODOO",  gamefunc_Weapon_10, { kWeaponVoodoo,     -1, -1 } },
};

constexpr int kNumWeaponSlots = ARRAY_SIZE(kWeaponSlots);

// UI widgets for the settings screen
enum WidgetType { WT_SLIDER, WT_TOGGLE, WT_BUTTON, WT_TAB };

enum WidgetAction
{
    WA_NONE,
    WA_DONE,
    WA_EDIT,
    WA_RESET,
    WA_TAB_GENERAL,
    WA_TAB_BUTTONS,
    WA_EDIT_DONE,
    WA_EDIT_SMALLER,
    WA_EDIT_BIGGER,
    WA_EDIT_HIDE,
    WA_EDIT_RESETITEM,
};

struct Widget
{
    int type;
    SDL_FRect rect;
    char const *label;
    float *fvalue;
    bool *bvalue;
    float vmin, vmax;
    int action;
    bool highlight;
};

//
// state
//

Button   g_buttons[kNumButtons];
Stick    g_stick;
Settings g_settings;
Finger   g_fingers[kMaxFingers];

bool     g_initialized;
int      g_mode = TM_MENU;
int      g_lastMode = -1;
int      g_settingsTab;
int      g_outW, g_outH;
SDL_Rect g_gameRect;
uint32_t g_frame;

// stick
bool  g_stickActive;
float g_stickBaseX, g_stickBaseY; // output pixels
float g_stickDX, g_stickDY;       // normalized -1..1

// look accumulation (output pixels)
float g_lookDX, g_lookDY;

// weapon picker
bool g_weaponPickerOpen;
int  g_weaponHover = -1;
int  g_weaponPulseFunc = -1;
int  g_weaponPulse;

// menu mouse emulation
uint32_t g_menuPressFrame;
bool     g_menuReleasePending;

// keys pressed by touch that need releasing
int g_tapKeyRelease = -1;

// auto-hide when keyboard/mouse is in use
bool g_hiddenByHardware;

// layout editor
int   g_editSelected = -1;
float g_pinchStartDist, g_pinchStartR;

// rendering resources
SDL_Renderer *g_renderer;
SDL_Texture  *g_fontTexture;

//
// helpers
//

inline float unitPx(void) { return (float)g_outH; }
inline float itemPX(float fx) { return fx * g_outW; }
inline float itemPY(float fy) { return fy * g_outH; }

float buttonRadiusPx(Button const &b) { return b.r * g_settings.buttonScale * unitPx(); }
float stickRadiusPx(void) { return g_stick.r * g_settings.buttonScale * unitPx(); }

inline float dist2(float ax, float ay, float bx, float by)
{
    float const dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

inline bool inRect(SDL_FRect const &r, float px, float py)
{
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

void pressKey(int sc)
{
    keySetState(sc, 1);
}

void releaseKey(int sc)
{
    keySetState(sc, 0);
}

void resetButtons(void)
{
    for (int i = 0; i < kNumButtons; i++)
        g_buttons[i] = kDefaultButtons[i];
    g_stick = Stick();
}

void releaseGameInput(void)
{
    for (auto &b : g_buttons)
    {
        if (b.held && b.kind == BK_KEY)
            releaseKey(b.code);
        b.held = false;
        b.pulse = 0;
        b.seen = 0;
    }

    for (auto &f : g_fingers)
        if (f.active && f.role != FR_UI && f.role != FR_EDIT)
            f.role = FR_NONE;

    for (auto &m : g_menuButtons)
    {
        if (m.held && m.action == MA_KEY)
            keySetState(m.code, 0);
        m.held = false;
    }

    g_stickActive = false;
    g_stickDX = g_stickDY = 0.f;
    g_lookDX = g_lookDY = 0.f;
    g_weaponPickerOpen = false;
    g_weaponHover = -1;
}

int currentGameMode(void)
{
    if (osd && (osd->flags & OSD_DRAW))
        return TM_MENU;
    if (gGameMenuMgr.m_bActive)
        return TM_MENU;
    if (!gGameStarted || gInputMode != INPUT_MODE_0)
        return TM_MENU;
    return TM_GAME;
}

void toggleKeyboard(void)
{
    if (SDL_IsTextInputActive())
        SDL_StopTextInput();
    else
        SDL_StartTextInput();
}

//
// config
//

void saveConfig(void)
{
    FILE *fp = fopen(kConfigFile, "w");
    if (!fp)
        return;

    auto const &s = g_settings;
    fprintf(fp, "version %d\n", kConfigVersion);
    fprintf(fp, "lookSensX %f\n", s.lookSensX);
    fprintf(fp, "lookSensY %f\n", s.lookSensY);
    fprintf(fp, "invertY %d\n", s.invertY);
    fprintf(fp, "deadZone %f\n", s.deadZone);
    fprintf(fp, "opacity %f\n", s.opacity);
    fprintf(fp, "buttonScale %f\n", s.buttonScale);
    fprintf(fp, "floatingStick %d\n", s.floatingStick);
    fprintf(fp, "alwaysRun %d\n", s.alwaysRun);
    fprintf(fp, "fireAims %d\n", s.fireAims);
    fprintf(fp, "autoHide %d\n", s.autoHide);
    fprintf(fp, "stick %f %f %f\n", g_stick.x, g_stick.y, g_stick.r);

    for (auto const &b : g_buttons)
        fprintf(fp, "button %s %f %f %f %d\n", b.name, b.x, b.y, b.r, b.visible);

    fclose(fp);
}

void loadConfig(void)
{
    FILE *fp = fopen(kConfigFile, "r");
    if (!fp)
        return;

    char line[256], key[64], name[64];
    auto &s = g_settings;

    while (fgets(line, sizeof(line), fp))
    {
        float f1, f2, f3;
        int i1;

        if (sscanf(line, "%63s", key) != 1)
            continue;

        if (!strcmp(key, "button"))
        {
            if (sscanf(line, "%*s %63s %f %f %f %d", name, &f1, &f2, &f3, &i1) == 5)
            {
                for (auto &b : g_buttons)
                {
                    if (!strcmp(b.name, name))
                    {
                        b.x = clamp(f1, 0.f, 1.f);
                        b.y = clamp(f2, 0.f, 1.f);
                        b.r = clamp(f3, 0.02f, 0.25f);
                        b.visible = !!i1;
                    }
                }
            }
        }
        else if (!strcmp(key, "stick"))
        {
            if (sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3)
            {
                g_stick.x = clamp(f1, 0.f, 1.f);
                g_stick.y = clamp(f2, 0.f, 1.f);
                g_stick.r = clamp(f3, 0.04f, 0.3f);
            }
        }
        else if (sscanf(line, "%*s %f", &f1) == 1)
        {
            if (!strcmp(key, "lookSensX")) s.lookSensX = clamp(f1, 0.05f, 5.f);
            else if (!strcmp(key, "lookSensY")) s.lookSensY = clamp(f1, 0.05f, 5.f);
            else if (!strcmp(key, "invertY")) s.invertY = f1 != 0.f;
            else if (!strcmp(key, "deadZone")) s.deadZone = clamp(f1, 0.f, 0.6f);
            else if (!strcmp(key, "opacity")) s.opacity = clamp(f1, 0.05f, 1.f);
            else if (!strcmp(key, "buttonScale")) s.buttonScale = clamp(f1, 0.5f, 2.f);
            else if (!strcmp(key, "floatingStick")) s.floatingStick = f1 != 0.f;
            else if (!strcmp(key, "alwaysRun")) s.alwaysRun = f1 != 0.f;
            else if (!strcmp(key, "fireAims")) s.fireAims = f1 != 0.f;
            else if (!strcmp(key, "autoHide")) s.autoHide = f1 != 0.f;
        }
    }

    fclose(fp);
}

void initialize(void)
{
    if (g_initialized)
        return;

    resetButtons();
    loadConfig();
    g_initialized = true;
}

//
// drawing
//

SDL_Color rgba(int r, int g, int b, float a)
{
    return SDL_Color { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)clamp((int)(a * 255.f), 0, 255) };
}

void fillCircle(float cx, float cy, float rad, SDL_Color c)
{
    constexpr int kSeg = 48;
    SDL_Vertex v[kSeg + 1];
    int idx[kSeg * 3];

    v[0] = { { cx, cy }, c, { 0, 0 } };
    for (int i = 0; i < kSeg; i++)
    {
        float const a = (float)i * (2.f * (float)M_PI / kSeg);
        v[i + 1] = { { cx + cosf(a) * rad, cy + sinf(a) * rad }, c, { 0, 0 } };
        idx[i * 3 + 0] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = (i + 1) % kSeg + 1;
    }

    SDL_RenderGeometry(g_renderer, NULL, v, kSeg + 1, idx, kSeg * 3);
}

void ringCircle(float cx, float cy, float rad, float thick, SDL_Color c)
{
    constexpr int kSeg = 48;
    SDL_Vertex v[kSeg * 2];
    int idx[kSeg * 6];
    float const rin = max(rad - thick, 0.f);

    for (int i = 0; i < kSeg; i++)
    {
        float const a = (float)i * (2.f * (float)M_PI / kSeg);
        float const ca = cosf(a), sa = sinf(a);
        v[i * 2 + 0] = { { cx + ca * rad, cy + sa * rad }, c, { 0, 0 } };
        v[i * 2 + 1] = { { cx + ca * rin, cy + sa * rin }, c, { 0, 0 } };

        int const n = (i + 1) % kSeg;
        idx[i * 6 + 0] = i * 2;
        idx[i * 6 + 1] = n * 2;
        idx[i * 6 + 2] = i * 2 + 1;
        idx[i * 6 + 3] = i * 2 + 1;
        idx[i * 6 + 4] = n * 2;
        idx[i * 6 + 5] = n * 2 + 1;
    }

    SDL_RenderGeometry(g_renderer, NULL, v, kSeg * 2, idx, kSeg * 6);
}

void fillRect(SDL_FRect const &r, SDL_Color c)
{
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectF(g_renderer, &r);
}

void outlineRect(SDL_FRect const &r, float thick, SDL_Color c)
{
    fillRect({ r.x, r.y, r.w, thick }, c);
    fillRect({ r.x, r.y + r.h - thick, r.w, thick }, c);
    fillRect({ r.x, r.y, thick, r.h }, c);
    fillRect({ r.x + r.w - thick, r.y, thick, r.h }, c);
}

void ensureFont(void)
{
    if (g_fontTexture)
        return;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 128, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf)
        return;

    auto pixels = (uint32_t *)surf->pixels;
    int const pitch = surf->pitch / 4;

    for (int ch = 0; ch < 256; ch++)
    {
        int const gx = (ch & 15) * 8, gy = (ch >> 4) * 8;
        for (int row = 0; row < 8; row++)
        {
            uint8_t const bits = (uint8_t)textfont[ch * 8 + row];
            for (int col = 0; col < 8; col++)
                pixels[(gy + row) * pitch + gx + col] = (bits & (0x80 >> col)) ? 0xFFFFFFFFu : 0x00FFFFFFu;
        }
    }

    g_fontTexture = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_FreeSurface(surf);

    if (g_fontTexture)
    {
        SDL_SetTextureBlendMode(g_fontTexture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_fontTexture, SDL_ScaleModeNearest);
    }
}

// base text scale: glyph pixels per font pixel
float textScale(float heightFrac)
{
    return max(1.f, floorf(g_outH * heightFrac / 8.f));
}

float textWidth(char const *s, float scale)
{
    return (float)strlen(s) * 8.f * scale;
}

// align: 0 = left, 1 = center, 2 = right; y is the vertical center
void drawText(char const *s, float x, float y, float scale, SDL_Color c, int align = 1)
{
    ensureFont();
    if (!g_fontTexture || !s)
        return;

    float const w = textWidth(s, scale);
    float px = align == 1 ? x - w * 0.5f : (align == 2 ? x - w : x);
    float const py = y - 4.f * scale;

    SDL_SetTextureColorMod(g_fontTexture, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g_fontTexture, c.a);

    for (; *s; s++, px += 8.f * scale)
    {
        uint8_t const ch = (uint8_t)*s;
        SDL_Rect const src = { (ch & 15) * 8, (ch >> 4) * 8, 8, 8 };
        SDL_FRect const dst = { px, py, 8.f * scale, 8.f * scale };
        SDL_RenderCopyF(g_renderer, g_fontTexture, &src, &dst);
    }
}

// Fits a label inside a circle of the given radius.
void drawLabelInCircle(char const *label, float cx, float cy, float rad, SDL_Color c)
{
    float scale = textScale(0.024f);
    float const maxW = rad * 1.55f;
    while (scale > 1.f && textWidth(label, scale) > maxW)
        scale -= 1.f;
    drawText(label, cx, cy, scale, c);
}

void drawRoundButton(char const *label, float cx, float cy, float rad, bool pressed, float alpha, bool hiddenStyle = false)
{
    float const a = alpha * (pressed ? 1.6f : 1.f);
    fillCircle(cx, cy, rad, rgba(pressed ? 160 : 20, pressed ? 20 : 20, pressed ? 20 : 20, min(a * 0.8f, 0.9f)));
    ringCircle(cx, cy, rad, max(2.f, rad * 0.08f), hiddenStyle ? rgba(140, 140, 140, a) : rgba(200, 30, 30, min(a * 1.4f, 1.f)));
    drawLabelInCircle(label, cx, cy, rad, rgba(235, 225, 210, min(a * 1.8f, 1.f)));
}

//
// game mode
//

int hitButton(float px, float py)
{
    int best = -1;
    float bestD = 0.f;

    for (int i = 0; i < kNumButtons; i++)
    {
        auto const &b = g_buttons[i];
        if (!b.visible)
            continue;

        float const r = buttonRadiusPx(b) * 1.15f; // a little forgiving
        float const d = dist2(px, py, itemPX(b.x), itemPY(b.y));

        if (d <= r * r && (best < 0 || d < bestD))
            best = i, bestD = d;
    }

    return best;
}

bool inStickZone(float px, float py)
{
    if (g_settings.floatingStick)
        return px < g_outW * 0.45f && py > g_outH * 0.16f;

    float const r = stickRadiusPx() * 1.8f;
    return dist2(px, py, itemPX(g_stick.x), itemPY(g_stick.y)) <= r * r;
}

void weaponSlotRect(int i, SDL_FRect *pRect)
{
    float const w = g_outW * 0.082f, h = g_outH * 0.13f, gap = g_outW * 0.01f;
    float const total = kNumWeaponSlots * w + (kNumWeaponSlots - 1) * gap;
    *pRect = { (g_outW - total) * 0.5f + i * (w + gap), g_outH * 0.30f, w, h };
}

int hitWeaponSlot(float px, float py)
{
    for (int i = 0; i < kNumWeaponSlots; i++)
    {
        SDL_FRect r;
        weaponSlotRect(i, &r);
        if (inRect(r, px, py))
            return i;
    }
    return -1;
}

bool weaponSlotAvailable(int i)
{
    if (!gMe)
        return true;

    for (int const w : kWeaponSlots[i].weapons)
        if (w >= 0 && w < kWeaponMax && gMe->hasWeapon[w])
            return true;

    return false;
}

void selectWeaponSlot(int i)
{
    if ((unsigned)i >= (unsigned)kNumWeaponSlots)
        return;
    g_weaponPulseFunc = kWeaponSlots[i].func;
    g_weaponPulse = 2;
}

void updateStick(Finger const &f)
{
    float const r = stickRadiusPx();
    float dx = (f.lastX - g_stickBaseX) / r, dy = (f.lastY - g_stickBaseY) / r;
    float const len = sqrtf(dx * dx + dy * dy);

    if (len > 1.f)
        dx /= len, dy /= len;

    g_stickDX = dx;
    g_stickDY = dy;
}

void gameFingerDown(Finger &f)
{
    if (g_weaponPickerOpen)
    {
        int const slot = hitWeaponSlot(f.lastX, f.lastY);
        if (slot >= 0)
            selectWeaponSlot(slot);

        int const b = hitButton(f.lastX, f.lastY);
        g_weaponPickerOpen = false;

        if (slot >= 0 || (b >= 0 && g_buttons[b].kind == BK_WEAPONS))
        {
            f.role = FR_NONE;
            return;
        }
    }

    int const b = hitButton(f.lastX, f.lastY);

    if (b >= 0)
    {
        auto &btn = g_buttons[b];
        f.role = FR_BUTTON;
        f.index = b;

        switch (btn.kind)
        {
            case BK_FUNC:
                btn.held = true;
                btn.seen = 0;
                break;
            case BK_KEY:
                btn.held = true;
                pressKey(btn.code);
                break;
            case BK_WEAPONS:
                g_weaponPickerOpen = true;
                g_weaponHover = -1;
                f.role = FR_WEAPONPICK;
                break;
            case BK_KEYBOARD:
                toggleKeyboard();
                break;
        }
        return;
    }

    if (!g_stickActive && inStickZone(f.lastX, f.lastY))
    {
        f.role = FR_STICK;
        g_stickActive = true;

        if (g_settings.floatingStick)
        {
            float const r = stickRadiusPx();
            g_stickBaseX = clamp(f.lastX, r, (float)g_outW - r);
            g_stickBaseY = clamp(f.lastY, r, (float)g_outH - r);
        }
        else
        {
            g_stickBaseX = itemPX(g_stick.x);
            g_stickBaseY = itemPY(g_stick.y);
        }

        updateStick(f);
        return;
    }

    for (auto const &o : g_fingers)
        if (o.active && &o != &f && o.role == FR_LOOK)
            return; // one look finger at a time

    f.role = FR_LOOK;
}

void gameFingerMove(Finger &f, float dx, float dy)
{
    switch (f.role)
    {
        case FR_STICK:
            updateStick(f);
            break;
        case FR_LOOK:
            g_lookDX += dx;
            g_lookDY += dy;
            break;
        case FR_BUTTON:
            if (g_settings.fireAims && g_buttons[f.index].aims)
            {
                g_lookDX += dx;
                g_lookDY += dy;
            }
            break;
        case FR_WEAPONPICK:
            g_weaponHover = hitWeaponSlot(f.lastX, f.lastY);
            break;
    }
}

void gameFingerUp(Finger &f)
{
    switch (f.role)
    {
        case FR_STICK:
            g_stickActive = false;
            g_stickDX = g_stickDY = 0.f;
            break;
        case FR_BUTTON:
        {
            auto &btn = g_buttons[f.index];
            if (btn.kind == BK_FUNC)
            {
                if (btn.held && btn.seen == 0)
                    btn.pulse = 2; // tap shorter than a frame
                btn.held = false;
            }
            else if (btn.kind == BK_KEY)
            {
                btn.held = false;
                releaseKey(btn.code);
            }
            break;
        }
        case FR_WEAPONPICK:
        {
            // slide-to-select; releasing on the button keeps the picker open for a tap
            int const slot = hitWeaponSlot(f.lastX, f.lastY);
            if (slot >= 0)
            {
                selectWeaponSlot(slot);
                g_weaponPickerOpen = false;
            }
            g_weaponHover = -1;
            break;
        }
    }
}

void renderGame(void)
{
    if (g_hiddenByHardware)
        return;

    float const alpha = g_settings.opacity;

    // stick
    {
        float const r = stickRadiusPx();
        float bx, by;

        if (g_stickActive)
            bx = g_stickBaseX, by = g_stickBaseY;
        else
            bx = itemPX(g_stick.x), by = itemPY(g_stick.y);

        float const a = g_stickActive ? alpha : alpha * 0.5f;
        fillCircle(bx, by, r, rgba(20, 20, 20, a * 0.6f));
        ringCircle(bx, by, r, max(2.f, r * 0.05f), rgba(200, 30, 30, min(a * 1.3f, 1.f)));

        float const kx = bx + g_stickDX * r, ky = by + g_stickDY * r;
        fillCircle(kx, ky, r * 0.42f, rgba(160, 20, 20, min(a * 1.2f, 0.9f)));
        ringCircle(kx, ky, r * 0.42f, max(2.f, r * 0.04f), rgba(235, 225, 210, min(a * 1.5f, 1.f)));
    }

    for (auto const &b : g_buttons)
    {
        if (!b.visible)
            continue;
        bool const pressed = b.held || (b.kind == BK_WEAPONS && g_weaponPickerOpen);
        drawRoundButton(b.label, itemPX(b.x), itemPY(b.y), buttonRadiusPx(b), pressed, alpha);
    }

    if (g_weaponPickerOpen)
    {
        float const scaleBig = textScale(0.04f), scaleSmall = textScale(0.016f);
        int const curWeapon = gMe ? gMe->curWeapon : -1;

        for (int i = 0; i < kNumWeaponSlots; i++)
        {
            SDL_FRect r;
            weaponSlotRect(i, &r);

            bool const avail = weaponSlotAvailable(i);
            bool current = false;
            for (int const w : kWeaponSlots[i].weapons)
                current |= (w >= 0 && w == curWeapon);

            float const a = avail ? 0.85f : 0.35f;
            fillRect(r, i == g_weaponHover ? rgba(150, 20, 20, 0.9f) : rgba(15, 15, 15, a * 0.85f));
            outlineRect(r, max(2.f, g_outH * 0.003f), current ? rgba(255, 200, 60, 1.f) : rgba(200, 30, 30, a));

            char num[4];
            Bsnprintf(num, sizeof(num), "%d", (i + 1) % 10);
            drawText(num, r.x + r.w * 0.5f, r.y + r.h * 0.38f, scaleBig, rgba(235, 225, 210, a));
            drawText(kWeaponSlots[i].label, r.x + r.w * 0.5f, r.y + r.h * 0.80f, scaleSmall, rgba(235, 225, 210, a));
        }
    }
}

//
// menu mode
//

int hitMenuButton(float px, float py)
{
    for (int i = 0; i < kNumMenuButtons; i++)
    {
        auto const &m = g_menuButtons[i];
        float const r = m.r * unitPx() * 1.15f;
        if (dist2(px, py, itemPX(m.x), itemPY(m.y)) <= r * r)
            return i;
    }
    return -1;
}

void outputToGame(float px, float py, int32_t *gx, int32_t *gy)
{
    float const rw = (float)max(g_gameRect.w, 1), rh = (float)max(g_gameRect.h, 1);
    *gx = clamp((int32_t)((px - g_gameRect.x) * xres / rw), 0, max(xres - 1, 0));
    *gy = clamp((int32_t)((py - g_gameRect.y) * yres / rh), 0, max(yres - 1, 0));
}

// a "real" menu that understands mouse clicks is on screen
bool menuTakesClicks(void)
{
    return gGameMenuMgr.m_bActive && !(osd && (osd->flags & OSD_DRAW));
}

void enterSettings(void);

void menuFingerDown(Finger &f)
{
    int const m = hitMenuButton(f.lastX, f.lastY);

    if (m >= 0)
    {
        auto &mb = g_menuButtons[m];
        f.role = FR_MENUBUTTON;
        f.index = m;
        mb.held = true;

        if (mb.action == MA_KEY)
            pressKey(mb.code);
        return;
    }

    if (menuTakesClicks())
    {
        f.role = FR_MENUMOUSE;
        outputToGame(f.lastX, f.lastY, &g_mouseAbs.x, &g_mouseAbs.y);
        g_mouseInsideWindow = 1;
        g_mouseClickState = MOUSE_PRESSED;
        g_menuPressFrame = g_frame;
        g_menuReleasePending = false;
        return;
    }

    // intermission / "press a key" screens / cutscenes
    f.role = FR_TAPKEY;
}

void menuFingerMove(Finger &f)
{
    if (f.role == FR_MENUMOUSE)
        outputToGame(f.lastX, f.lastY, &g_mouseAbs.x, &g_mouseAbs.y);
}

void menuFingerUp(Finger &f)
{
    switch (f.role)
    {
        case FR_MENUBUTTON:
        {
            auto &mb = g_menuButtons[f.index];
            if (mb.held)
            {
                mb.held = false;
                if (mb.action == MA_KEY)
                    releaseKey(mb.code);
                else if (mb.action == MA_KEYBOARD)
                    toggleKeyboard();
                else if (mb.action == MA_SETTINGS)
                    enterSettings();
            }
            break;
        }
        case FR_MENUMOUSE:
            outputToGame(f.lastX, f.lastY, &g_mouseAbs.x, &g_mouseAbs.y);
            // the menu has to see the press before the release, or the click is lost
            if (g_menuPressFrame == g_frame)
                g_menuReleasePending = true;
            else
                g_mouseClickState = MOUSE_RELEASED;
            break;
        case FR_TAPKEY:
            if (!f.moved)
            {
                pressKey(sc_Return);
                if (!keyBufferFull())
                    keyBufferInsert('\r');
                g_tapKeyRelease = sc_Return;
            }
            break;
    }
}

void renderMenu(void)
{
    if (g_hiddenByHardware)
        return;

    float const alpha = max(g_settings.opacity * 0.8f, 0.25f);

    for (auto const &m : g_menuButtons)
    {
        if (m.action == MA_KEYBOARD && SDL_IsTextInputActive())
            drawRoundButton(m.label, itemPX(m.x), itemPY(m.y), m.r * unitPx(), true, alpha);
        else
            drawRoundButton(m.label, itemPX(m.x), itemPY(m.y), m.r * unitPx(), m.held, alpha);
    }
}

//
// settings screen
//

void buildSettingsWidgets(Widget *w, int *pCount, int maxCount)
{
    int n = 0;
    auto add = [&](Widget const &wd) { if (n < maxCount) w[n++] = wd; };

    float const W = (float)g_outW, H = (float)g_outH;
    float const margin = W * 0.05f;
    float const rowH = H * 0.085f;
    float const top = H * 0.17f;

    // tabs
    add({ WT_TAB, { margin, H * 0.075f, W * 0.18f, H * 0.065f }, "GENERAL", nullptr, nullptr, 0, 0, WA_TAB_GENERAL, g_settingsTab == 0 });
    add({ WT_TAB, { margin + W * 0.19f, H * 0.075f, W * 0.18f, H * 0.065f }, "BUTTONS", nullptr, nullptr, 0, 0, WA_TAB_BUTTONS, g_settingsTab == 1 });

    if (g_settingsTab == 0)
    {
        float const colW = W * 0.42f;
        float y = top;
        auto &s = g_settings;

        add({ WT_SLIDER, { margin, y, colW, rowH * 0.8f }, "LOOK SPEED X", &s.lookSensX, nullptr, 0.1f, 4.f, WA_NONE, false }); y += rowH;
        add({ WT_SLIDER, { margin, y, colW, rowH * 0.8f }, "LOOK SPEED Y", &s.lookSensY, nullptr, 0.1f, 4.f, WA_NONE, false }); y += rowH;
        add({ WT_SLIDER, { margin, y, colW, rowH * 0.8f }, "STICK DEAD ZONE", &s.deadZone, nullptr, 0.f, 0.5f, WA_NONE, false }); y += rowH;
        add({ WT_SLIDER, { margin, y, colW, rowH * 0.8f }, "OPACITY", &s.opacity, nullptr, 0.1f, 1.f, WA_NONE, false }); y += rowH;
        add({ WT_SLIDER, { margin, y, colW, rowH * 0.8f }, "BUTTON SIZE", &s.buttonScale, nullptr, 0.6f, 1.6f, WA_NONE, false }); y += rowH;

        float const x2 = W * 0.53f, col2W = W * 0.42f;
        y = top;
        add({ WT_TOGGLE, { x2, y, col2W, rowH * 0.8f }, "INVERT LOOK Y", nullptr, &s.invertY, 0, 0, WA_NONE, false }); y += rowH;
        add({ WT_TOGGLE, { x2, y, col2W, rowH * 0.8f }, "FLOATING STICK", nullptr, &s.floatingStick, 0, 0, WA_NONE, false }); y += rowH;
        add({ WT_TOGGLE, { x2, y, col2W, rowH * 0.8f }, "ALWAYS RUN", nullptr, &s.alwaysRun, 0, 0, WA_NONE, false }); y += rowH;
        add({ WT_TOGGLE, { x2, y, col2W, rowH * 0.8f }, "DRAG FIRE TO AIM", nullptr, &s.fireAims, 0, 0, WA_NONE, false }); y += rowH;
        add({ WT_TOGGLE, { x2, y, col2W, rowH * 0.8f }, "HIDE FOR KEYBOARD/MOUSE", nullptr, &s.autoHide, 0, 0, WA_NONE, false }); y += rowH;
    }
    else
    {
        // visibility chips, 4 columns
        int const cols = 4;
        float const chipW = (W - margin * 2.f - (cols - 1) * W * 0.015f) / cols;
        float const chipH = H * 0.075f;

        for (int i = 0; i < kNumButtons; i++)
        {
            float const cx = margin + (i % cols) * (chipW + W * 0.015f);
            float const cy = top + (i / cols) * (chipH + H * 0.018f);
            add({ WT_TOGGLE, { cx, cy, chipW, chipH }, g_buttons[i].name, nullptr, &g_buttons[i].visible, 0, 0, WA_NONE, false });
        }
    }

    // bottom bar
    float const by = H * 0.86f, bh = H * 0.09f, bw = W * 0.2f;
    add({ WT_BUTTON, { margin, by, bw, bh }, "EDIT LAYOUT", nullptr, nullptr, 0, 0, WA_EDIT, false });
    add({ WT_BUTTON, { margin + bw + W * 0.02f, by, bw, bh }, "RESET ALL", nullptr, nullptr, 0, 0, WA_RESET, false });
    add({ WT_BUTTON, { W - margin - bw, by, bw, bh }, "DONE", nullptr, nullptr, 0, 0, WA_DONE, true });

    *pCount = n;
}

void buildEditWidgets(Widget *w, int *pCount, int maxCount)
{
    int n = 0;
    auto add = [&](Widget const &wd) { if (n < maxCount) w[n++] = wd; };

    float const W = (float)g_outW, H = (float)g_outH;
    float const bh = H * 0.07f, bw = W * 0.085f, y = H * 0.4f;
    float x = W * 0.5f - (bw * 5.f + W * 0.01f * 4.f) * 0.5f;

    add({ WT_BUTTON, { x, y, bw, bh }, "-", nullptr, nullptr, 0, 0, WA_EDIT_SMALLER, false }); x += bw + W * 0.01f;
    add({ WT_BUTTON, { x, y, bw, bh }, "+", nullptr, nullptr, 0, 0, WA_EDIT_BIGGER, false }); x += bw + W * 0.01f;
    add({ WT_BUTTON, { x, y, bw, bh }, "HIDE", nullptr, nullptr, 0, 0, WA_EDIT_HIDE, false }); x += bw + W * 0.01f;
    add({ WT_BUTTON, { x, y, bw, bh }, "RESET", nullptr, nullptr, 0, 0, WA_EDIT_RESETITEM, false }); x += bw + W * 0.01f;
    add({ WT_BUTTON, { x, y, bw, bh }, "DONE", nullptr, nullptr, 0, 0, WA_EDIT_DONE, true });

    *pCount = n;
}

constexpr int kMaxWidgets = 48;

void drawWidgets(Widget const *w, int count)
{
    float const thick = max(2.f, g_outH * 0.003f);
    float const scale = textScale(0.022f);

    for (int i = 0; i < count; i++)
    {
        auto const &wd = w[i];
        auto const &r = wd.rect;

        switch (wd.type)
        {
            case WT_SLIDER:
            {
                float const t = (*wd.fvalue - wd.vmin) / (wd.vmax - wd.vmin);
                char buf[64];
                Bsnprintf(buf, sizeof(buf), "%s: %.2f", wd.label, *wd.fvalue);
                drawText(buf, r.x, r.y + r.h * 0.22f, scale, rgba(235, 225, 210, 1.f), 0);

                SDL_FRect const track = { r.x, r.y + r.h * 0.62f, r.w, r.h * 0.16f };
                fillRect(track, rgba(70, 70, 70, 1.f));
                fillRect({ track.x, track.y, track.w * clamp(t, 0.f, 1.f), track.h }, rgba(180, 25, 25, 1.f));
                fillCircle(track.x + track.w * clamp(t, 0.f, 1.f), track.y + track.h * 0.5f, r.h * 0.22f, rgba(235, 225, 210, 1.f));
                break;
            }
            case WT_TOGGLE:
            {
                bool const on = *wd.bvalue;
                fillRect(r, on ? rgba(120, 15, 15, 0.9f) : rgba(35, 35, 35, 0.9f));
                outlineRect(r, thick, on ? rgba(230, 60, 40, 1.f) : rgba(110, 110, 110, 1.f));
                char buf[64];
                Bsnprintf(buf, sizeof(buf), "%s %s", on ? "[X]" : "[ ]", wd.label);
                float s = scale;
                while (s > 1.f && textWidth(buf, s) > r.w * 0.94f)
                    s -= 1.f;
                drawText(buf, r.x + r.w * 0.04f, r.y + r.h * 0.5f, s, rgba(235, 225, 210, 1.f), 0);
                break;
            }
            case WT_BUTTON:
            case WT_TAB:
            {
                fillRect(r, wd.highlight ? rgba(150, 20, 20, 0.95f) : rgba(40, 40, 40, 0.95f));
                outlineRect(r, thick, rgba(230, 60, 40, 1.f));
                float s = scale;
                while (s > 1.f && textWidth(wd.label, s) > r.w * 0.9f)
                    s -= 1.f;
                drawText(wd.label, r.x + r.w * 0.5f, r.y + r.h * 0.5f, s, rgba(235, 225, 210, 1.f));
                break;
            }
        }
    }
}

void setSliderFromX(Widget const &wd, float px)
{
    float const t = clamp((px - wd.rect.x) / max(wd.rect.w, 1.f), 0.f, 1.f);
    float v = wd.vmin + t * (wd.vmax - wd.vmin);
    v = roundf(v * 100.f) / 100.f;
    *wd.fvalue = v;
}

void enterSettings(void)
{
    releaseGameInput();
    g_mode = TM_SETTINGS;
    g_settingsTab = 0;
}

void doWidgetAction(int action)
{
    switch (action)
    {
        case WA_DONE:
            saveConfig();
            g_mode = currentGameMode();
            break;
        case WA_EDIT:
            g_mode = TM_EDIT;
            g_editSelected = -1;
            break;
        case WA_RESET:
            resetButtons();
            g_settings = Settings();
            break;
        case WA_TAB_GENERAL:
            g_settingsTab = 0;
            break;
        case WA_TAB_BUTTONS:
            g_settingsTab = 1;
            break;
        case WA_EDIT_DONE:
            saveConfig();
            g_mode = TM_SETTINGS;
            break;
        case WA_EDIT_SMALLER:
        case WA_EDIT_BIGGER:
        {
            float const f = action == WA_EDIT_BIGGER ? 1.1f : 1.f / 1.1f;
            if (g_editSelected == kStickItem)
                g_stick.r = clamp(g_stick.r * f, 0.04f, 0.3f);
            else if (g_editSelected >= 0)
                g_buttons[g_editSelected].r = clamp(g_buttons[g_editSelected].r * f, 0.02f, 0.25f);
            break;
        }
        case WA_EDIT_HIDE:
            if (g_editSelected >= 0 && g_editSelected < kNumButtons)
            {
                g_buttons[g_editSelected].visible = false;
                g_editSelected = -1;
            }
            break;
        case WA_EDIT_RESETITEM:
            if (g_editSelected == kStickItem)
                g_stick = Stick();
            else if (g_editSelected >= 0)
            {
                auto &b = g_buttons[g_editSelected];
                b.x = kDefaultButtons[g_editSelected].x;
                b.y = kDefaultButtons[g_editSelected].y;
                b.r = kDefaultButtons[g_editSelected].r;
            }
            break;
    }
}

int hitWidget(Widget const *w, int count, float px, float py)
{
    for (int i = 0; i < count; i++)
    {
        SDL_FRect r = w[i].rect;
        if (w[i].type == WT_SLIDER) // grow sliders vertically for fat fingers
            r.y -= r.h * 0.2f, r.h *= 1.4f;
        if (inRect(r, px, py))
            return i;
    }
    return -1;
}

void settingsFingerDown(Finger &f)
{
    Widget w[kMaxWidgets];
    int count;
    buildSettingsWidgets(w, &count, kMaxWidgets);

    int const i = hitWidget(w, count, f.lastX, f.lastY);
    f.role = FR_UI;
    f.index = i;

    if (i >= 0 && w[i].type == WT_SLIDER)
        setSliderFromX(w[i], f.lastX);
}

void settingsFingerMove(Finger &f)
{
    if (f.index < 0)
        return;

    Widget w[kMaxWidgets];
    int count;
    buildSettingsWidgets(w, &count, kMaxWidgets);

    if (f.index < count && w[f.index].type == WT_SLIDER)
        setSliderFromX(w[f.index], f.lastX);
}

void settingsFingerUp(Finger &f)
{
    if (f.index < 0)
        return;

    Widget w[kMaxWidgets];
    int count;
    buildSettingsWidgets(w, &count, kMaxWidgets);

    if (f.index >= count || hitWidget(w, count, f.lastX, f.lastY) != f.index)
        return;

    auto const &wd = w[f.index];
    if (wd.type == WT_TOGGLE)
        *wd.bvalue = !*wd.bvalue;
    else if (wd.type == WT_BUTTON || wd.type == WT_TAB)
        doWidgetAction(wd.action);
}

void renderSettings(void)
{
    fillRect({ 0, 0, (float)g_outW, (float)g_outH }, rgba(0, 0, 0, 0.82f));
    drawText("TOUCH CONTROLS", g_outW * 0.95f, g_outH * 0.108f, textScale(0.035f), rgba(220, 40, 30, 1.f), 2);

    Widget w[kMaxWidgets];
    int count;
    buildSettingsWidgets(w, &count, kMaxWidgets);
    drawWidgets(w, count);

    if (g_settingsTab == 1)
        drawText("TAP TO SHOW/HIDE. USE EDIT LAYOUT TO MOVE AND RESIZE.", g_outW * 0.5f, g_outH * 0.80f, textScale(0.018f), rgba(180, 170, 160, 1.f));
}

//
// layout editor
//

int hitEditItem(float px, float py)
{
    int const b = hitButton(px, py);
    if (b >= 0)
        return b;

    float const r = stickRadiusPx();
    if (dist2(px, py, itemPX(g_stick.x), itemPY(g_stick.y)) <= r * r)
        return kStickItem;

    return -1;
}

void editMoveItem(int item, float dx, float dy)
{
    float &x = item == kStickItem ? g_stick.x : g_buttons[item].x;
    float &y = item == kStickItem ? g_stick.y : g_buttons[item].y;
    x = clamp(x + dx / g_outW, 0.f, 1.f);
    y = clamp(y + dy / g_outH, 0.f, 1.f);
}

void editFingerDown(Finger &f)
{
    Widget w[16];
    int count;
    buildEditWidgets(w, &count, 16);

    int const wi = hitWidget(w, count, f.lastX, f.lastY);
    if (wi >= 0)
    {
        f.role = FR_UI;
        f.index = wi;
        return;
    }

    // second finger while dragging an item: pinch to resize it
    for (auto const &o : g_fingers)
    {
        if (o.active && &o != &f && o.role == FR_EDIT && o.index >= 0)
        {
            f.role = FR_EDIT;
            f.index = -2; // pinch helper
            g_pinchStartDist = sqrtf(dist2(o.lastX, o.lastY, f.lastX, f.lastY));
            g_pinchStartR = o.index == kStickItem ? g_stick.r : g_buttons[o.index].r;
            return;
        }
    }

    f.role = FR_EDIT;
    f.index = hitEditItem(f.lastX, f.lastY);
    if (f.index >= 0)
        g_editSelected = f.index;
}

void editFingerMove(Finger &f, float dx, float dy)
{
    if (f.role != FR_EDIT)
        return;

    Finger *pinch = nullptr, *drag = nullptr;
    for (auto &o : g_fingers)
    {
        if (!o.active || o.role != FR_EDIT)
            continue;
        if (o.index == -2)
            pinch = &o;
        else if (o.index >= 0)
            drag = &o;
    }

    if (pinch && drag && g_pinchStartDist > 1.f)
    {
        float const d = sqrtf(dist2(drag->lastX, drag->lastY, pinch->lastX, pinch->lastY));
        float const r = g_pinchStartR * d / g_pinchStartDist;
        if (drag->index == kStickItem)
            g_stick.r = clamp(r, 0.04f, 0.3f);
        else
            g_buttons[drag->index].r = clamp(r, 0.02f, 0.25f);
        return;
    }

    if (f.index >= 0)
        editMoveItem(f.index, dx, dy);
}

void editFingerUp(Finger &f)
{
    if (f.role != FR_UI)
        return;

    Widget w[16];
    int count;
    buildEditWidgets(w, &count, 16);

    if (f.index >= 0 && f.index < count && hitWidget(w, count, f.lastX, f.lastY) == f.index)
        doWidgetAction(w[f.index].action);
}

void renderEdit(void)
{
    fillRect({ 0, 0, (float)g_outW, (float)g_outH }, rgba(0, 0, 0, 0.45f));

    float const scale = textScale(0.02f);
    drawText("DRAG TO MOVE - PINCH OR -/+ TO RESIZE - HIDDEN BUTTONS: TOUCH SETTINGS > BUTTONS",
             g_outW * 0.5f, g_outH * 0.36f, max(1.f, scale - 1.f), rgba(235, 225, 210, 1.f));

    // stick
    {
        float const r = stickRadiusPx(), x = itemPX(g_stick.x), y = itemPY(g_stick.y);
        fillCircle(x, y, r, rgba(20, 20, 20, 0.6f));
        ringCircle(x, y, r, max(3.f, r * 0.05f), g_editSelected == kStickItem ? rgba(255, 200, 60, 1.f) : rgba(200, 30, 30, 1.f));
        drawLabelInCircle(g_settings.floatingStick ? "STICK*" : "STICK", x, y, r, rgba(235, 225, 210, 1.f));
    }

    for (int i = 0; i < kNumButtons; i++)
    {
        auto const &b = g_buttons[i];
        if (!b.visible)
            continue;
        float const x = itemPX(b.x), y = itemPY(b.y), r = buttonRadiusPx(b);
        drawRoundButton(b.label, x, y, r, false, 0.9f);
        if (i == g_editSelected)
            ringCircle(x, y, r * 1.12f, max(3.f, r * 0.06f), rgba(255, 200, 60, 1.f));
    }

    Widget w[16];
    int count;
    buildEditWidgets(w, &count, 16);
    drawWidgets(w, count);

    if (g_settings.floatingStick)
        drawText("* FLOATING STICK: THIS IS THE RESTING SPOT, IT FOLLOWS YOUR THUMB", g_outW * 0.5f, g_outH * 0.52f,
                 max(1.f, scale - 1.f), rgba(180, 170, 160, 1.f));
}

//
// dispatch
//

Finger *findFinger(SDL_FingerID id)
{
    for (auto &f : g_fingers)
        if (f.active && f.id == id)
            return &f;
    return nullptr;
}

Finger *allocFinger(SDL_FingerID id)
{
    for (auto &f : g_fingers)
    {
        if (!f.active)
        {
            f = Finger {};
            f.id = id;
            f.active = true;
            return &f;
        }
    }
    return nullptr;
}

void syncMode(void)
{
    if (g_mode == TM_SETTINGS || g_mode == TM_EDIT)
    {
        // settings are only reachable from menus; bail out if the menu went away under us
        if (currentGameMode() == TM_GAME)
        {
            saveConfig();
            g_mode = TM_GAME;
        }
    }
    else
        g_mode = currentGameMode();

    if (g_mode != g_lastMode)
    {
        releaseGameInput();
        g_lastMode = g_mode;
    }
}

} // namespace

//
// public interface
//

bool touch_handleEvent(SDL_Event const *ev)
{
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return false;

    initialize();

    if (!g_outW || !g_outH)
        return true;

    syncMode();

    float const px = ev->tfinger.x * g_outW, py = ev->tfinger.y * g_outH;

    switch (ev->type)
    {
        case SDL_FINGERDOWN:
        {
            g_hiddenByHardware = false;

            Finger *f = allocFinger(ev->tfinger.fingerId);
            if (!f)
                break;

            f->startX = f->lastX = px;
            f->startY = f->lastY = py;
            f->startTime = SDL_GetTicks();

            switch (g_mode)
            {
                case TM_GAME:     gameFingerDown(*f); break;
                case TM_MENU:     menuFingerDown(*f); break;
                case TM_SETTINGS: settingsFingerDown(*f); break;
                case TM_EDIT:     editFingerDown(*f); break;
            }
            break;
        }
        case SDL_FINGERMOTION:
        {
            Finger *f = findFinger(ev->tfinger.fingerId);
            if (!f)
                break;

            float const dx = px - f->lastX, dy = py - f->lastY;
            f->lastX = px;
            f->lastY = py;

            float const moveSlop = g_outH * 0.015f;
            if (dist2(px, py, f->startX, f->startY) > moveSlop * moveSlop)
                f->moved = true;

            switch (g_mode)
            {
                case TM_GAME:     gameFingerMove(*f, dx, dy); break;
                case TM_MENU:     menuFingerMove(*f); break;
                case TM_SETTINGS: settingsFingerMove(*f); break;
                case TM_EDIT:     editFingerMove(*f, dx, dy); break;
            }
            break;
        }
        case SDL_FINGERUP:
        {
            Finger *f = findFinger(ev->tfinger.fingerId);
            if (!f)
                break;

            f->lastX = px;
            f->lastY = py;

            switch (g_mode)
            {
                case TM_GAME:     gameFingerUp(*f); break;
                case TM_MENU:     menuFingerUp(*f); break;
                case TM_SETTINGS: settingsFingerUp(*f); break;
                case TM_EDIT:     editFingerUp(*f); break;
            }

            f->active = false;
            break;
        }
    }

    return true;
}

void touch_notifyHardwareInput(void)
{
    if (g_settings.autoHide)
        g_hiddenByHardware = true;
}

void touch_updateGameFunctions(int32_t *flags, int32_t numFlags)
{
    if (!g_initialized || g_mode != TM_GAME)
        return;

    for (auto &b : g_buttons)
    {
        if (b.kind != BK_FUNC || (unsigned)b.code >= (unsigned)numFlags)
            continue;

        if (b.held)
        {
            flags[b.code] = 1;
            b.seen++;
        }
        else if (b.pulse > 0)
        {
            flags[b.code] = 1;
            b.pulse--;
        }
    }

    if (g_weaponPulse > 0 && (unsigned)g_weaponPulseFunc < (unsigned)numFlags)
    {
        flags[g_weaponPulseFunc] = 1;
        if (--g_weaponPulse == 0)
            g_weaponPulseFunc = -1;
    }
}

void touch_getInput(TouchInput *pInput)
{
    *pInput = {};

    if (!g_initialized || g_mode != TM_GAME || !g_outH)
        return;

    // stick with radial dead zone, rescaled so movement starts smoothly at the edge of it
    float const len = sqrtf(g_stickDX * g_stickDX + g_stickDY * g_stickDY);
    float const dz = g_settings.deadZone;

    if (g_stickActive && len > dz && len > 0.f)
    {
        float const scaled = min((len - dz) / max(1.f - dz, 0.01f), 1.f);
        pInput->strafe = g_stickDX / len * scaled;
        pInput->forward = -g_stickDY / len * scaled;
    }

    // swiping one screen height turns 180 degrees at speed 1.0
    float const nx = g_lookDX / g_outH, ny = g_lookDY / g_outH;
    pInput->yaw = nx * 180.f * g_settings.lookSensX;
    pInput->pitch = -ny * 600.f * g_settings.lookSensY * (g_settings.invertY ? -1.f : 1.f);
    pInput->run = g_settings.alwaysRun;

    g_lookDX = g_lookDY = 0.f;
}

void touch_render(SDL_Renderer *renderer, SDL_Rect const *gameRect)
{
    initialize();

    if (renderer != g_renderer)
    {
        if (g_fontTexture)
            SDL_DestroyTexture(g_fontTexture);
        g_fontTexture = nullptr;
        g_renderer = renderer;
    }

    SDL_GetRendererOutputSize(renderer, &g_outW, &g_outH);
    g_gameRect = *gameRect;
    g_frame++;

    syncMode();

    // deferred menu click release (see menuFingerUp)
    if (g_menuReleasePending && g_frame != g_menuPressFrame)
    {
        g_mouseClickState = MOUSE_RELEASED;
        g_menuReleasePending = false;
    }

    if (g_tapKeyRelease >= 0)
    {
        releaseKey(g_tapKeyRelease);
        g_tapKeyRelease = -1;
    }

    // chat in multiplayer: bring up the on-screen keyboard
    static bool typingKeyboard;
    bool const typing = gGameStarted && gInputMode == INPUT_MODE_2;
    if (typing && !typingKeyboard && !SDL_IsTextInputActive())
        SDL_StartTextInput();
    else if (!typing && typingKeyboard && SDL_IsTextInputActive())
        SDL_StopTextInput();
    typingKeyboard = typing;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    switch (g_mode)
    {
        case TM_GAME:     renderGame(); break;
        case TM_MENU:     renderMenu(); break;
        case TM_SETTINGS: renderSettings(); break;
        case TM_EDIT:     renderEdit(); break;
    }
}

#endif // EDUKE32_IOS
