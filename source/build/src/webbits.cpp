// Web browser platform glue for NotBlood (iosbits.h for the WebAssembly build, see NOTBLOOD_WEB in
// compat.h and platform/Web)
//
// The web port runs the iPad code: touch controls, gl4es (here on WebGL) for the OpenGL renderer.
// The game folder is /notblood, an IndexedDB file system the page (platform/Web/shell) mounts and
// fills with the player's Blood files before the game starts; notblood.pk3 is in /app.
//
// The engine's loops never return, so the program is built with ASYNCIFY and web_yield() hands
// control back to the browser once a frame (to draw it and deliver input, sound and network
// events) and regularly while a loop only waits.

#ifdef NOTBLOOD_WEB

#include <emscripten.h>
#include <emscripten/html5.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

#include "compat.h"
#include "baselayer.h"
#include "iosbits.h"
#include "webbits.h"
#include "SDL.h"

#ifdef USE_OPENGL
extern "C" {
void set_getprocaddress(void *(*new_proc_address)(const char *));
void set_getmainfbsize(void (*new_getMainFBSize)(int *width, int *height));
void initialize_gl4es(void);
void *gl4es_GetProcAddress(const char *name);
}
#endif

static char const kGameDir[] = "/notblood";
static char const kAppDir[] = "/app";

// ---- giving control back to the browser ------------------------------------------------------

// waits for the browser's next animation frame (the screen refresh)
EM_ASYNC_JS(void, web_waitAnimationFrame, (), {
    await new Promise(function(resolve) {
        if (document.hidden)
            setTimeout(resolve, 16);  // no animation frames in a hidden tab
        else
            requestAnimationFrame(function() { resolve(); });
    });
});

static double s_lastYield;

void web_yield(int bFrame)
{
    double const now = emscripten_get_now();
    if (bFrame)
    {
        web_waitAnimationFrame();
        s_lastYield = emscripten_get_now();
    }
    else if (now - s_lastYield > 12)
    {
        // a loop that only waits (timers, menus without a new frame, network): let the browser run
        emscripten_sleep(0);
        s_lastYield = emscripten_get_now();
    }
}

EM_JS(void, web_syncFiles, (), {
    if (Module.notbloodSync)
        Module.notbloodSync();
});

void web_filesChanged(void)
{
    web_syncFiles();
}

EM_JS(int, web_hastouch, (), {
    return (navigator.maxTouchPoints || 0) > 0 || ('ontouchstart' in window) ? 1 : 0;
});

// ---- folders --------------------------------------------------------------------------------

char *ios_getappdir(void)
{
    return Xstrdup(kAppDir);
}

char *ios_getdocumentsdir(void)
{
    return Xstrdup(kGameDir);
}

void ios_setupdocuments(void)
{
    mkdir(kGameDir, 0775);
    chdir(kGameDir);
}

void ios_checkgamedata(void)
{
    // the page only starts the game once BLOOD.RFF is there
    if (access("BLOOD.RFF", F_OK) != 0 && access("blood.rff", F_OK) != 0)
        LOG_F(WARNING, "BLOOD.RFF is missing from %s", kGameDir);
}

// ---- screen ---------------------------------------------------------------------------------

EM_JS(int, web_screenWidth, (), {
    var c = Module.canvas;
    var w = (c && c.clientWidth) || window.innerWidth;
    return Math.round(w * (window.devicePixelRatio || 1));
});

EM_JS(int, web_screenHeight, (), {
    var c = Module.canvas;
    var h = (c && c.clientHeight) || window.innerHeight;
    return Math.round(h * (window.devicePixelRatio || 1));
});

void ios_getscreensize(int32_t *pixelw, int32_t *pixelh, int32_t *pointw, int32_t *pointh)
{
    int w = max(web_screenWidth(), 320), h = max(web_screenHeight(), 200);
    double const ratio = max(emscripten_get_device_pixel_ratio(), 1.0);
    if (pixelw) *pixelw = w;
    if (pixelh) *pixelh = h;
    if (pointw) *pointw = (int32_t)(w / ratio);
    if (pointh) *pointh = (int32_t)(h / ratio);
}

int const ios_renderscales[kIosRenderScales] = { 100, 90, 80, 75, 66, 60, 50, 40, 33, 25 };

void ios_scaledsize(int pct, int32_t *w, int32_t *h)
{
    int32_t fw, fh;
    ios_getscreensize(&fw, &fh, NULL, NULL);
    pct = clamp(pct, 25, 100);
    *w = (fw * pct / 100) & ~1;
    *h = (fh * pct / 100) & ~1;
}

int ios_scaleforsize(int32_t w)
{
    for (int const pct : ios_renderscales)
    {
        int32_t sw, sh;
        ios_scaledsize(pct, &sw, &sh);
        if (abs(sw - w) <= 2)
            return pct;
    }
    return 100;
}

// the canvas keeps its size on the page (CSS); a lower render scale only gives it fewer pixels
void ios_setrenderscale(SDL_Window *window, int pct)
{
    UNREFERENCED_PARAMETER(window);
    UNREFERENCED_PARAMETER(pct);
}

// ---- OpenGL through gl4es on WebGL ----------------------------------------------------------

#ifdef USE_OPENGL
static SDL_Window *s_glWindow;

// WebGL 2 handles textures of any size, but doesn't list an extension for it: gl4es would treat
// them as limited (and garble or drop the tile sheets), so its extension list gets one
typedef unsigned char const *(*getString_t)(unsigned);
static getString_t s_glGetString;

static unsigned char const *web_glGetString(unsigned name)
{
    unsigned char const *p = s_glGetString(name);
    if (name != 0x1F03 /*GL_EXTENSIONS*/ || !p)
        return p;
    static std::string ext;
    if (ext.empty())
    {
        ext = (char const *)p;
        int major = 0;
        typedef void (*getIntegerv_t)(unsigned, int *);
        if (auto getIntegerv = (getIntegerv_t)SDL_GL_GetProcAddress("glGetIntegerv"))
            getIntegerv(0x821B /*GL_MAJOR_VERSION*/, &major);
        if (major >= 3)
            ext += " GL_OES_texture_npot ";
    }
    return (unsigned char const *)ext.c_str();
}

static void *web_glesGetProcAddress(const char *name)
{
    if (!strcmp(name, "glGetString"))
    {
        s_glGetString = (getString_t)SDL_GL_GetProcAddress(name);
        return (void *)web_glGetString;
    }
    return SDL_GL_GetProcAddress(name);
}

static void web_getMainFBSize(int *width, int *height)
{
    SDL_GL_GetDrawableSize(s_glWindow, width, height);
}

extern "C" int gl4es_info_npot(void);
extern "C" int gl4es_info_highp(void);

bool android_glindexedcolors(void)
{
    return gl4es_info_npot() >= 3 && gl4es_info_highp() > 0;
}

int ios_initgl4es(SDL_Window *window)
{
    s_glWindow = window;
    static bool initialized;
    if (!initialized)
    {
        setenv("LIBGL_USEVBO", "0", 1);
        set_getprocaddress(web_glesGetProcAddress);
        set_getmainfbsize(web_getMainFBSize);
        initialize_gl4es();
        initialized = true;
        LOG_F(INFO, "gl4es on WebGL: NPOT support %d, highp in fragment shaders %d", gl4es_info_npot(), gl4es_info_highp());
    }
    return 0;
}

void *ios_glGetProcAddress(const char *name)
{
    return gl4es_GetProcAddress(name);
}
#endif

// A start with OpenGL that never got going falls back to the classic renderer next time, as on the
// iPad (the page can't catch a crash, but it can see the game never reached its first frames).
static char const kNoGLFile[] = "DISABLE_OPENGL.txt";
static char const kGLStartingFile[] = ".opengl_starting";

bool ios_glsafemode(void)
{
    if (access(kNoGLFile, F_OK) == 0)
        return true;

    int nFailed = 0;
    if (FILE *fp = fopen(kGLStartingFile, "r"))
    {
        if (fscanf(fp, "%d", &nFailed) != 1 || nFailed < 1)
            nFailed = 1;
        fclose(fp);
    }
    if (nFailed >= 2)
    {
        unlink(kGLStartingFile);
        if (FILE *fp = fopen(kNoGLFile, "w"))
        {
            fputs("NotBlood failed twice in a row while starting with its OpenGL renderer, so it now uses the\n"
                  "classic renderer. Delete this file to try OpenGL again.\n", fp);
            fclose(fp);
        }
        web_filesChanged();
        return true;
    }
    if (FILE *fp = fopen(kGLStartingFile, "w"))
    {
        fprintf(fp, "%d\n", nFailed + 1);
        fclose(fp);
    }
    web_filesChanged();
    return false;
}

void ios_glstartupok(void)
{
    unlink(kGLStartingFile);
    web_filesChanged();
}

// ---- entry point ----------------------------------------------------------------------------

extern "C" int eduke32_ios_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    // everything (game data, config, saves) lives in the game folder
    char **newargv = (char **)calloc(argc + 2, sizeof(char *));
    int newargc = 0;
    newargv[newargc++] = argc > 0 ? argv[0] : (char *)"notblood";
    newargv[newargc++] = (char *)"-usecwd";
    for (int i = 1; i < argc; i++)
        newargv[newargc++] = argv[i];
    newargv[newargc] = NULL;
    return eduke32_ios_main(newargc, newargv);
}

#endif // NOTBLOOD_WEB
