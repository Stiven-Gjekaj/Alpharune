#include "ar_pads.h"

#include <math.h>
#include <string.h>
#include "ar_run.h"

void ar_pads_layout(ARPads *p, int w, int h) {
    memset(p, 0, sizeof(*p));
    p->screen_w = w;
    p->screen_h = h;
    // The largest 4:3 rectangle in the middle.
    p->game_h = h;
    p->game_w = h * 4 / 3;
    if (p->game_w > w) {
        p->game_w = w;
        p->game_h = w * 3 / 4;
    }
    p->game_x = (w - p->game_w) / 2;
    p->game_y = (h - p->game_h) / 2;
    float u = h / 640.0f;  // The unit: one pixel of a 4S in landscape.
    p->dpad_radius = 100 * u;
    p->dpad_x = 115 * u;
    p->dpad_y = h - 125 * u;
    ARPadButton z = {w - 85 * u, h - 150 * u, 58 * u, 'Z', 'Z'};
    ARPadButton x = {w - 190 * u, h - 75 * u, 50 * u, 'X', 'X'};
    ARPadButton c = {w - 75 * u, h - 290 * u, 42 * u, 'C', 'C'};
    p->buttons[0] = z;
    p->buttons[1] = x;
    p->buttons[2] = c;
}

void ar_pads_keys(const ARPads *p, const float *xs, const float *ys, int count, uint8_t keys[256]) {
    keys[AR_VK_LEFT] = keys[AR_VK_RIGHT] = keys[AR_VK_UP] = keys[AR_VK_DOWN] = 0;
    for (int b = 0; b < 3; b++) keys[p->buttons[b].key] = 0;
    for (int i = 0; i < count; i++) {
        float dx = xs[i] - p->dpad_x, dy = ys[i] - p->dpad_y;
        float d = sqrtf(dx * dx + dy * dy);
        // The touch area of the D-pad is larger than its picture.
        if (d <= p->dpad_radius * 1.5f) {
            if (d < p->dpad_radius * 0.2f) continue;
            // Eight directions: each axis takes a cone of 135 degrees.
            float a = atan2f(-dy, dx) * 180.0f / (float)M_PI;
            if (a < 0) a += 360;
            if (a < 67.5f || a > 292.5f) keys[AR_VK_RIGHT] = 1;
            if (a > 22.5f && a < 157.5f) keys[AR_VK_UP] = 1;
            if (a > 112.5f && a < 247.5f) keys[AR_VK_LEFT] = 1;
            if (a > 202.5f && a < 337.5f) keys[AR_VK_DOWN] = 1;
            continue;
        }
        // The nearest button in reach.
        int best = -1;
        float best_d = 0;
        for (int b = 0; b < 3; b++) {
            const ARPadButton *btn = &p->buttons[b];
            float bx = xs[i] - btn->x, by = ys[i] - btn->y;
            float bd = sqrtf(bx * bx + by * by);
            if (bd <= btn->radius * 1.35f && (best < 0 || bd < best_d)) {
                best = b;
                best_d = bd;
            }
        }
        if (best >= 0) keys[p->buttons[best].key] = 1;
    }
}

static int put_tri(ARVertex *out, int n, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c) {
    if (n + 3 > 4096) return n;
    ARVertex v[3] = {{x1, y1, 0, 0, c}, {x2, y2, 0, 0, c}, {x3, y3, 0, 0, c}};
    memcpy(out + n, v, sizeof(v));
    return n + 3;
}

static int put_rect(ARVertex *out, int n, float x, float y, float w, float h, uint32_t c) {
    n = put_tri(out, n, x, y, x + w, y, x, y + h, c);
    return put_tri(out, n, x + w, y, x + w, y + h, x, y + h, c);
}

static int put_disc(ARVertex *out, int n, float cx, float cy, float r, uint32_t c) {
    const int segments = 28;
    for (int i = 0; i < segments; i++) {
        float a0 = 2 * (float)M_PI * i / segments, a1 = 2 * (float)M_PI * (i + 1) / segments;
        n = put_tri(out, n, cx, cy, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, c);
    }
    return n;
}

// Letters of 5 x 7 pixels, a row in each byte, for the labels.
static const uint8_t kLetters[3][7] = {
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},  // Z
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
};

static int put_letter(ARVertex *out, int n, char letter, float cx, float cy, float px, uint32_t c) {
    int index = letter == 'Z' ? 0 : letter == 'X' ? 1 : 2;
    float x0 = cx - 2.5f * px, y0 = cy - 3.5f * px;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (kLetters[index][row] & (0x10 >> col)) n = put_rect(out, n, x0 + col * px, y0 + row * px, px, px, c);
        }
    }
    return n;
}

// White with an alpha, as a vertex color.
static uint32_t white(float alpha) {
    uint32_t a = (uint32_t)(alpha * 255 + 0.5f);
    return 0xFFFFFFu | a << 24;
}

int ar_pads_draw(const ARPads *p, const uint8_t keys[256], ARVertex *out) {
    int n = 0;
    float r = p->dpad_radius, cx = p->dpad_x, cy = p->dpad_y;
    float arm = r * 0.36f;
    n = put_disc(out, n, cx, cy, r, white(0.10f));
    // The cross, and each arm brighter when its key is down.
    n = put_rect(out, n, cx - arm, cy - arm, 2 * arm, 2 * arm, white(0.22f));
    n = put_rect(out, n, cx - arm, cy - r * 0.9f, 2 * arm, r * 0.9f - arm, white(keys[AR_VK_UP] ? 0.55f : 0.22f));
    n = put_rect(out, n, cx - arm, cy + arm, 2 * arm, r * 0.9f - arm, white(keys[AR_VK_DOWN] ? 0.55f : 0.22f));
    n = put_rect(out, n, cx - r * 0.9f, cy - arm, r * 0.9f - arm, 2 * arm, white(keys[AR_VK_LEFT] ? 0.55f : 0.22f));
    n = put_rect(out, n, cx + arm, cy - arm, r * 0.9f - arm, 2 * arm, white(keys[AR_VK_RIGHT] ? 0.55f : 0.22f));
    for (int b = 0; b < 3; b++) {
        const ARPadButton *btn = &p->buttons[b];
        n = put_disc(out, n, btn->x, btn->y, btn->radius, white(keys[btn->key] ? 0.5f : 0.2f));
        n = put_letter(out, n, btn->label, btn->x, btn->y, btn->radius * 0.14f, white(0.75f));
    }
    return n;
}
