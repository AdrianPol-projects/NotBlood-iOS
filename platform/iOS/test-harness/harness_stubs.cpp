// The Linux harness builds without ALSA; the audiolib driver table still references it.
#include "driver_alsa.h"

int ALSA_ClientID = -1;
int ALSA_PortID = -1;

int ALSADrv_GetError(void) { return -1; }
const char *ALSADrv_ErrorString(int) { return "ALSA not available in the test harness"; }
int ALSADrv_MIDI_Init(midifuncs *) { return -1; }
void ALSADrv_MIDI_Shutdown(void) {}
int ALSADrv_MIDI_StartPlayback(void) { return -1; }
void ALSADrv_MIDI_HaltPlayback(void) {}
unsigned int ALSADrv_MIDI_GetTick(void) { return 0; }
void ALSADrv_MIDI_SetTempo(int, int) {}
void ALSADrv_MIDI_Lock(void) {}
void ALSADrv_MIDI_Unlock(void) {}
void ALSADrv_MIDI_Service(void) {}
void ALSADrv_MIDI_QueueStart(void) {}
void ALSADrv_MIDI_QueueStop(void) {}
