#include "ARForm.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char label[48];
    uint32_t at;   // Where the pointer is.
    int string;    // The pointer is to a string, not to a label.
} Patch;

typedef struct {
    char label[48];
    uint32_t offset;
} Label;

struct ARForm {
    uint8_t *b;
    size_t size, capacity;
    size_t chunk;  // The offset of the size of the open chunk, or 0.
    Patch *patches;
    int patch_count, patch_capacity;
    Label *labels;
    int label_count, label_capacity;
    char **strings;
    int string_count, string_capacity;
};

static void put(ARForm *f, const void *bytes, size_t n) {
    if (f->size + n > f->capacity) {
        f->capacity = (f->size + n) * 2 + 256;
        f->b = realloc(f->b, f->capacity);
    }
    memcpy(f->b + f->size, bytes, n);
    f->size += n;
}

static void set32(ARForm *f, size_t at, uint32_t v) {
    f->b[at] = (uint8_t)v;
    f->b[at + 1] = (uint8_t)(v >> 8);
    f->b[at + 2] = (uint8_t)(v >> 16);
    f->b[at + 3] = (uint8_t)(v >> 24);
}

ARForm *ar_form_new(void) {
    ARForm *f = calloc(1, sizeof(ARForm));
    put(f, "FORM\0\0\0\0", 8);
    return f;
}

void ar_form_free(ARForm *f) {
    for (int i = 0; i < f->string_count; i++) free(f->strings[i]);
    free(f->strings);
    free(f->patches);
    free(f->labels);
    free(f->b);
    free(f);
}

static void end_chunk(ARForm *f) {
    if (f->chunk) set32(f, f->chunk, (uint32_t)(f->size - f->chunk - 4));
    f->chunk = 0;
}

void ar_form_chunk(ARForm *f, const char *name) {
    end_chunk(f);
    put(f, name, 4);
    f->chunk = f->size;
    put(f, "\0\0\0\0", 4);
}

void ar_form_u32(ARForm *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(f, b, 4);
}

void ar_form_f32(ARForm *f, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    ar_form_u32(f, bits);
}

void ar_form_u16(ARForm *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(f, b, 2);
}

void ar_form_bytes(ARForm *f, const void *bytes, size_t size) {
    put(f, bytes, size);
}

static void add_patch(ARForm *f, const char *label, int string) {
    if (f->patch_count == f->patch_capacity) {
        f->patch_capacity = f->patch_capacity * 2 + 16;
        f->patches = realloc(f->patches, (size_t)f->patch_capacity * sizeof(Patch));
    }
    Patch *p = &f->patches[f->patch_count++];
    strncpy(p->label, label, sizeof(p->label) - 1);
    p->label[sizeof(p->label) - 1] = 0;
    p->at = (uint32_t)f->size;
    p->string = string;
    ar_form_u32(f, 0);
}

void ar_form_ptr(ARForm *f, const char *label) {
    add_patch(f, label, 0);
}

void ar_form_str(ARForm *f, const char *text) {
    for (int i = 0; i < f->string_count; i++) {
        if (strcmp(f->strings[i], text) == 0) {
            add_patch(f, text, 1);
            return;
        }
    }
    if (f->string_count == f->string_capacity) {
        f->string_capacity = f->string_capacity * 2 + 16;
        f->strings = realloc(f->strings, (size_t)f->string_capacity * sizeof(char *));
    }
    f->strings[f->string_count++] = strdup(text);
    add_patch(f, text, 1);
}

void ar_form_label(ARForm *f, const char *label) {
    if (f->label_count == f->label_capacity) {
        f->label_capacity = f->label_capacity * 2 + 16;
        f->labels = realloc(f->labels, (size_t)f->label_capacity * sizeof(Label));
    }
    Label *l = &f->labels[f->label_count++];
    strncpy(l->label, label, sizeof(l->label) - 1);
    l->label[sizeof(l->label) - 1] = 0;
    l->offset = (uint32_t)f->size;
}

int ar_form_string_index(ARForm *f, const char *text) {
    for (int i = 0; i < f->string_count; i++) {
        if (strcmp(f->strings[i], text) == 0) return i;
    }
    if (f->string_count == f->string_capacity) {
        f->string_capacity = f->string_capacity * 2 + 16;
        f->strings = realloc(f->strings, (size_t)f->string_capacity * sizeof(char *));
    }
    f->strings[f->string_count++] = strdup(text);
    return f->string_count - 1;
}

uint32_t ar_form_offset(ARForm *f) {
    return (uint32_t)f->size;
}

const uint8_t *ar_form_finish(ARForm *f, size_t *size) {
    // The STRG chunk: a list of pointers, then each string with its length.
    ar_form_chunk(f, "STRG");
    ar_form_u32(f, (uint32_t)f->string_count);
    size_t list = f->size;
    for (int i = 0; i < f->string_count; i++) ar_form_u32(f, 0);
    uint32_t *text = calloc((size_t)f->string_count + 1, sizeof(uint32_t));
    for (int i = 0; i < f->string_count; i++) {
        set32(f, list + 4 * (size_t)i, (uint32_t)f->size);
        ar_form_u32(f, (uint32_t)strlen(f->strings[i]));
        text[i] = (uint32_t)f->size;
        put(f, f->strings[i], strlen(f->strings[i]) + 1);
    }
    end_chunk(f);
    set32(f, 4, (uint32_t)(f->size - 8));
    for (int i = 0; i < f->patch_count; i++) {
        Patch *p = &f->patches[i];
        uint32_t value = 0;
        if (p->string) {
            for (int k = 0; k < f->string_count; k++) {
                if (strcmp(f->strings[k], p->label) == 0) value = text[k];
            }
        } else {
            for (int k = 0; k < f->label_count; k++) {
                if (strcmp(f->labels[k].label, p->label) == 0) value = f->labels[k].offset;
            }
        }
        set32(f, p->at, value);
    }
    free(text);
    *size = f->size;
    return f->b;
}

// The GEN8 chunk of bytecode 17, with a window of 640 x 480, two rooms in
// the room order, and 30 frames each second.
void ar_form_gen8(ARForm *f) {
    ar_form_chunk(f, "GEN8");
    ar_form_u32(f, 1 | 17 << 8);  // debug off, bytecode 17
    ar_form_str(f, "GAME");      // file name
    ar_form_str(f, "Default");   // config
    ar_form_u32(f, 100000);      // last object
    ar_form_u32(f, 10000000);    // last tile
    ar_form_u32(f, 0);           // game id
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // guid
    ar_form_str(f, "GAME");      // name
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // version
    ar_form_u32(f, 640);
    ar_form_u32(f, 480);
    ar_form_u32(f, 0);           // info
    ar_form_u32(f, 0);           // licence crc
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // licence md5
    ar_form_u32(f, 0);           // timestamp
    ar_form_u32(f, 0);
    ar_form_str(f, "The Game");  // display name
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // targets, classes
    ar_form_u32(f, 0);           // steam
    ar_form_u32(f, 0);           // debugger port
    ar_form_u32(f, 2);           // room order
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    for (int i = 0; i < 10; i++) ar_form_u32(f, 0);  // five random numbers
    ar_form_f32(f, 30);
}
