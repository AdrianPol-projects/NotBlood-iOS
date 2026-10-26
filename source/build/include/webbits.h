// Web browser platform glue for NotBlood (see webbits.cpp)

#ifndef webbits_h_
#define webbits_h_

#ifdef NOTBLOOD_WEB

#ifdef __cplusplus
extern "C" {
#endif

// gives control back to the browser: bFrame after a finished frame (waits for the next screen
// refresh), otherwise only when the last time was a while ago (loops that just wait)
void web_yield(int bFrame);

// files in the game folder changed (settings, saves): the page writes them to the browser's storage
void web_filesChanged(void);

// the device has a touch screen (tablets, phones, touch laptops)
int web_hastouch(void);

// the page's size in pixels changed (and stayed so for a moment) since the last call
int web_screenchanged(void);

// the render scale (percent of the page's pixels) of a first start
int web_defaultscale(void);

// the touch controls' KEYB button this frame (output pixels, r 0 for none): the page opens its
// keyboard when it's tapped, since a browser only shows one for a text field focused in the tap
void web_keyboardbutton(float x, float y, float r, int outw, int outh);
// the page's keyboard is open; web_keyboardclose closes it (the game stopped taking text)
int web_keyboardopen(void);
// the window ID of the keys typed on it (so they aren't taken for a hardware keyboard)
#define WEB_KEYBOARD_WINDOWID 0x4b4244u
void web_keyboardclose(void);
// once a frame: the next of the keys typed on it goes to the game (Blood's menus take one a frame)
void web_keyboardframe(void);

#ifdef __cplusplus
}
#endif

#endif // NOTBLOOD_WEB

#endif // webbits_h_
