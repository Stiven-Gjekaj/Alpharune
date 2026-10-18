// ar_gfx: the interface between the runtime and the renderer of a host.
//
// The runtime makes triangles in the pixels of the current target, with
// (0, 0) at the top left. The host draws them with OpenGL. A texture and a
// target share one space of handles, so that a surface can be a texture.

#ifndef AR_GFX_H
#define AR_GFX_H

#include <stdint.h>

typedef struct {
    float x, y;
    float u, v;
    uint32_t color;  // RGBA, red in the low byte, not premultiplied.
} ARVertex;

enum {
    AR_BLEND_ZERO = 1,
    AR_BLEND_ONE,
    AR_BLEND_SRC_COLOR,
    AR_BLEND_INV_SRC_COLOR,
    AR_BLEND_SRC_ALPHA,
    AR_BLEND_INV_SRC_ALPHA,
    AR_BLEND_DEST_ALPHA,
    AR_BLEND_INV_DEST_ALPHA,
    AR_BLEND_DEST_COLOR,
    AR_BLEND_INV_DEST_COLOR,
    AR_BLEND_SRC_ALPHA_SATURATE
};

typedef struct ARGfx {
    void *context;
    // Makes a texture of RGBA pixels. Returns a handle above 0, or 0.
    int (*texture_new)(void *context, const uint8_t *rgba, int width, int height);
    // Makes a target that can also be a texture. Returns a handle, or 0.
    int (*target_new)(void *context, int width, int height);
    void (*texture_free)(void *context, int handle);
    // Draws into a target, or into the screen for handle 0.
    void (*target_set)(void *context, int handle, int width, int height);
    void (*clear)(void *context, uint32_t rgba);
    // The blend of the next triangles. enabled 0 writes the pixels as they are.
    void (*blend)(void *context, int enabled, int src, int dst, int src_alpha, int dst_alpha);
    // Draws triangles with a texture, or with no texture for handle 0.
    // filter 1 makes the texture smooth. fog 1 takes the color from the
    // vertex and only the alpha from the texture.
    void (*triangles)(void *context, int texture, int filter, int fog, const ARVertex *vertices, int count);
    // Reads the pixels of a target into rgba (width * height * 4 bytes).
    // The host can leave it NULL.
    void (*read)(void *context, int handle, int width, int height, uint8_t *rgba);
} ARGfx;

#endif
