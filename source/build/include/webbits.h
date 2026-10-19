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

#ifdef __cplusplus
}
#endif

#endif // NOTBLOOD_WEB

#endif // webbits_h_
