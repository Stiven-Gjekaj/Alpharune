// ar_gl: the ARGfx of the runtime with OpenGL ES 2.0 on the phone, or
// OpenGL 2.1 on the Mac for the tests and the tools.

#ifndef AR_GL_H
#define AR_GL_H

#include "ar_gfx.h"

// Makes the shader and the buffers. Call it with a current GL context.
// screen_framebuffer is the framebuffer of the screen (0 on the Mac).
// Returns 0 when the shader does not compile.
int ar_gl_init(unsigned int screen_framebuffer);

// The ARGfx that draws with GL.
ARGfx ar_gl_gfx(void);

// The GL texture of a handle, so that the host can draw a target.
unsigned int ar_gl_texture(int handle);

// Draws a target to the screen in the rectangle (x, y, w, h) of a screen of
// screen_w x screen_h pixels, with no filter.
void ar_gl_present(int handle, int x, int y, int w, int h, int screen_w, int screen_h);

// Draws plain triangles on the screen, for the pads of the host.
void ar_gl_screen_triangles(const ARVertex *vertices, int count, int screen_w, int screen_h);

// The bytes of texture memory that the textures and targets use now.
long ar_gl_texture_bytes(void);

#endif
