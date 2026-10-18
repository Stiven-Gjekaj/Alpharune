// ar_keys: a script of keys for a replay, as tests/play/chapter1.keys
// holds it. Each line is "FRAME KEY [FRAMES]", "+N KEY [FRAMES]" for N
// frames after the line before it, or "FRAME debug COMMAND". A "#" starts
// a comment. The Mac host and a debug build of the app both play it.

#ifndef AR_KEYS_H
#define AR_KEYS_H

typedef struct ARKeys ARKeys;

ARKeys *ar_keys_new(void);
void ar_keys_free(ARKeys *s);

// Adds the lines of text. Returns the count of the keys and the commands
// that it added.
int ar_keys_parse(ARKeys *s, const char *text);
// Adds a press of key at frame, held for length frames.
void ar_keys_add_key(ARKeys *s, long frame, int key, int length);
// Adds a debug command at frame.
void ar_keys_add_debug(ARKeys *s, long frame, const char *command);
// Puts the keys in the order of their frames. Call it after the parse and
// before the first step. A key that the host adds later must not be
// earlier than the keys before it.
void ar_keys_sort(ARKeys *s);

// The code of a key name: "Z", "UP", "ENTER", and so on. 0 when unknown.
int ar_keys_key_code(const char *name);

// Gives the events of frame to the host: first the releases, then the
// presses, then the debug commands. A key that another hold still covers
// stays down. The frames must come in order.
void ar_keys_step(ARKeys *s, long frame, void (*key)(void *context, int key, int down),
                    void (*debug)(void *context, const char *command), void *context);

// The frame of the last key or command.
long ar_keys_last_frame(const ARKeys *s);

#endif
