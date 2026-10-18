// ar_run: the runtime of a GameMaker game. It runs the rooms, the
// instances, and their events with ar_vm, and draws with the ARGfx of the
// host.

#ifndef AR_RUN_H
#define AR_RUN_H

#include <stddef.h>
#include "ar_data.h"
#include "ar_gfx.h"

typedef struct ARRun ARRun;

// The audio of the host. The runtime mixes the sound itself and asks the
// host only to start and stop the output. The host may leave it all NULL.
typedef struct {
    void *context;
    // Starts the output. The host then calls ar_run_mix from its own thread.
    void (*start)(void *context, int rate);
} ARAudioHost;

typedef struct {
    // The directory of the data file. working_directory of GML names it,
    // and the game reads files such as "lang/lang_en.json" from it.
    const char *game_dir;
    // The directory for the files that the game writes.
    const char *save_dir;
    // The directory of the shared music ("mus/"), when it is not game_dir.
    const char *music_dir;
    // The command line of the game, as parameter_string gives it: the path
    // of the program, then the parameters. NULL for none.
    const char *const *parameters;
    int parameter_count;
    ARGfx gfx;
    ARAudioHost audio;
} ARRunConfig;

// The keys of GML (vk_*). A letter is its upper case code.
enum {
    AR_VK_NOKEY = 0,
    AR_VK_ANYKEY = 1,
    AR_VK_BACKSPACE = 8,
    AR_VK_TAB = 9,
    AR_VK_ENTER = 13,
    AR_VK_SHIFT = 16,
    AR_VK_CONTROL = 17,
    AR_VK_ALT = 18,
    AR_VK_ESCAPE = 27,
    AR_VK_SPACE = 32,
    AR_VK_LEFT = 37,
    AR_VK_UP = 38,
    AR_VK_RIGHT = 39,
    AR_VK_DOWN = 40,
    AR_VK_F1 = 112
};

// Makes the runtime of a data file that ar_data_read read. data must stay
// in memory. Returns NULL and writes error when it fails.
ARRun *ar_run_new(const ARData *data, const ARRunConfig *config, char *error, size_t size);
void ar_run_free(ARRun *run);

// Runs the global scripts, the game start, and the first room.
void ar_run_start(ARRun *run);

// Runs one frame of the game: the step, then the draw into the window
// target. Returns 0 when the game asks to end.
int ar_run_frame(ARRun *run);

// The frames each second of the current room.
double ar_run_speed(ARRun *run);
// The target with the image of the window, and its size.
int ar_run_window(ARRun *run, int *width, int *height);

// A key goes down or up.
void ar_run_key(ARRun *run, int key, int down);

// Mixes the audio: frames of stereo 16-bit samples.
void ar_run_mix(ARRun *run, int16_t *out, int frames);

// A request of game_change: the directory of the next data file, relative
// to the parent of game_dir, and its parameters. Returns 0 when there is none.
int ar_run_change_request(ARRun *run, char *dir, size_t dir_size, char *params, size_t params_size);
// A request of game_restart.
int ar_run_restart_request(ARRun *run);

// The count of frames since the start.
long ar_run_frame_count(ARRun *run);

// The name of the current room.
const char *ar_run_room_name(ARRun *run);

// Runs a debug command, such as "room room_town" or "goto 120 80".
void ar_run_debug(ARRun *run, const char *command);

#endif
