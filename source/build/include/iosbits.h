// iOS/iPadOS platform glue for NotBlood

#ifndef iosbits_h_
#define iosbits_h_

#include "compat.h"

#ifdef EDUKE32_IOS

#include "sdl_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Real program entry is in iosbits.mm; it hands control to SDL's UIKit
// runloop, which then calls this (the engine's former main()).
int eduke32_ios_main(int argc, char *argv[]);

// Returns a heap-allocated (Xstrdup) path; caller frees.
char *ios_getappdir(void);       // read-only app bundle resources
char *ios_getdocumentsdir(void); // visible in the Files app

// chdir() to the Documents folder (shown in Files) and drop the readme there.
void ios_setupdocuments(void);

// Nags (with a recheck loop) until BLOOD.RFF shows up in the Documents folder.
void ios_checkgamedata(void);

// Landscape screen size: physical pixels and UIKit points.
void ios_getscreensize(int32_t *pixelw, int32_t *pixelh, int32_t *pointw, int32_t *pointh);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// Hooks implemented by the game-side touch layer (source/blood/src/touchcontrols.cpp).
bool touch_handleEvent(SDL_Event const *ev);                    // true if the event was consumed
void touch_render(SDL_Renderer *renderer, SDL_Rect const *gameRect); // draw overlay after the game frame
void touch_updateGameFunctions(int32_t *flags, int32_t numFlags); // OR in on-screen button states
void touch_notifyHardwareInput(void);                            // keyboard/mouse activity seen
#endif

#endif // EDUKE32_IOS

#endif // iosbits_h_
