// The audio functions of GML. This first form keeps the state of each
// sound but plays nothing.

#include <stdlib.h>
#include <string.h>
#include "ar_run_internal.h"

#define FN(name) static ARValue name(ARVM *vm, int argc, ARValue *a)
#define UNUSED (void)vm, (void)argc, (void)a

static int gNextVoice = 200000;

void ar_audio_init(ARRun *run) {
    (void)run;
}

void ar_audio_free(ARRun *run) {
    (void)run;
}

void ar_audio_mark(ARRun *run) {
    (void)run;
}

void ar_run_mix(ARRun *run, int16_t *out, int frames) {
    (void)run;
    memset(out, 0, sizeof(int16_t) * 2 * (size_t)frames);
}

FN(f_audio_play_sound) {
    UNUSED;
    return ar_real(gNextVoice++);
}

FN(f_nothing) {
    UNUSED;
    return ar_undefined();
}

FN(f_false) {
    UNUSED;
    return ar_bool(0);
}

FN(f_true) {
    UNUSED;
    return ar_bool(1);
}

FN(f_zero) {
    UNUSED;
    return ar_real(0);
}

FN(f_audio_create_stream) {
    UNUSED;
    return ar_real(gNextVoice++);
}

static const ARBuiltin kAudio[] = {
    {"audio_play_sound", f_audio_play_sound}, {"audio_stop_sound", f_nothing}, {"audio_stop_all", f_nothing},
    {"audio_pause_sound", f_nothing}, {"audio_resume_sound", f_nothing}, {"audio_pause_all", f_nothing},
    {"audio_resume_all", f_nothing}, {"audio_is_playing", f_false}, {"audio_exists", f_true},
    {"audio_sound_gain", f_nothing}, {"audio_sound_pitch", f_nothing}, {"audio_sound_length", f_zero},
    {"audio_sound_get_track_position", f_zero}, {"audio_sound_set_track_position", f_nothing},
    {"audio_set_master_gain", f_nothing}, {"audio_create_stream", f_audio_create_stream},
    {"audio_destroy_stream", f_nothing}, {"audio_group_load", f_true}, {"audio_group_is_loaded", f_true},
    {"audio_group_set_gain", f_nothing},
};

__attribute__((constructor)) static void register_audio(void) {
    ar_vm_register(kAudio, (int)(sizeof(kAudio) / sizeof(kAudio[0])), NULL, 0);
}
