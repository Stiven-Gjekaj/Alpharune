// ARForm: builds a small GameMaker data file in memory for a test.
//
// A test adds chunks with words, strings, and pointers. A pointer to a
// label gets its file offset when the test calls ar_form_finish.

#ifndef AR_FORM_H
#define AR_FORM_H

#include <stddef.h>
#include <stdint.h>

typedef struct ARForm ARForm;

ARForm *ar_form_new(void);
void ar_form_free(ARForm *form);

// Starts a chunk. The chunk ends at the next ar_form_chunk or at the end.
void ar_form_chunk(ARForm *form, const char *name);

void ar_form_u32(ARForm *form, uint32_t value);
void ar_form_f32(ARForm *form, float value);
void ar_form_u16(ARForm *form, uint16_t value);
void ar_form_bytes(ARForm *form, const void *bytes, size_t size);

// Puts the file offset of a label, as a word. A label is a name of the
// test, for example "sprite0".
void ar_form_ptr(ARForm *form, const char *label);
// Puts the file offset of the text of a string, as a word. The string goes
// into the STRG chunk.
void ar_form_str(ARForm *form, const char *text);
// The index of a string in the STRG chunk. Adds the string when it is new.
int ar_form_string_index(ARForm *form, const char *text);
// Adds a GEN8 chunk of bytecode 17, with a window of 640 x 480, the room
// order 1, 0, and 30 frames each second.
void ar_form_gen8(ARForm *form);
// Marks the current offset with a label.
void ar_form_label(ARForm *form, const char *label);
// The current file offset.
uint32_t ar_form_offset(ARForm *form);

// Adds the STRG chunk, sets each pointer, and returns the bytes. The form
// keeps the bytes until ar_form_free.
const uint8_t *ar_form_finish(ARForm *form, size_t *size);

#endif
