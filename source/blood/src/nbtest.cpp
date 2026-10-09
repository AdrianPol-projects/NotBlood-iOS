//-------------------------------------------------------------------------
/*
This file is part of NotBlood (iOS port).

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.
*/
//-------------------------------------------------------------------------

// Desktop test harness for the iOS OpenGL path (platform/iOS/test-harness).
//
// Built only with NOTBLOOD_GL4ES_TEST on Linux: the engine renders through gl4es on an
// OpenGL ES context that is made to look like the iPad's (Apple's extension list,
// limited NPOT, 4096 max texture size), and a small script drives the game and
// captures screenshots, so rendering problems can be reproduced without a device.
//
//   NBTEST_SCRIPT="120:shot:menu;150:key:esc;200:renderer:classic;260:shot:classic;300:quit"
//
// Actions: shot:<name>  key:<esc|enter|up|down|left|right|space>  renderer:<classic|polymost>
//          look:<up|down|center>  quit

#include "compat.h"

#ifdef NOTBLOOD_GL4ES_TEST

#include "build.h"
#include "baselayer.h"
#include "glbuild.h"
#include "keyboard.h"
#include "sdl_inc.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "osdcmds.h"
#include "globals.h"
#include "player.h"
#include "view.h"

#include <dlfcn.h>
#include <vector>
#include <string>

extern "C" {
void set_getprocaddress(void *(*new_proc_address)(const char *));
void set_getmainfbsize(void (*new_getMainFBSize)(int *width, int *height));
void initialize_gl4es(void);
void *gl4es_GetProcAddress(const char *name);
}

void onvideomodechange(int32_t newmode);

//
// GLES driver that reports what the iPad reported (see stdout.txt from the device)
//

static SDL_Window *s_window;

typedef unsigned char const *(*glGetString_t)(unsigned);
typedef void (*glGetIntegerv_t)(unsigned, int *);

static glGetString_t s_realGetString;
static glGetIntegerv_t s_realGetIntegerv;

static char const kAppleExtensions[] =
    "GL_EXT_blend_minmax GL_OES_mapbuffer GL_OES_element_index_uint GL_OES_packed_depth_stencil "
    "GL_OES_depth24 GL_OES_rgb8_rgba8 GL_OES_depth_texture GL_EXT_texture_rg GL_OES_texture_float "
    "GL_OES_texture_half_float GL_EXT_color_buffer_half_float GL_EXT_shader_texture_lod "
    "GL_OES_standard_derivatives GL_EXT_texture_filter_anisotropic GL_APPLE_texture_2D_limited_npot "
    "GL_OES_vertex_array_object GL_EXT_debug_label GL_EXT_debug_marker GL_EXT_discard_framebuffer ";

static unsigned char const *fakeGetString(unsigned name)
{
    if (name == 0x1F03 /* GL_EXTENSIONS */)
        return (unsigned char const *)kAppleExtensions;
    if (name == 0x1F00 /* GL_VENDOR */)
        return (unsigned char const *)"Apple Inc. (emulated)";
    if (name == 0x1F02 /* GL_VERSION */)
        return (unsigned char const *)"OpenGL ES 2.0 Apple (emulated)";
    return s_realGetString(name);
}

static void fakeGetIntegerv(unsigned pname, int *data)
{
    s_realGetIntegerv(pname, data);

    switch (pname)
    {
        case 0x0D33: /* GL_MAX_TEXTURE_SIZE */ *data = min(*data, 4096); break;
        case 0x8DFC: /* GL_MAX_VARYING_VECTORS */ *data = min(*data, 8); break;
        case 0x8872: /* GL_MAX_TEXTURE_IMAGE_UNITS */ *data = min(*data, 8); break;
        case 0x8869: /* GL_MAX_VERTEX_ATTRIBS */ *data = min(*data, 16); break;
    }
}

static void *glesProcAddress(char const *name)
{
    if (!strcmp(name, "glGetString"))
    {
        s_realGetString = (glGetString_t)SDL_GL_GetProcAddress(name);
        return (void *)fakeGetString;
    }
    if (!strcmp(name, "glGetIntegerv"))
    {
        s_realGetIntegerv = (glGetIntegerv_t)SDL_GL_GetProcAddress(name);
        return (void *)fakeGetIntegerv;
    }
    return SDL_GL_GetProcAddress(name);
}

static void mainFBSize(int *w, int *h)
{
    SDL_GL_GetDrawableSize(s_window, w, h);
}

int nbtest_initgl4es(SDL_Window *window)
{
    s_window = window;

    static bool initialized;
    if (!initialized)
    {
        set_getprocaddress(glesProcAddress);
        set_getmainfbsize(mainFBSize);
        initialize_gl4es();
        initialized = true;
    }
    return 0;
}

void *nbtest_glGetProcAddress(const char *name)
{
    return gl4es_GetProcAddress(name);
}

//
// scripted run
//

struct Step
{
    int frame;
    std::string action, arg;
};

static std::vector<Step> s_steps;
static int s_frame;
static std::string s_pendingShot;
static int s_releaseKey = -1;

static void parseScript(void)
{
    static bool parsed;
    if (parsed)
        return;
    parsed = true;

    char const *env = getenv("NBTEST_SCRIPT");
    if (!env)
        return;

    std::string s(env), item;
    size_t pos = 0;

    while (pos <= s.size())
    {
        size_t end = s.find(';', pos);
        if (end == std::string::npos)
            end = s.size();
        item = s.substr(pos, end - pos);
        pos = end + 1;

        if (item.empty())
            continue;

        Step st;
        size_t const c1 = item.find(':'), c2 = item.find(':', c1 + 1);
        st.frame = atoi(item.substr(0, c1).c_str());
        st.action = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        st.arg = c2 == std::string::npos ? "" : item.substr(c2 + 1);
        s_steps.push_back(st);
    }
}

static int keyFromName(std::string const &n)
{
    if (n == "esc") return sc_Escape;
    if (n == "enter") return sc_Return;
    if (n == "up") return sc_UpArrow;
    if (n == "down") return sc_DownArrow;
    if (n == "left") return sc_LeftArrow;
    if (n == "right") return sc_RightArrow;
    if (n == "space") return sc_Space;
    return -1;
}

static void switchRenderer(bool polymost)
{
    int const newbpp = polymost ? 32 : 8;
    LOG_F(INFO, "nbtest: switching renderer to %s", polymost ? "polymost" : "classic");

    if (videoSetGameMode(fullscreen, xres, yres, newbpp, upscalefactor) < 0)
        LOG_F(ERROR, "nbtest: videoSetGameMode failed");
    else
        onvideomodechange(newbpp > 8);

    viewResizeView(gViewSize);
    gSetup.bpp = bpp;
}

void nbtest_frame(void)
{
    parseScript();
    s_frame++;

    if (s_releaseKey >= 0)
    {
        keySetState(s_releaseKey, 0);
        s_releaseKey = -1;
    }

    for (auto const &st : s_steps)
    {
        if (st.frame != s_frame)
            continue;

        LOG_F(INFO, "nbtest: frame %d: %s %s", s_frame, st.action.c_str(), st.arg.c_str());

        if (st.action == "shot")
            s_pendingShot = st.arg.empty() ? "shot" : st.arg;
        else if (st.action == "key")
        {
            int const sc = keyFromName(st.arg);
            if (sc >= 0)
            {
                keySetState(sc, 1);
                s_releaseKey = sc;
            }
        }
        else if (st.action == "renderer")
            switchRenderer(st.arg == "polymost");
        else if (st.action == "look")
        {
            if (gMe)
                gViewLook = st.arg == "up" ? F16(200) : st.arg == "down" ? F16(-250) : 0;
        }
        else if (st.action == "quit")
        {
            LOG_F(INFO, "nbtest: done");
            exit(0);
        }
    }
}

// called right before the GL swap: grab the back buffer if a screenshot is pending
void nbtest_beforeswap(int w, int h)
{
    if (s_pendingShot.empty())
        return;

    {
        int depthBits = -1, stencilBits = -1;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthBits);
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencilBits);
        GLint vp[4] = {};
        glGetIntegerv(GL_VIEWPORT, vp);
        LOG_F(INFO, "nbtest: rendmode %d bpp %d xdim %d ydim %d depth %d stencil %d viewport %d,%d %dx%d glError 0x%x",
              videoGetRenderMode(), bpp, xdim, ydim, depthBits, stencilBits, vp[0], vp[1], vp[2], vp[3], glGetError());
    }

    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    char name[256];
    Bsnprintf(name, sizeof(name), "nbtest_%s.ppm", s_pendingShot.c_str());

    if (FILE *fp = fopen(name, "wb"))
    {
        fprintf(fp, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--)
            for (int x = 0; x < w; x++)
                fwrite(&px[((size_t)y * w + x) * 4], 1, 3, fp);
        fclose(fp);
        LOG_F(INFO, "nbtest: wrote %s (%dx%d)", name, w, h);
    }

    s_pendingShot.clear();
}

#endif // NOTBLOOD_GL4ES_TEST
