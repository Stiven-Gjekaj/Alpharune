#include "ar_keys.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ar_run.h"

// A hold is at most MAX_HOLD frames long, so each frame looks only at the
// keys of a window.
#define MAX_HOLD 2000

typedef struct {
    long frame;
    int key;
    int length;
} Key;

typedef struct {
    long frame;
    char *command;
} Debug;

struct ARKeys {
    Key *keys;
    int key_count, key_capacity;
    Debug *debugs;
    int debug_count, debug_capacity;
    int low;         // The first key that can still change a frame.
    int next_debug;  // The first command that did not run.
};

ARKeys *ar_keys_new(void) {
    return calloc(1, sizeof(ARKeys));
}

void ar_keys_free(ARKeys *s) {
    if (!s) return;
    for (int i = 0; i < s->debug_count; i++) free(s->debugs[i].command);
    free(s->debugs);
    free(s->keys);
    free(s);
}

int ar_keys_key_code(const char *name) {
    if (strcmp(name, "ENTER") == 0) return AR_VK_ENTER;
    if (strcmp(name, "SHIFT") == 0) return AR_VK_SHIFT;
    if (strcmp(name, "ESCAPE") == 0) return AR_VK_ESCAPE;
    if (strcmp(name, "UP") == 0) return AR_VK_UP;
    if (strcmp(name, "DOWN") == 0) return AR_VK_DOWN;
    if (strcmp(name, "LEFT") == 0) return AR_VK_LEFT;
    if (strcmp(name, "RIGHT") == 0) return AR_VK_RIGHT;
    if (strcmp(name, "SPACE") == 0) return AR_VK_SPACE;
    if (strlen(name) == 1) return toupper((unsigned char)name[0]);
    return 0;
}

void ar_keys_add_key(ARKeys *s, long frame, int key, int length) {
    if (s->key_count == s->key_capacity) {
        s->key_capacity = s->key_capacity ? s->key_capacity * 2 : 1024;
        s->keys = realloc(s->keys, sizeof(Key) * (size_t)s->key_capacity);
    }
    if (length < 1) length = 1;
    if (length > MAX_HOLD) length = MAX_HOLD;
    s->keys[s->key_count++] = (Key){frame, key, length};
}

void ar_keys_add_debug(ARKeys *s, long frame, const char *command) {
    if (s->debug_count == s->debug_capacity) {
        s->debug_capacity = s->debug_capacity ? s->debug_capacity * 2 : 64;
        s->debugs = realloc(s->debugs, sizeof(Debug) * (size_t)s->debug_capacity);
    }
    s->debugs[s->debug_count++] = (Debug){frame, strdup(command)};
}

int ar_keys_parse(ARKeys *s, const char *text) {
    int added = 0;
    long last = 0;
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[512];
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = end ? end + 1 : p + n;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        line[strcspn(line, "\r")] = 0;
        char when[32], name[64];
        int length = 1;
        int fields = sscanf(line, "%31s %63s %d", when, name, &length);
        if (fields < 2) continue;
        long frame = when[0] == '+' ? last + atol(when + 1) : atol(when);
        last = frame;
        if (strcmp(name, "debug") == 0) {
            ar_keys_add_debug(s, frame, strstr(line, "debug") + 6);
        } else {
            ar_keys_add_key(s, frame, ar_keys_key_code(name), fields >= 3 ? length : 1);
        }
        added++;
    }
    return added;
}

static int compare_keys(const void *a, const void *b) {
    const Key *x = a, *y = b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return 0;
}

static int compare_debugs(const void *a, const void *b) {
    const Debug *x = a, *y = b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return 0;
}

void ar_keys_sort(ARKeys *s) {
    // mergesort keeps the order of the lines of one frame.
    if (s->key_count > 1) mergesort(s->keys, (size_t)s->key_count, sizeof(Key), compare_keys);
    if (s->debug_count > 1) mergesort(s->debugs, (size_t)s->debug_count, sizeof(Debug), compare_debugs);
}

void ar_keys_step(ARKeys *s, long f, void (*key)(void *context, int key, int down),
                    void (*debug)(void *context, const char *command), void *context) {
    while (s->low < s->key_count && s->keys[s->low].frame < f - MAX_HOLD) s->low++;
    const Key *keys = s->keys;
    for (int k = s->low; k < s->key_count && keys[k].frame <= f; k++) {
        if (keys[k].frame + keys[k].length != f) continue;
        int held = 0;
        for (int j = s->low; j < s->key_count && keys[j].frame <= f; j++) {
            if (j != k && keys[j].key == keys[k].key && f < keys[j].frame + keys[j].length) held = 1;
        }
        if (!held) key(context, keys[k].key, 0);
    }
    for (int k = s->low; k < s->key_count && keys[k].frame <= f; k++) {
        if (keys[k].frame == f) key(context, keys[k].key, 1);
    }
    while (s->next_debug < s->debug_count && s->debugs[s->next_debug].frame < f) s->next_debug++;
    while (s->next_debug < s->debug_count && s->debugs[s->next_debug].frame == f) {
        if (debug) debug(context, s->debugs[s->next_debug].command);
        s->next_debug++;
    }
}

long ar_keys_last_frame(const ARKeys *s) {
    long last = 0;
    for (int i = 0; i < s->key_count; i++) {
        if (s->keys[i].frame > last) last = s->keys[i].frame;
    }
    for (int i = 0; i < s->debug_count; i++) {
        if (s->debugs[i].frame > last) last = s->debugs[i].frame;
    }
    return last;
}
