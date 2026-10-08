// iOS/iPadOS platform glue for NotBlood

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#include "compat.h"
#include "iosbits.h"

#include <unistd.h>

static const char kReadmeName[] = "PUT BLOOD FILES HERE.txt";
static const char kReadmeText[] =
    "NotBlood for iPad\n"
    "=================\n\n"
    "Copy your Blood game files into this folder (On My iPad > NotBlood) using the Files app.\n\n"
    "Required (from GOG/Steam 'One Unit Whole Blood', Fresh Supply, or the original CD):\n"
    "  BLOOD.RFF  GUI.RFF  SOUNDS.RFF  SURFACE.DAT  TILES000.ART ... TILES017.ART\n"
    "  VOXEL.DAT  BLOOD.INI  (and the other *.DAT / *.ART files from the game folder)\n\n"
    "Optional:\n"
    "  CRYPTIC.INI + CP*.MAP + CPART*.ART + CPSEQ.RFF ... for Cryptic Passage\n"
    "  CD audio tracks (blood02.ogg ... or Track02.ogg etc.) for the CD soundtrack\n"
    "  movie/ folder with the *.SMK cutscenes\n\n"
    "Simplest approach: copy EVERYTHING from your Blood install folder in here.\n"
    "Config, savegames and touchcontrols.cfg are also stored in this folder.\n";

char *ios_getappdir(void)
{
    @autoreleasepool
    {
        NSString *path = [[NSBundle mainBundle] resourcePath];
        return path ? Xstrdup([path fileSystemRepresentation]) : NULL;
    }
}

char *ios_getdocumentsdir(void)
{
    @autoreleasepool
    {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if ([paths count] == 0)
            return NULL;
        return Xstrdup([[paths objectAtIndex:0] fileSystemRepresentation]);
    }
}

void ios_getscreensize(int32_t *pixelw, int32_t *pixelh, int32_t *pointw, int32_t *pointh)
{
    @autoreleasepool
    {
        UIScreen *screen = [UIScreen mainScreen];
        CGRect const native = [screen nativeBounds];
        CGRect const points = [screen bounds];

        // Game is landscape-only: report the long side as width.
        int32_t const nw = (int32_t)native.size.width, nh = (int32_t)native.size.height;
        int32_t const pw = (int32_t)points.size.width, ph = (int32_t)points.size.height;

        if (pixelw) *pixelw = max(nw, nh);
        if (pixelh) *pixelh = min(nw, nh);
        if (pointw) *pointw = max(pw, ph);
        if (pointh) *pointh = min(pw, ph);
    }
}

static bool ios_havegamedata(void)
{
    return access("BLOOD.RFF", F_OK) == 0 || access("blood.rff", F_OK) == 0 || access("Blood.rff", F_OK) == 0;
}

void ios_checkgamedata(void)
{
    while (!ios_havegamedata())
    {
        static SDL_MessageBoxButtonData const buttons[] =
        {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Check again" },
            { 0, 0, "Start anyway" },
        };

        SDL_MessageBoxData const data =
        {
            SDL_MESSAGEBOX_INFORMATION,
            NULL,
            "Blood game files needed",
            "BLOOD.RFF was not found.\n\n"
            "Open the Files app, go to On My iPad > NotBlood and copy everything from your "
            "Blood folder (GOG/Steam One Unit Whole Blood, Fresh Supply or CD) into it.\n\n"
            "Then come back here and tap \"Check again\".",
            2,
            buttons,
            NULL
        };

        int choice = 0;
        if (SDL_ShowMessageBox(&data, &choice) < 0 || choice != 1)
            break;
    }
}

// Must also run after SDL's app delegate has started: it changes the working
// directory to the (read-only) app bundle in didFinishLaunchingWithOptions.
void ios_setupdocuments(void)
{
    // Runs before the engine allocator exists, so stick to Foundation/libc here.
    @autoreleasepool
    {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if ([paths count] == 0)
            return;
        chdir([[paths objectAtIndex:0] fileSystemRepresentation]);
    }

    // Drop a readme so the folder shows up in Files right away and explains itself.
    if (access(kReadmeName, F_OK) != 0)
    {
        if (FILE *fp = fopen(kReadmeName, "w"))
        {
            fputs(kReadmeText, fp);
            fclose(fp);
        }
    }
}

int main(int argc, char *argv[])
{
    @autoreleasepool
    {
        // Play audio like a game (not silenced by the ring/silent switch, mixes cleanly after interruptions).
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
    }

    ios_setupdocuments();

    // Everything (game data, config, saves) lives in the Documents folder exposed in the Files app.
    char **newargv = (char **)calloc(argc + 2, sizeof(char *));
    int newargc = 0;

    newargv[newargc++] = argv[0];
    newargv[newargc++] = (char *)"-usecwd";

    for (int i = 1; i < argc; i++)
        newargv[newargc++] = argv[i];

    newargv[newargc] = NULL;

    return SDL_UIKitRunApp(newargc, newargv, eduke32_ios_main);
}
