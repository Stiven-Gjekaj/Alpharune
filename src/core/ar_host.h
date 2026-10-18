// ar_host: the part of a host that the Mac and the phone share. It maps a
// data file into memory, makes the runtime, and follows game_change from
// the chapter select into a chapter and back.

#ifndef AR_HOST_H
#define AR_HOST_H

#include "ar_run.h"

typedef struct ARHost ARHost;

typedef struct {
    // The directory with game.ios, the chapter directories, and mus/.
    const char *base_dir;
    const char *save_dir;
    const char *data_name;  // "game.ios" when NULL.
    ARGfx gfx;
    ARAudioHost audio;
} ARHostConfig;

// Opens the chapter select in base_dir. Returns NULL and writes error when
// it cannot.
ARHost *ar_host_open(const ARHostConfig *config, char *error, size_t size);
void ar_host_close(ARHost *host);

// Runs one frame. After a game_change or a game_restart, the next frame
// runs the next data file. Returns 0 when the game ends or when the next
// data file does not open (ar_host_error tells why).
int ar_host_frame(ARHost *host);

ARRun *ar_host_run(ARHost *host);
const char *ar_host_error(ARHost *host);
// A message for the player, once, or NULL: for example that the chosen
// chapter comes in a later version. The chapter select runs again then.
const char *ar_host_notice(ARHost *host);
// The directory of the current data file.
const char *ar_host_dir(ARHost *host);

#endif
