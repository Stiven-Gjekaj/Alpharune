// ar_audio: the sound of the runtime. The audio functions of GML play the
// sounds of the data file, the audio groups, and the Ogg files of the game
// into one stereo stream at AR_AUDIO_RATE.

#ifndef AR_AUDIO_H
#define AR_AUDIO_H

#include <stdint.h>

#define AR_AUDIO_RATE 44100

// The host calls this from its audio thread: it mixes frames of stereo
// samples of the game that runs now, or silence.
void ar_audio_render(int16_t *out, int frames);

// The host tells the runtime that its output runs. With no output, the
// runtime moves each sound forward by the time of each frame, so that a
// sound still ends and audio_is_playing tells the truth.
void ar_audio_set_output(int running);

#endif
