// ar_pads: the touch controls of the phone. A D-pad sends the arrow keys,
// and three round buttons send Z, X, and C. The game fills the middle of
// the screen at 4:3, and the controls lie over its sides.

#ifndef AR_PADS_H
#define AR_PADS_H

#include <stdint.h>
#include "ar_gfx.h"

typedef struct {
    float x, y, radius;
    int key;
    char label;
} ARPadButton;

typedef struct {
    int screen_w, screen_h;
    // The rectangle of the game on the screen.
    int game_x, game_y, game_w, game_h;
    float dpad_x, dpad_y, dpad_radius;
    ARPadButton buttons[3];
} ARPads;

// Places the game and the controls on a screen of w x h pixels, in
// landscape.
void ar_pads_layout(ARPads *pads, int w, int h);

// Sets keys[k] to 1 for each key that a touch holds, and to 0 for the
// other keys of the pads. The touches are in pixels of the screen.
void ar_pads_keys(const ARPads *pads, const float *xs, const float *ys, int count, uint8_t keys[256]);

// Writes the triangles of the controls, brighter for a key that is down.
// Returns the count of vertices. out must have room for 4096 vertices.
int ar_pads_draw(const ARPads *pads, const uint8_t keys[256], ARVertex *out);

#endif
