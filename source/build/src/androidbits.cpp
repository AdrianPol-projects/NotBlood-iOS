// Android platform glue for NotBlood (iosbits.h for Android, see NOTBLOOD_ANDROID in compat.h)
//
// The Android port runs the iPad code: the engine renders through gl4es on OpenGL ES 2.0 and the
// touch controls are the same. The game folder is NotBlood in the main storage
// (/storage/emulated/0/NotBlood, next to Download and Documents), which needs the "all files"
// permission from Android 11 on; without it the app's own folder in Android/data is used.

#ifdef NOTBLOOD_ANDROID

#include <jni.h>
#include <cstdarg>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unwind.h>

#include "compat.h"
#include "baselayer.h"
#include "iosbits.h"
#include "SDL.h"
#include "SDL_system.h"

#ifdef USE_OPENGL
extern "C" {
void set_getprocaddress(void *(*new_proc_address)(const char *));
void set_getmainfbsize(void (*new_getMainFBSize)(int *width, int *height));
void initialize_gl4es(void);
void *gl4es_GetProcAddress(const char *name);
}
#endif

extern "C" int eduke32_android_main(int argc, char *argv[]);

static char const kSharedFolder[] = "/storage/emulated/0/NotBlood";
static char const kReadmeName[] = "PUT BLOOD FILES HERE.txt";
static char const kReadmeText[] =
    "NotBlood for Android\n"
    "====================\n\n"
    "Copy your Blood game files into this folder (NotBlood in the main storage, next to Download).\n\n"
    "Required (from GOG/Steam 'One Unit Whole Blood', Fresh Supply, or the original CD):\n"
    "  BLOOD.RFF  GUI.RFF  SOUNDS.RFF  SURFACE.DAT  TABLES.DAT  VOXEL.DAT  BLOOD.INI\n"
    "  TILES000.ART ... TILES017.ART\n\n"
    "Optional:\n"
    "  movie/   the MOVIE folder of the Blood CD for the cutscenes\n"
    "  addons/  add-on episodes (addons/cryptic for Cryptic Passage), mods/ for mods\n"
    "  blood02.ogg ... the CD soundtrack\n\n"
    "Settings, saves and touchcontrols.cfg are kept in this folder too.\n";

static char s_gameDir[BMAX_PATH];
static char s_appDir[BMAX_PATH];

// ---- Java side (NotBloodActivity) -----------------------------------------------------------

static bool callStatic(const char *pzMethod, const char *pzSig, ...)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity)
        return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = env->GetStaticMethodID(cls, pzMethod, pzSig);
    bool bOk = false;
    if (mid)
    {
        va_list va;
        va_start(va, pzSig);
        env->CallStaticVoidMethodV(cls, mid, va);
        va_end(va);
        bOk = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return bOk;
}

void android_restartapp(void)
{
    callStatic("restartApp", "()V");
}

// ---- folders --------------------------------------------------------------------------------

char *ios_getappdir(void)
{
    return Xstrdup(s_appDir);
}

char *ios_getdocumentsdir(void)
{
    return Xstrdup(s_gameDir);
}

static bool canUseFolder(const char *pzPath)
{
    mkdir(pzPath, 0775);
    return access(pzPath, R_OK | W_OK | X_OK) == 0;
}

// notblood.pk3 ships in the APK's assets: keep a copy where the engine can open it
static void extractBuiltinFiles(void)
{
    static char const *const files[] = { "notblood.pk3" };
    for (char const *pzName : files)
    {
        SDL_RWops *in = SDL_RWFromFile(pzName, "rb");  // reads the APK's assets
        if (!in)
            continue;
        Sint64 const nSize = SDL_RWsize(in);
        char path[BMAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", s_appDir, pzName);
        struct stat st;
        if (!stat(path, &st) && st.st_size == nSize)
        {
            SDL_RWclose(in);
            continue;
        }
        if (FILE *fp = fopen(path, "wb"))
        {
            char buf[65536];
            size_t n;
            while ((n = SDL_RWread(in, buf, 1, sizeof(buf))) > 0)
                fwrite(buf, 1, n, fp);
            fclose(fp);
        }
        SDL_RWclose(in);
    }
}

void ios_setupdocuments(void)
{
    if (s_gameDir[0])
    {
        chdir(s_gameDir);
        return;
    }

    const char *pzInternal = SDL_AndroidGetInternalStoragePath();
    snprintf(s_appDir, sizeof(s_appDir), "%s", pzInternal ? pzInternal : ".");
    extractBuiltinFiles();

    // Android 10 and older: the storage permission; 11 and newer: "all files access", granted in
    // the system settings (the activity opens them)
    if (SDL_GetAndroidSDKVersion() < 30)
        SDL_AndroidRequestPermission("android.permission.WRITE_EXTERNAL_STORAGE");

    while (!canUseFolder(kSharedFolder))
    {
        static SDL_MessageBoxButtonData const buttons[] =
        {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Allow" },
            { 0, 2, "Check again" },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Use the app folder" },
        };
        SDL_MessageBoxData const data =
        {
            SDL_MESSAGEBOX_INFORMATION, NULL, "Access to the NotBlood folder",
            "NotBlood keeps the Blood files, settings and saves in the folder NotBlood in your main "
            "storage (next to Download), so you can copy files there with any file manager.\n\n"
            "For that Android needs to allow NotBlood \"access to all files\". Tap Allow, switch it on, "
            "come back and tap Check again.",
            3, buttons, NULL
        };
        int choice = 0;
        if (SDL_ShowMessageBox(&data, &choice) < 0 || choice == 0)
            break;
        if (choice == 1)
        {
            if (SDL_GetAndroidSDKVersion() >= 30)
                callStatic("requestAllFilesAccess", "()V");
            else
                SDL_AndroidRequestPermission("android.permission.WRITE_EXTERNAL_STORAGE");
        }
    }

    if (canUseFolder(kSharedFolder))
        snprintf(s_gameDir, sizeof(s_gameDir), "%s", kSharedFolder);
    else
    {
        // Android/data/<app>/files: reachable over USB from a PC, not from most file managers
        const char *pzExternal = SDL_AndroidGetExternalStoragePath();
        snprintf(s_gameDir, sizeof(s_gameDir), "%s", pzExternal ? pzExternal : s_appDir);
    }
    chdir(s_gameDir);

    if (access(kReadmeName, F_OK) != 0)
    {
        if (FILE *fp = fopen(kReadmeName, "w"))
        {
            fputs(kReadmeText, fp);
            fclose(fp);
        }
    }
}

static bool haveGameData(void)
{
    return access("BLOOD.RFF", F_OK) == 0 || access("blood.rff", F_OK) == 0 || access("Blood.rff", F_OK) == 0;
}

void ios_checkgamedata(void)
{
    while (!haveGameData())
    {
        static SDL_MessageBoxButtonData const buttons[] =
        {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Check again" },
            { 0, 0, "Start anyway" },
        };
        char text[640];
        snprintf(text, sizeof(text),
                 "BLOOD.RFF was not found.\n\nCopy everything from your Blood folder (GOG/Steam One Unit "
                 "Whole Blood, Fresh Supply or CD) into\n\n%s\n\nThen come back here and tap \"Check again\".",
                 s_gameDir);
        SDL_MessageBoxData const data = { SDL_MESSAGEBOX_INFORMATION, NULL, "Blood game files needed", text, 2, buttons, NULL };
        int choice = 0;
        if (SDL_ShowMessageBox(&data, &choice) < 0 || choice != 1)
            break;
    }
}

// ---- screen ---------------------------------------------------------------------------------

void ios_getscreensize(int32_t *pixelw, int32_t *pixelh, int32_t *pointw, int32_t *pointh)
{
    SDL_DisplayMode mode;
    int w = 1920, h = 1080;
    if (!SDL_GetDesktopDisplayMode(0, &mode) && mode.w > 0 && mode.h > 0)
    {
        w = mode.w;
        h = mode.h;
    }
    // landscape only: the long side is the width
    if (pixelw) *pixelw = max(w, h);
    if (pixelh) *pixelh = min(w, h);
    if (pointw) *pointw = max(w, h);
    if (pointh) *pointh = min(w, h);
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

// fewer pixels than the screen: the surface gets a fixed size and Android's hardware scaler
// stretches it to the screen
void ios_setrenderscale(SDL_Window *window, int pct)
{
    UNREFERENCED_PARAMETER(window);
    static int s_applied = 100;
    pct = clamp(pct, 25, 100);
    if (pct == s_applied)
        return;
    int32_t w = 0, h = 0;
    if (pct < 100)
        ios_scaledsize(pct, &w, &h);
    if (callStatic("setRenderSize", "(II)V", (jint)w, (jint)h))
        s_applied = pct;
}

// ---- OpenGL through gl4es --------------------------------------------------------------------

#ifdef USE_OPENGL
static SDL_Window *s_glWindow;

static void *android_glesGetProcAddress(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

static void android_getMainFBSize(int *width, int *height)
{
    SDL_GL_GetDrawableSize(s_glWindow, width, height);
}

extern "C" int gl4es_info_npot(void);
extern "C" int gl4es_info_highp(void);

// Palette-indexed textures (one copy of each tile in big sheets, palette and shading in the shader:
// the fast path) need full NPOT support, or gl4es garbles the tiles it uploads into the sheets, and
// highp floats in fragment shaders to address single texels of a 4096 sheet. Without them the
// renderer makes a texture per tile, palette and shade.
bool android_glindexedcolors(void)
{
    return gl4es_info_npot() >= 3 && gl4es_info_highp() > 0;
}

// what the phone's own OpenGL ES driver says (gl4es reports itself), for bug reports
static void logGLESDriver(void)
{
    typedef unsigned char const *(*getString_t)(unsigned);
    typedef void (*getPrecision_t)(unsigned, unsigned, int *, int *);
    auto const getString = (getString_t)SDL_GL_GetProcAddress("glGetString");
    auto const getPrecision = (getPrecision_t)SDL_GL_GetProcAddress("glGetShaderPrecisionFormat");
    if (!getString)
        return;
    LOG_F(INFO, "GLES driver: %s / %s / %s / GLSL %s", (char const *)getString(0x1F00 /*GL_VENDOR*/),
          (char const *)getString(0x1F01 /*GL_RENDERER*/), (char const *)getString(0x1F02 /*GL_VERSION*/),
          (char const *)getString(0x8B8C /*GL_SHADING_LANGUAGE_VERSION*/));
    if (getPrecision)
    {
        int high[2] = {}, highBits = 0, medium[2] = {}, mediumBits = 0;
        getPrecision(0x8B30 /*GL_FRAGMENT_SHADER*/, 0x8DF2 /*GL_HIGH_FLOAT*/, high, &highBits);
        getPrecision(0x8B30 /*GL_FRAGMENT_SHADER*/, 0x8DF1 /*GL_MEDIUM_FLOAT*/, medium, &mediumBits);
        LOG_F(INFO, "GLES fragment shader floats: highp %d bits (range %d), mediump %d bits (range %d)",
              highBits, high[1], mediumBits, medium[1]);
    }
    LOG_F(INFO, "GLES extensions: %s", (char const *)getString(0x1F03 /*GL_EXTENSIONS*/));
}

int ios_initgl4es(SDL_Window *window)
{
    s_glWindow = window;
    static bool initialized;
    if (!initialized)
    {
        logGLESDriver();
        // same as on iOS: client arrays instead of one constantly rewritten VBO
        setenv("LIBGL_USEVBO", "0", 1);
        set_getprocaddress(android_glesGetProcAddress);
        set_getmainfbsize(android_getMainFBSize);
        initialize_gl4es();
        initialized = true;
        LOG_F(INFO, "gl4es: NPOT support %d, highp in fragment shaders %d: %s textures", gl4es_info_npot(), gl4es_info_highp(),
              android_glindexedcolors() ? "palette-indexed" : "true-color");
    }
    return 0;
}

void *ios_glGetProcAddress(const char *name)
{
    return gl4es_GetProcAddress(name);
}
#endif

// ---- GL crash guard (same as iOS) ------------------------------------------------------------

static char const kNoGLFile[] = "DISABLE_OPENGL.txt";
static char const kGLStartingFile[] = ".opengl_starting";

// .opengl_starting counts the starts with OpenGL that didn't get through the first seconds (it is
// removed once they did, when the game runs the classic renderer, and when it goes to the
// background). One such start can be bad luck; after two in a row the next start uses the classic
// renderer.
bool ios_glsafemode(void)
{
    if (access(kNoGLFile, F_OK) == 0)
        return true;

    int nFailed = 0;
    if (FILE *fp = fopen(kGLStartingFile, "r"))
    {
        if (fscanf(fp, "%d", &nFailed) != 1 || nFailed < 1)
            nFailed = 1; // empty marker of an older version
        fclose(fp);
    }

    if (nFailed >= 2)
    {
        unlink(kGLStartingFile);
        if (FILE *fp = fopen(kNoGLFile, "w"))
        {
            fputs("NotBlood crashed twice in a row while starting with its OpenGL renderer, so it now uses the\n"
                  "classic renderer. Delete this file to try OpenGL again.\n", fp);
            fclose(fp);
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OpenGL renderer disabled",
                                 "NotBlood crashed twice in a row while starting with its OpenGL renderer, so it will "
                                 "use the classic renderer from now on.\n\nTo try OpenGL again, delete DISABLE_OPENGL.txt "
                                 "in the NotBlood folder. If it keeps crashing, please send notblood.log from that folder.", NULL);
        return true;
    }

    if (FILE *fp = fopen(kGLStartingFile, "w"))
    {
        fprintf(fp, "%d\n", nFailed + 1);
        fclose(fp);
    }
    if (nFailed)
        LOG_F(WARNING, "The last start with OpenGL ended early; trying OpenGL again.");
    return false;
}

void ios_glstartupok(void)
{
    unlink(kGLStartingFile);
}

// ---- crash report ---------------------------------------------------------------------------
//
// Android writes crash details where users can't get at them, so a crash also appends the signal
// and a backtrace (library + offset, symbolized with the unstripped libmain.so that CI keeps) to
// notblood.log. Only async-signal-safe calls, mostly; then the default handler takes over.

static int s_crashFd = -1;
static int const kCrashSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
static struct sigaction s_oldCrashActions[ARRAY_SIZE(kCrashSignals)];

static void crashPuts(char const *s)
{
    if (s_crashFd >= 0 && s)
        write(s_crashFd, s, strlen(s));
}

static void crashPutHex(uintptr_t v)
{
    char buf[2 + 16 + 1];
    char *p = buf + sizeof(buf) - 1;
    *p = 0;
    do
        *--p = "0123456789abcdef"[v & 15];
    while ((v >>= 4) && p > buf + 2);
    *--p = 'x';
    *--p = '0';
    crashPuts(p);
}

static void crashPutDec(int v)
{
    char buf[16];
    char *p = buf + sizeof(buf) - 1;
    *p = 0;
    bool const neg = v < 0;
    unsigned u = neg ? -(unsigned)v : (unsigned)v;
    do
        *--p = '0' + u % 10;
    while ((u /= 10) && p > buf + 1);
    if (neg)
        *--p = '-';
    crashPuts(p);
}

static void crashPutFrame(int n, uintptr_t pc)
{
    crashPuts("  #");
    crashPutDec(n);
    crashPuts(" ");
    crashPutHex(pc);
    Dl_info info;
    if (dladdr((void *)pc, &info) && info.dli_fname)
    {
        char const *name = strrchr(info.dli_fname, '/');
        crashPuts(" ");
        crashPuts(name ? name + 1 : info.dli_fname);
        crashPuts(" +");
        crashPutHex(pc - (uintptr_t)info.dli_fbase);
        if (info.dli_sname)
        {
            crashPuts(" (");
            crashPuts(info.dli_sname);
            crashPuts(")");
        }
    }
    crashPuts("\n");
}

struct CrashUnwind
{
    int count;
};

static _Unwind_Reason_Code crashUnwindFrame(struct _Unwind_Context *ctx, void *arg)
{
    auto *state = (CrashUnwind *)arg;
    uintptr_t const pc = _Unwind_GetIP(ctx);
    if (pc)
        crashPutFrame(state->count++, pc);
    return state->count < 40 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void crashHandler(int sig, siginfo_t *info, void *ucontext)
{
    static volatile sig_atomic_t s_inHandler;
    if (!s_inHandler)
    {
        s_inHandler = 1;
        crashPuts("\n*** NotBlood crashed: signal ");
        crashPutDec(sig);
        crashPuts(sig == SIGSEGV ? " (SIGSEGV)" : sig == SIGBUS ? " (SIGBUS)" : sig == SIGFPE ? " (SIGFPE)" : sig == SIGILL ? " (SIGILL)" : " (SIGABRT)");
        crashPuts(" code ");
        crashPutDec(info ? info->si_code : 0);
        crashPuts(" address ");
        crashPutHex(info ? (uintptr_t)info->si_addr : 0);
        crashPuts("\n");

        if (auto *uc = (ucontext_t *)ucontext)
        {
#if defined __aarch64__
            crashPuts("crashed at:\n");
            crashPutFrame(0, (uintptr_t)uc->uc_mcontext.pc);
            crashPuts("called from:\n");
            crashPutFrame(1, (uintptr_t)uc->uc_mcontext.regs[30]);
#elif defined __x86_64__
            crashPuts("crashed at:\n");
            crashPutFrame(0, (uintptr_t)uc->uc_mcontext.gregs[REG_RIP]);
#endif
        }

        crashPuts("backtrace:\n");
        CrashUnwind state = { 0 };
        _Unwind_Backtrace(crashUnwindFrame, &state);
        crashPuts("*** end of crash report\n");
    }

    // let Android's handler finish the job (crash dialog, tombstone)
    for (size_t i = 0; i < ARRAY_SIZE(kCrashSignals); i++)
        if (kCrashSignals[i] == sig)
            sigaction(sig, &s_oldCrashActions[i], NULL);
    raise(sig);
}

void android_installcrashhandler(char const *pzLogFile)
{
    s_crashFd = open(pzLogFile, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0664);
    if (s_crashFd < 0)
        return;

    static char altStack[64 * 1024];
    stack_t ss = {};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, NULL);

    struct sigaction sa = {};
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < ARRAY_SIZE(kCrashSignals); i++)
        sigaction(kCrashSignals[i], &sa, &s_oldCrashActions[i]);
}

// ---- entry point ----------------------------------------------------------------------------

extern "C" int SDL_main(int argc, char *argv[])
{
    // everything (game data, config, saves) lives in the game folder
    char **newargv = (char **)calloc(argc + 2, sizeof(char *));
    int newargc = 0;
    newargv[newargc++] = argc > 0 ? argv[0] : (char *)"notblood";
    newargv[newargc++] = (char *)"-usecwd";
    for (int i = 1; i < argc; i++)
        newargv[newargc++] = argv[i];
    newargv[newargc] = NULL;
    return eduke32_android_main(newargc, newargv);
}

#endif // NOTBLOOD_ANDROID
