#include "ar_gl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
// OpenGL 2.1 of the Mac has the framebuffer objects as an extension.
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#undef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER GL_FRAMEBUFFER_EXT
#undef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#undef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_EXT
#endif

typedef struct {
    GLuint texture;
    GLuint framebuffer;  // 0 for a texture that is not a target.
    int width, height;
    int used;
} Handle;

#define MAX_HANDLES 1024

static Handle gHandles[MAX_HANDLES];
static GLuint gProgram;
static GLuint gWhite;
static GLuint gBuffer;
static GLint gUniformScale, gUniformOffset, gUniformFog;
static GLuint gScreen;
static int gTarget = -1;
static int gBound = -2;
static int gBoundFilter = -1;
static int gFog = -1;
static long gTextureBytes;

static const char *kVertex =
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "attribute vec4 color;\n"
    "uniform vec2 scale;\n"
    "uniform vec2 offset;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    v_texcoord = texcoord;\n"
    "    v_color = color;\n"
    "    gl_Position = vec4(position * scale + offset, 0.0, 1.0);\n"
    "}\n";

static const char *kFragment =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D texture;\n"
    "uniform float fog;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    vec4 t = texture2D(texture, v_texcoord);\n"
    "    vec4 c = t * v_color;\n"
    "    gl_FragColor = mix(c, vec4(v_color.rgb, t.a * v_color.a), fog);\n"
    "}\n";

static GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "ar_gl: shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int ar_gl_init(unsigned int screen_framebuffer) {
    gScreen = screen_framebuffer;
    GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return 0;
    gProgram = glCreateProgram();
    glAttachShader(gProgram, vs);
    glAttachShader(gProgram, fs);
    glBindAttribLocation(gProgram, 0, "position");
    glBindAttribLocation(gProgram, 1, "texcoord");
    glBindAttribLocation(gProgram, 2, "color");
    glLinkProgram(gProgram);
    GLint ok = 0;
    glGetProgramiv(gProgram, GL_LINK_STATUS, &ok);
    if (!ok) return 0;
    glUseProgram(gProgram);
    gUniformScale = glGetUniformLocation(gProgram, "scale");
    gUniformOffset = glGetUniformLocation(gProgram, "offset");
    gUniformFog = glGetUniformLocation(gProgram, "fog");
    glUniform1i(glGetUniformLocation(gProgram, "texture"), 0);
    glUniform1f(gUniformFog, 0);
    gFog = 0;

    glGenBuffers(1, &gBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, gBuffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ARVertex), (void *)offsetof(ARVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ARVertex), (void *)offsetof(ARVertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ARVertex), (void *)offsetof(ARVertex, color));

    uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &gWhite);
    glBindTexture(GL_TEXTURE_2D, gWhite);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return 1;
}

static int new_handle(void) {
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (!gHandles[i].used) {
            memset(&gHandles[i], 0, sizeof(Handle));
            gHandles[i].used = 1;
            return i;
        }
    }
    return 0;
}

static void set_params(int filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int texture_new(void *context, const uint8_t *rgba, int width, int height) {
    (void)context;
    int h = new_handle();
    if (!h) return 0;
    Handle *t = &gHandles[h];
    glGenTextures(1, &t->texture);
    glBindTexture(GL_TEXTURE_2D, t->texture);
    set_params(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    t->width = width;
    t->height = height;
    gTextureBytes += (long)width * height * 4;
    gBound = -2;
    return h;
}

static void bind_target(int handle);

static int target_new(void *context, int width, int height) {
    (void)context;
    int h = new_handle();
    if (!h) return 0;
    Handle *t = &gHandles[h];
    glGenTextures(1, &t->texture);
    glBindTexture(GL_TEXTURE_2D, t->texture);
    set_params(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &t->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ar_gl: a target of %d x %d is not complete\n", width, height);
    }
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    t->width = width;
    t->height = height;
    gTextureBytes += (long)width * height * 4;
    gBound = -2;
    // Bind the current target again.
    int current = gTarget;
    gTarget = -1;
    if (current >= 0) bind_target(current);
    else glBindFramebuffer(GL_FRAMEBUFFER, gScreen);
    return h;
}

static void texture_free(void *context, int handle) {
    (void)context;
    if (handle <= 0 || handle >= MAX_HANDLES || !gHandles[handle].used) return;
    Handle *t = &gHandles[handle];
    if (t->framebuffer) glDeleteFramebuffers(1, &t->framebuffer);
    glDeleteTextures(1, &t->texture);
    gTextureBytes -= (long)t->width * t->height * 4;
    memset(t, 0, sizeof(*t));
    if (gTarget == handle) gTarget = -1;
    gBound = -2;
}

static int gTargetW, gTargetH;

static void bind_target(int handle) {
    if (handle > 0 && handle < MAX_HANDLES && gHandles[handle].used && gHandles[handle].framebuffer) {
        Handle *t = &gHandles[handle];
        glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
        glViewport(0, 0, t->width, t->height);
        // In a target, the row 0 of the texture is the top of the image, so
        // the y of the game goes up the NDC from -1.
        glUniform2f(gUniformScale, 2.0f / t->width, 2.0f / t->height);
        glUniform2f(gUniformOffset, -1, -1);
        gTargetW = t->width;
        gTargetH = t->height;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, gScreen);
        handle = 0;
    }
    gTarget = handle;
}

static void target_set(void *context, int handle, int width, int height) {
    (void)context;
    (void)width;
    (void)height;
    if (handle == gTarget) return;
    bind_target(handle);
}

static void clear(void *context, uint32_t rgba) {
    (void)context;
    glClearColor((rgba & 255) / 255.0f, ((rgba >> 8) & 255) / 255.0f, ((rgba >> 16) & 255) / 255.0f,
                 (rgba >> 24) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static GLenum factor(int f) {
    switch (f) {
    case 1: return GL_ZERO;
    case 2: return GL_ONE;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    case 11: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

static void blend(void *context, int enabled, int src, int dst, int src_alpha, int dst_alpha) {
    (void)context;
    if (!enabled) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    glBlendFuncSeparate(factor(src), factor(dst), factor(src_alpha), factor(dst_alpha));
}

static void triangles(void *context, int texture, int filter, int fog, const ARVertex *vertices, int count) {
    (void)context;
    if (count <= 0) return;
    if (texture != gBound || filter != gBoundFilter) {
        GLuint t = gWhite;
        if (texture > 0 && texture < MAX_HANDLES && gHandles[texture].used) t = gHandles[texture].texture;
        glBindTexture(GL_TEXTURE_2D, t);
        if (t != gWhite) set_params(filter);
        gBound = texture;
        gBoundFilter = filter;
    }
    if (fog != gFog) {
        glUniform1f(gUniformFog, fog ? 1.0f : 0.0f);
        gFog = fog;
    }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(ARVertex) * (size_t)count), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, count);
}

static void read_pixels(void *context, int handle, int width, int height, uint8_t *rgba) {
    (void)context;
    int previous = gTarget;
    bind_target(handle);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (previous != handle) {
        gTarget = -1;
        if (previous >= 0) bind_target(previous);
    }
}

ARGfx ar_gl_gfx(void) {
    ARGfx g;
    memset(&g, 0, sizeof(g));
    g.texture_new = texture_new;
    g.target_new = target_new;
    g.texture_free = texture_free;
    g.target_set = target_set;
    g.clear = clear;
    g.blend = blend;
    g.triangles = triangles;
    g.read = read_pixels;
    return g;
}

unsigned int ar_gl_texture(int handle) {
    if (handle <= 0 || handle >= MAX_HANDLES || !gHandles[handle].used) return 0;
    return gHandles[handle].texture;
}

long ar_gl_texture_bytes(void) {
    return gTextureBytes;
}

static void screen_mode(int screen_w, int screen_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, gScreen);
    gTarget = -1;
    glViewport(0, 0, screen_w, screen_h);
    // On the screen, the y of the game goes down from the top.
    glUniform2f(gUniformScale, 2.0f / screen_w, -2.0f / screen_h);
    glUniform2f(gUniformOffset, -1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (gFog != 0) {
        glUniform1f(gUniformFog, 0);
        gFog = 0;
    }
}

void ar_gl_present(int handle, int x, int y, int w, int h, int screen_w, int screen_h) {
    screen_mode(screen_w, screen_h);
    uint32_t white = 0xFFFFFFFF;
    ARVertex v[6] = {
        {(float)x, (float)y, 0, 0, white},           {(float)(x + w), (float)y, 1, 0, white},
        {(float)x, (float)(y + h), 0, 1, white},     {(float)x, (float)(y + h), 0, 1, white},
        {(float)(x + w), (float)y, 1, 0, white},     {(float)(x + w), (float)(y + h), 1, 1, white},
    };
    glDisable(GL_BLEND);
    triangles(NULL, handle, 0, 0, v, 6);
    glEnable(GL_BLEND);
}

void ar_gl_screen_triangles(const ARVertex *vertices, int count, int screen_w, int screen_h) {
    screen_mode(screen_w, screen_h);
    triangles(NULL, 0, 0, 0, vertices, count);
}
