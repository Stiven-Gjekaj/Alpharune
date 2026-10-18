// The audio functions of GML and the mixer.
//
// A sound is a WAV of 16 bits in the data file or in an audio group file,
// or an Ogg Vorbis file beside the data file, or a stream that
// audio_create_stream opens. Each play makes a voice with its own gain,
// pitch, and position. The game thread calls the GML functions, and the
// audio thread of the host calls ar_audio_render. A lock keeps them apart.

#include "ar_audio.h"

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ar_run_internal.h"

// The decoder is third party code. Its warnings are not ours to fix.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../third_party/stb_vorbis.c"
#pragma clang diagnostic pop

#define MAX_VOICES 48
#define MAX_GROUPS 8
#define VOICE_BASE 200000
#define OGG_BUFFER 2048

enum { SOUND_NONE, SOUND_PCM, SOUND_OGG };

typedef struct {
    int kind;
    int tried;  // The load ran, with success or not.
    const int16_t *pcm;
    int frames, channels, rate;
    const uint8_t *ogg;
    size_t ogg_size;
    int mapped;  // The Ogg bytes are a map of a file.
    float gain, pitch;
    int group;
    int stream;  // A sound of audio_create_stream.
    int destroyed;
} Sound;

typedef struct {
    int active, paused;
    int sound;
    int id;
    int loop;
    double pos;  // In frames of the sound.
    float pitch;
    float gain;
    float fade_from, fade_to;
    long fade_left, fade_total;  // In frames of the output.
    stb_vorbis *vorbis;
    int16_t buffer[OGG_BUFFER * 2];
    int buffer_first, buffer_count, decoded;  // decoded: the next frame of the decoder.
} Voice;

struct ARMixer {
    pthread_mutex_t lock;
    ARRun *run;
    Sound *sounds;
    int sound_count, sound_capacity;
    Voice voices[MAX_VOICES];
    int next_id;
    float master;
    float group_gain[MAX_GROUPS];
    // The audio groups, from audiogroupN.dat.
    const uint8_t *group_bytes[MAX_GROUPS];
    size_t group_size[MAX_GROUPS];
    ARAudio *group_audio[MAX_GROUPS];
    int32_t group_count[MAX_GROUPS];
    int group_tried[MAX_GROUPS];
    float *mix;
    int mix_frames;
};

static pthread_mutex_t gCurrentLock = PTHREAD_MUTEX_INITIALIZER;
static ARMixer *gCurrent;
static int gOutput;

void ar_audio_set_output(int running) {
    gOutput = running;
}

// ------------------------------------------------------------- the sounds

static int wav_parse(const uint8_t *b, size_t size, Sound *s) {
    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return 0;
    size_t p = 12;
    int channels = 0, rate = 0, bits = 0, format = 0;
    while (p + 8 <= size) {
        uint32_t n = (uint32_t)b[p + 4] | (uint32_t)b[p + 5] << 8 | (uint32_t)b[p + 6] << 16 | (uint32_t)b[p + 7] << 24;
        if (p + 8 + n > size) n = (uint32_t)(size - p - 8);
        if (memcmp(b + p, "fmt ", 4) == 0 && n >= 16) {
            format = b[p + 8] | b[p + 9] << 8;
            channels = b[p + 10] | b[p + 11] << 8;
            rate = (int)((uint32_t)b[p + 12] | (uint32_t)b[p + 13] << 8 | (uint32_t)b[p + 14] << 16 |
                         (uint32_t)b[p + 15] << 24);
            bits = b[p + 22] | b[p + 23] << 8;
        } else if (memcmp(b + p, "data", 4) == 0) {
            if (format != 1 || bits != 16 || channels < 1 || channels > 2 || rate <= 0) return 0;
            // The samples are aligned to 2 bytes in the files of GameMaker.
            if (((uintptr_t)(b + p + 8)) & 1) return 0;
            s->kind = SOUND_PCM;
            s->pcm = (const int16_t *)(b + p + 8);
            s->channels = channels;
            s->rate = rate;
            s->frames = (int)(n / (uint32_t)(2 * channels));
            return 1;
        }
        p += 8 + n + (n & 1);
    }
    return 0;
}

static int ogg_parse(const uint8_t *bytes, size_t size, Sound *s) {
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_memory(bytes, (int)size, &error, NULL);
    if (!v) return 0;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    s->kind = SOUND_OGG;
    s->ogg = bytes;
    s->ogg_size = size;
    s->channels = info.channels;
    s->rate = (int)info.sample_rate;
    s->frames = (int)stb_vorbis_stream_length_in_samples(v);
    stb_vorbis_close(v);
    return s->frames > 0;
}

static const uint8_t *map_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    fstat(fd, &st);
    if (st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *size = (size_t)st.st_size;
    return p;
}

static int load_group(ARMixer *m, int group) {
    if (group < 1 || group >= MAX_GROUPS) return 0;
    if (m->group_tried[group]) return m->group_audio[group] != NULL;
    m->group_tried[group] = 1;
    char name[64], path[1200];
    snprintf(name, sizeof(name), "audiogroup%d.dat", group);
    ar_run_path(m->run, name, 0, path, sizeof(path));
    size_t size;
    const uint8_t *bytes = map_file(path, &size);
    if (!bytes) {
        ar_vm_log(m->run->vm, "no audio group file %s", path);
        return 0;
    }
    m->group_bytes[group] = bytes;
    m->group_size[group] = size;
    if (!ar_data_read_audio_group(bytes, size, &m->group_audio[group], &m->group_count[group])) {
        ar_vm_log(m->run->vm, "audio group %d does not read", group);
        return 0;
    }
    return 1;
}

// Loads a sound of the data on its first play.
static void load_sound(ARMixer *m, int index) {
    Sound *s = &m->sounds[index];
    if (s->tried) return;
    s->tried = 1;
    const ARData *d = m->run->data;
    if (index >= d->sound_count) return;
    const ARSoundInfo *info = &d->sounds[index];
    if (info->audio >= 0 && (info->flags & 1)) {
        const ARAudio *a = NULL;
        if (info->group <= 0) {
            if (info->audio < d->audio_count) a = &d->audio[info->audio];
        } else if (load_group(m, info->group) && info->audio < m->group_count[info->group]) {
            a = &m->group_audio[info->group][info->audio];
        }
        if (a && !wav_parse(a->bytes, a->size, s) && !ogg_parse(a->bytes, a->size, s)) {
            ar_vm_log(m->run->vm, "sound %s is not a WAV of 16 bits", info->name);
        }
        return;
    }
    // A sound in a file beside the data file.
    const char *file = info->file ? info->file : info->name;
    char path[1200], name[600];
    snprintf(name, sizeof(name), "%s", file);
    ar_run_path(m->run, name, 0, path, sizeof(path));
    size_t size;
    const uint8_t *bytes = map_file(path, &size);
    if (!bytes) {
        snprintf(name, sizeof(name), "%s.ogg", file);
        ar_run_path(m->run, name, 0, path, sizeof(path));
        bytes = map_file(path, &size);
    }
    if (!bytes) {
        ar_vm_log(m->run->vm, "no file for sound %s", info->name);
        return;
    }
    s->mapped = 1;
    if (!ogg_parse(bytes, size, s) && !wav_parse(bytes, size, s)) {
        ar_vm_log(m->run->vm, "sound file %s does not decode", path);
        munmap((void *)bytes, size);
        s->kind = SOUND_NONE;
    }
    if (s->kind == SOUND_NONE) s->mapped = 0;
}

void ar_audio_init(ARRun *run) {
    ARMixer *m = calloc(1, sizeof(ARMixer));
    pthread_mutex_init(&m->lock, NULL);
    m->run = run;
    m->master = 1;
    m->next_id = VOICE_BASE;
    for (int g = 0; g < MAX_GROUPS; g++) m->group_gain[g] = 1;
    const ARData *d = run->data;
    m->sound_capacity = d->sound_count + 64;
    m->sounds = calloc((size_t)m->sound_capacity, sizeof(Sound));
    m->sound_count = d->sound_count;
    for (int i = 0; i < d->sound_count; i++) {
        m->sounds[i].gain = d->sounds[i].volume > 0 ? d->sounds[i].volume : 1;
        m->sounds[i].pitch = d->sounds[i].pitch > 0 ? d->sounds[i].pitch : 1;
        m->sounds[i].group = d->sounds[i].group;
    }
    run->mixer = m;
    pthread_mutex_lock(&gCurrentLock);
    gCurrent = m;
    pthread_mutex_unlock(&gCurrentLock);
}

static void close_voice(Voice *v) {
    if (v->vorbis) stb_vorbis_close(v->vorbis);
    v->vorbis = NULL;
    v->active = 0;
}

void ar_audio_free(ARRun *run) {
    ARMixer *m = run->mixer;
    if (!m) return;
    pthread_mutex_lock(&gCurrentLock);
    if (gCurrent == m) gCurrent = NULL;
    pthread_mutex_unlock(&gCurrentLock);
    for (int i = 0; i < MAX_VOICES; i++) close_voice(&m->voices[i]);
    for (int i = 0; i < m->sound_count; i++) {
        if (m->sounds[i].mapped) munmap((void *)m->sounds[i].ogg, m->sounds[i].ogg_size);
    }
    for (int g = 0; g < MAX_GROUPS; g++) {
        free(m->group_audio[g]);
        if (m->group_bytes[g]) munmap((void *)m->group_bytes[g], m->group_size[g]);
    }
    free(m->sounds);
    free(m->mix);
    pthread_mutex_destroy(&m->lock);
    free(m);
    run->mixer = NULL;
}

void ar_audio_mark(ARRun *run) {
    (void)run;
}

// --------------------------------------------------------------- the mix

// One stereo frame of an Ogg voice, at a frame index of the sound.
static int ogg_frame(Sound *s, Voice *v, int index, float out[2]) {
    if (index < 0 || index >= s->frames || !v->vorbis) return 0;
    if (index < v->buffer_first || index >= v->decoded + OGG_BUFFER * 4) {
        if (!stb_vorbis_seek_frame(v->vorbis, (unsigned)index)) return 0;
        v->buffer_first = v->decoded = index;
        v->buffer_count = 0;
    }
    while (index >= v->buffer_first + v->buffer_count) {
        int got = stb_vorbis_get_samples_short_interleaved(v->vorbis, 2, v->buffer, OGG_BUFFER * 2);
        if (got <= 0) return 0;
        v->buffer_first = v->decoded;
        v->buffer_count = got;
        v->decoded += got;
    }
    out[0] = v->buffer[(index - v->buffer_first) * 2];
    out[1] = v->buffer[(index - v->buffer_first) * 2 + 1];
    return 1;
}

static int pcm_frame(const Sound *s, int index, float out[2]) {
    if (index < 0 || index >= s->frames) return 0;
    if (s->channels == 1) {
        out[0] = out[1] = s->pcm[index];
    } else {
        out[0] = s->pcm[index * 2];
        out[1] = s->pcm[index * 2 + 1];
    }
    return 1;
}

static float voice_gain(ARMixer *m, Voice *v) {
    Sound *s = &m->sounds[v->sound];
    int g = s->group >= 0 && s->group < MAX_GROUPS ? s->group : 0;
    return v->gain * s->gain * m->group_gain[g] * m->master;
}

static double voice_step(ARMixer *m, Voice *v) {
    Sound *s = &m->sounds[v->sound];
    return (double)s->rate / AR_AUDIO_RATE * v->pitch * s->pitch;
}

static void fade_step(Voice *v, long frames) {
    if (v->fade_left <= 0) return;
    v->fade_left -= frames;
    if (v->fade_left <= 0) {
        v->fade_left = 0;
        v->gain = v->fade_to;
    } else {
        float t = 1 - (float)v->fade_left / (float)v->fade_total;
        v->gain = v->fade_from + (v->fade_to - v->fade_from) * t;
    }
}

static void render_voice(ARMixer *m, Voice *v, float *mix, int frames) {
    Sound *s = &m->sounds[v->sound];
    double step = voice_step(m, v);
    int restarts = 0;
    for (int i = 0; i < frames; i++) {
        if ((i & 63) == 0) fade_step(v, i == 0 ? 0 : 64);
        int index = (int)v->pos;
        float a[2], b[2];
        int ok = s->kind == SOUND_PCM ? pcm_frame(s, index, a) : ogg_frame(s, v, index, a);
        if (!ok) {
            // A loop starts again, but a sound that gives no frame at its
            // start stops, so that the loop cannot go on with no end.
            if (v->loop && s->frames > 0 && restarts++ == 0) {
                v->pos = 0;
                i--;
                continue;
            }
            close_voice(v);
            return;
        }
        float t = (float)(v->pos - index);
        if (t > 0 && (s->kind == SOUND_PCM ? pcm_frame(s, index + 1, b) : ogg_frame(s, v, index + 1, b))) {
            a[0] += (b[0] - a[0]) * t;
            a[1] += (b[1] - a[1]) * t;
        }
        float g = voice_gain(m, v);
        mix[i * 2] += a[0] * g;
        mix[i * 2 + 1] += a[1] * g;
        v->pos += step;
    }
}

void ar_audio_render(int16_t *out, int frames) {
    pthread_mutex_lock(&gCurrentLock);
    ARMixer *m = gCurrent;
    if (!m) {
        memset(out, 0, sizeof(int16_t) * 2 * (size_t)frames);
        pthread_mutex_unlock(&gCurrentLock);
        return;
    }
    pthread_mutex_lock(&m->lock);
    if (frames > m->mix_frames) {
        m->mix = realloc(m->mix, sizeof(float) * 2 * (size_t)frames);
        m->mix_frames = frames;
    }
    memset(m->mix, 0, sizeof(float) * 2 * (size_t)frames);
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &m->voices[i];
        if (v->active && !v->paused) render_voice(m, v, m->mix, frames);
    }
    for (int i = 0; i < frames * 2; i++) {
        float x = m->mix[i];
        out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
    pthread_mutex_unlock(&m->lock);
    pthread_mutex_unlock(&gCurrentLock);
}

void ar_run_mix(ARRun *run, int16_t *out, int frames) {
    (void)run;
    ar_audio_render(out, frames);
}

// With no output, each voice moves by the time of a frame.
void ar_audio_advance(ARRun *run, double seconds) {
    ARMixer *m = run->mixer;
    if (!m || gOutput) return;
    pthread_mutex_lock(&m->lock);
    long frames = (long)(seconds * AR_AUDIO_RATE);
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &m->voices[i];
        if (!v->active || v->paused) continue;
        fade_step(v, frames);
        Sound *s = &m->sounds[v->sound];
        v->pos += voice_step(m, v) * (double)frames;
        if (v->pos >= s->frames) {
            if (v->loop && s->frames > 0) v->pos = fmod(v->pos, s->frames);
            else close_voice(v);
        }
    }
    pthread_mutex_unlock(&m->lock);
}

// -------------------------------------------------------- GML functions

#define FN(name) static ARValue name(ARVM *vm, int argc, ARValue *a)
#define UNUSED (void)vm, (void)argc, (void)a
#define MIXER (ar_run_of(vm)->mixer)
#define INT(i, d) ar_arg_int(argc, a, i, d)
#define REAL(i, d) ar_arg_real(argc, a, i, d)

static int sound_ok(ARMixer *m, int index) {
    return index >= 0 && index < m->sound_count && !m->sounds[index].destroyed;
}

// The voices that an id names: one voice, or each voice of a sound.
static int voices_of(ARMixer *m, int id, Voice **out) {
    int n = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &m->voices[i];
        if (!v->active) continue;
        if (id >= VOICE_BASE ? v->id == id : v->sound == id) out[n++] = v;
    }
    return n;
}

FN(f_audio_play_sound) {
    UNUSED;
    ARMixer *m = MIXER;
    int index = INT(0, -1);
    if (!m || !sound_ok(m, index)) return ar_real(-1);
    pthread_mutex_lock(&m->lock);
    load_sound(m, index);
    Sound *s = &m->sounds[index];
    if (s->kind == SOUND_NONE) {
        pthread_mutex_unlock(&m->lock);
        return ar_real(-1);
    }
    // A free voice, or the oldest voice.
    Voice *v = NULL;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!m->voices[i].active) {
            v = &m->voices[i];
            break;
        }
    }
    if (!v) {
        v = &m->voices[0];
        for (int i = 1; i < MAX_VOICES; i++) {
            if (m->voices[i].id < v->id) v = &m->voices[i];
        }
        close_voice(v);
    }
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->sound = index;
    v->id = m->next_id++;
    v->loop = argc > 2 && ar_truthy(a[2]);
    v->gain = argc > 3 ? (float)REAL(3, 1) : 1;
    v->pitch = argc > 5 ? (float)REAL(5, 1) : 1;
    v->pos = argc > 4 ? REAL(4, 0) * s->rate : 0;
    if (s->kind == SOUND_OGG) {
        int error = 0;
        v->vorbis = stb_vorbis_open_memory(s->ogg, (int)s->ogg_size, &error, NULL);
        if (!v->vorbis) v->active = 0;
    }
    int id = v->active ? v->id : -1;
    pthread_mutex_unlock(&m->lock);
    return ar_real(id);
}

static ARValue for_voices(ARVM *vm, int id, void (*fn)(Voice *v, void *ctx), void *ctx) {
    ARMixer *m = MIXER;
    if (!m) return ar_undefined();
    pthread_mutex_lock(&m->lock);
    Voice *list[MAX_VOICES];
    int n = voices_of(m, id, list);
    for (int i = 0; i < n; i++) fn(list[i], ctx);
    pthread_mutex_unlock(&m->lock);
    return ar_undefined();
}

static void stop_voice(Voice *v, void *ctx) {
    (void)ctx;
    close_voice(v);
}

static void pause_voice(Voice *v, void *ctx) {
    v->paused = *(int *)ctx;
}

FN(f_audio_stop_sound) {
    UNUSED;
    return for_voices(vm, INT(0, -1), stop_voice, NULL);
}

FN(f_audio_pause_sound) {
    UNUSED;
    int on = 1;
    return for_voices(vm, INT(0, -1), pause_voice, &on);
}

FN(f_audio_resume_sound) {
    UNUSED;
    int on = 0;
    return for_voices(vm, INT(0, -1), pause_voice, &on);
}

static void all_voices(ARVM *vm, int stop, int pause) {
    ARMixer *m = MIXER;
    if (!m) return;
    pthread_mutex_lock(&m->lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (stop) close_voice(&m->voices[i]);
        else m->voices[i].paused = pause;
    }
    pthread_mutex_unlock(&m->lock);
}

FN(f_audio_stop_all) {
    UNUSED;
    all_voices(vm, 1, 0);
    return ar_undefined();
}

FN(f_audio_pause_all) {
    UNUSED;
    all_voices(vm, 0, 1);
    return ar_undefined();
}

FN(f_audio_resume_all) {
    UNUSED;
    all_voices(vm, 0, 0);
    return ar_undefined();
}

FN(f_audio_is_playing) {
    UNUSED;
    ARMixer *m = MIXER;
    if (!m) return ar_bool(0);
    pthread_mutex_lock(&m->lock);
    Voice *list[MAX_VOICES];
    int n = voices_of(m, INT(0, -1), list);
    pthread_mutex_unlock(&m->lock);
    return ar_bool(n > 0);
}

FN(f_audio_exists) {
    UNUSED;
    ARMixer *m = MIXER;
    int id = INT(0, -1);
    if (!m) return ar_bool(0);
    if (id >= VOICE_BASE) {
        pthread_mutex_lock(&m->lock);
        Voice *list[MAX_VOICES];
        int n = voices_of(m, id, list);
        pthread_mutex_unlock(&m->lock);
        return ar_bool(n > 0);
    }
    return ar_bool(sound_ok(m, id));
}

typedef struct {
    float gain;
    long frames;
} Fade;

static void fade_voice(Voice *v, void *ctx) {
    Fade *f = ctx;
    if (f->frames <= 0) {
        v->gain = f->gain;
        v->fade_left = 0;
    } else {
        v->fade_from = v->gain;
        v->fade_to = f->gain;
        v->fade_total = v->fade_left = f->frames;
    }
}

FN(f_audio_sound_gain) {
    UNUSED;
    ARMixer *m = MIXER;
    int id = INT(0, -1);
    float gain = (float)REAL(1, 1);
    if (!m) return ar_undefined();
    if (id < VOICE_BASE && sound_ok(m, id)) {
        // The gain of a sound asset: the voices of it fade too.
        pthread_mutex_lock(&m->lock);
        m->sounds[id].gain = gain;
        pthread_mutex_unlock(&m->lock);
        return ar_undefined();
    }
    Fade f = {gain, (long)(REAL(2, 0) / 1000.0 * AR_AUDIO_RATE)};
    return for_voices(vm, id, fade_voice, &f);
}

FN(f_audio_sound_get_gain) {
    UNUSED;
    ARMixer *m = MIXER;
    int id = INT(0, -1);
    if (!m) return ar_real(0);
    if (id < VOICE_BASE) return ar_real(sound_ok(m, id) ? m->sounds[id].gain : 0);
    pthread_mutex_lock(&m->lock);
    Voice *list[MAX_VOICES];
    int n = voices_of(m, id, list);
    double g = n ? list[0]->gain : 0;
    pthread_mutex_unlock(&m->lock);
    return ar_real(g);
}

static void pitch_voice(Voice *v, void *ctx) {
    v->pitch = *(float *)ctx;
}

FN(f_audio_sound_pitch) {
    UNUSED;
    ARMixer *m = MIXER;
    int id = INT(0, -1);
    float pitch = (float)REAL(1, 1);
    if (!m) return ar_undefined();
    if (id < VOICE_BASE && sound_ok(m, id)) {
        pthread_mutex_lock(&m->lock);
        m->sounds[id].pitch = pitch;
        pthread_mutex_unlock(&m->lock);
        return ar_undefined();
    }
    return for_voices(vm, id, pitch_voice, &pitch);
}

FN(f_audio_sound_length) {
    UNUSED;
    ARMixer *m = MIXER;
    int id = INT(0, -1);
    if (!m) return ar_real(0);
    pthread_mutex_lock(&m->lock);
    if (id >= VOICE_BASE) {
        Voice *list[MAX_VOICES];
        id = voices_of(m, id, list) ? list[0]->sound : -1;
    }
    double length = 0;
    if (sound_ok(m, id)) {
        load_sound(m, id);
        Sound *s = &m->sounds[id];
        if (s->rate > 0) length = (double)s->frames / s->rate;
    }
    pthread_mutex_unlock(&m->lock);
    return ar_real(length);
}

FN(f_audio_sound_get_track_position) {
    UNUSED;
    ARMixer *m = MIXER;
    if (!m) return ar_real(0);
    pthread_mutex_lock(&m->lock);
    Voice *list[MAX_VOICES];
    int n = voices_of(m, INT(0, -1), list);
    double pos = 0;
    if (n) {
        Sound *s = &m->sounds[list[0]->sound];
        if (s->rate > 0) pos = list[0]->pos / s->rate;
    }
    pthread_mutex_unlock(&m->lock);
    return ar_real(pos);
}

static void seek_voice(Voice *v, void *ctx) {
    v->pos = *(double *)ctx;
}

FN(f_audio_sound_set_track_position) {
    UNUSED;
    ARMixer *m = MIXER;
    if (!m) return ar_undefined();
    int id = INT(0, -1);
    double seconds = REAL(1, 0);
    pthread_mutex_lock(&m->lock);
    Voice *list[MAX_VOICES];
    int n = voices_of(m, id, list);
    for (int i = 0; i < n; i++) {
        double pos = seconds * m->sounds[list[i]->sound].rate;
        seek_voice(list[i], &pos);
    }
    pthread_mutex_unlock(&m->lock);
    return ar_undefined();
}

FN(f_audio_set_master_gain) {
    UNUSED;
    ARMixer *m = MIXER;
    if (m) m->master = (float)REAL(1, 1);
    return ar_undefined();
}

FN(f_audio_group_set_gain) {
    UNUSED;
    ARMixer *m = MIXER;
    int g = INT(0, 0);
    if (m && g >= 0 && g < MAX_GROUPS) m->group_gain[g] = (float)REAL(1, 1);
    return ar_undefined();
}

FN(f_audio_create_stream) {
    UNUSED;
    ARMixer *m = MIXER;
    if (!m || argc == 0) return ar_real(-1);
    ARRun *run = ar_run_of(vm);
    char path[1200];
    ar_run_path(run, ar_to_str(ar_vm_heap(vm), a[0])->text, 0, path, sizeof(path));
    size_t size;
    const uint8_t *bytes = map_file(path, &size);
    if (!bytes) {
        ar_vm_log(vm, "audio_create_stream: no file %s", path);
        return ar_real(-1);
    }
    pthread_mutex_lock(&m->lock);
    int index = -1;
    // A destroyed stream leaves a slot for the next.
    for (int i = run->data->sound_count; i < m->sound_count; i++) {
        if (m->sounds[i].destroyed) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (m->sound_count == m->sound_capacity) {
            m->sound_capacity *= 2;
            m->sounds = realloc(m->sounds, sizeof(Sound) * (size_t)m->sound_capacity);
        }
        index = m->sound_count++;
    }
    Sound *s = &m->sounds[index];
    memset(s, 0, sizeof(*s));
    s->tried = 1;
    s->stream = 1;
    s->gain = 1;
    s->pitch = 1;
    s->mapped = 1;
    if (!ogg_parse(bytes, size, s)) {
        munmap((void *)bytes, size);
        s->destroyed = 1;
        s->mapped = 0;
        index = -1;
    }
    pthread_mutex_unlock(&m->lock);
    return ar_real(index);
}

FN(f_audio_destroy_stream) {
    UNUSED;
    ARMixer *m = MIXER;
    int index = INT(0, -1);
    if (!m || !sound_ok(m, index) || !m->sounds[index].stream) return ar_undefined();
    pthread_mutex_lock(&m->lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (m->voices[i].active && m->voices[i].sound == index) close_voice(&m->voices[i]);
    }
    Sound *s = &m->sounds[index];
    if (s->mapped) munmap((void *)s->ogg, s->ogg_size);
    memset(s, 0, sizeof(*s));
    s->destroyed = 1;
    pthread_mutex_unlock(&m->lock);
    return ar_undefined();
}

FN(f_true) {
    UNUSED;
    return ar_bool(1);
}

static const ARBuiltin kAudio[] = {
    {"audio_play_sound", f_audio_play_sound}, {"audio_stop_sound", f_audio_stop_sound},
    {"audio_stop_all", f_audio_stop_all}, {"audio_pause_sound", f_audio_pause_sound},
    {"audio_resume_sound", f_audio_resume_sound}, {"audio_pause_all", f_audio_pause_all},
    {"audio_resume_all", f_audio_resume_all}, {"audio_is_playing", f_audio_is_playing},
    {"audio_exists", f_audio_exists}, {"audio_sound_gain", f_audio_sound_gain},
    {"audio_sound_get_gain", f_audio_sound_get_gain}, {"audio_sound_pitch", f_audio_sound_pitch},
    {"audio_sound_length", f_audio_sound_length},
    {"audio_sound_get_track_position", f_audio_sound_get_track_position},
    {"audio_sound_set_track_position", f_audio_sound_set_track_position},
    {"audio_set_master_gain", f_audio_set_master_gain}, {"audio_create_stream", f_audio_create_stream},
    {"audio_destroy_stream", f_audio_destroy_stream}, {"audio_group_load", f_true},
    {"audio_group_is_loaded", f_true}, {"audio_group_set_gain", f_audio_group_set_gain},
};

__attribute__((constructor)) static void register_audio(void) {
    ar_vm_register(kAudio, (int)(sizeof(kAudio) / sizeof(kAudio[0])), NULL, 0);
}
