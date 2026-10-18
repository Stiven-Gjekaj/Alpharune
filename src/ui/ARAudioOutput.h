// ARAudioOutput: sends the mix of ar_audio to the speaker of the phone with
// the RemoteIO audio unit.

#ifndef AR_AUDIO_OUTPUT_H
#define AR_AUDIO_OUTPUT_H

// Starts the audio session and the output. The audio unit calls
// ar_audio_render on its own thread. The session is "ambient sound", so
// the ring/silent switch mutes the game. Returns 0 when the output does
// not start.
int ar_audio_output_start(void);

// Stops the output when the app goes to the background, and starts it
// again when the app comes back.
void ar_audio_output_set_active(int active);

#endif
