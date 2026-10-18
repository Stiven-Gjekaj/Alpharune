// alpharune-mac: runs a GameMaker data file on the Mac with no window. It
// draws with OpenGL in an offscreen context, saves frames as PNG files, and
// presses keys from a script. The tools and the tests of the runtime use it.
//
// Usage: alpharune-mac GAME_DIR [options]
//   --frames N        Run N frames (default 300).
//   --shot N          Save a PNG of the window each N frames (default 30).
//   --out DIR         The directory of the PNG files (default build/frames).
//   --save DIR        The directory for the files that the game writes.
//   --key F:NAME      Press the key NAME at frame F, for one frame.
//   --hold F:NAME:N   Hold the key NAME from frame F for N frames.
//   --room NAME       Go to a room after the first frame.
//   --data FILE       The data file in GAME_DIR (default game.ios).
//   --debug F:COMMAND Runs a debug command of ar_run_debug at frame F.
// The key names are Z, X, C, ENTER, SHIFT, ESCAPE, UP, DOWN, LEFT, RIGHT, or
// a letter.

#define GL_SILENCE_DEPRECATION 1
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ar_data.h"
#include "ar_gl.h"
#include "ar_host.h"
#include "ar_run.h"

typedef struct {
    long frame;
    int key;
    int length;
} KeyPress;

static int key_code(const char *name) {
    if (strcmp(name, "ENTER") == 0) return AR_VK_ENTER;
    if (strcmp(name, "SHIFT") == 0) return AR_VK_SHIFT;
    if (strcmp(name, "ESCAPE") == 0) return AR_VK_ESCAPE;
    if (strcmp(name, "UP") == 0) return AR_VK_UP;
    if (strcmp(name, "DOWN") == 0) return AR_VK_DOWN;
    if (strcmp(name, "LEFT") == 0) return AR_VK_LEFT;
    if (strcmp(name, "RIGHT") == 0) return AR_VK_RIGHT;
    if (strcmp(name, "SPACE") == 0) return AR_VK_SPACE;
    if (strlen(name) == 1) return toupper((unsigned char)name[0]);
    return 0;
}

static int save_png(const char *path, const uint8_t *rgba, int w, int h) {
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate((void *)rgba, (size_t)w, (size_t)h, 8, (size_t)w * 4, space,
                                                 (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGImageRef image = CGBitmapContextCreateImage(context);
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(dest, image, NULL);
    int ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return ok;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 2) {
            fprintf(stderr, "usage: alpharune-mac GAME_DIR [options]\n");
            return 2;
        }
        const char *game_dir = argv[1];
        long frames = 300, shot = 30;
        const char *out = "build/frames", *save = NULL, *room = NULL, *data_name = "game.ios";
        KeyPress keys[256];
        int key_count = 0;
        long debug_frames[32];
        const char *debug_commands[32];
        int debug_count = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atol(argv[++i]);
            else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = atol(argv[++i]);
            else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
            else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = argv[++i];
            else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) room = argv[++i];
            else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data_name = argv[++i];
            else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc && debug_count < 32) {
                char *colon = strchr(argv[i + 1], ':');
                if (colon) {
                    debug_frames[debug_count] = atol(argv[i + 1]);
                    debug_commands[debug_count++] = colon + 1;
                }
                i++;
            }
            else if ((strcmp(argv[i], "--key") == 0 || strcmp(argv[i], "--hold") == 0) && i + 1 < argc &&
                     key_count < 256) {
                char name[32];
                long f = 0;
                int length = 1;
                if (sscanf(argv[i + 1], "%ld:%31[^:]:%d", &f, name, &length) >= 2) {
                    keys[key_count].frame = f;
                    keys[key_count].key = key_code(name);
                    keys[key_count].length = length > 0 ? length : 1;
                    key_count++;
                }
                i++;
            } else {
                fprintf(stderr, "alpharune-mac: unknown option %s\n", argv[i]);
                return 2;
            }
        }

        CGLPixelFormatAttribute attributes[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,
                                                kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
        CGLPixelFormatObj format;
        GLint count;
        if (CGLChoosePixelFormat(attributes, &format, &count) != kCGLNoError || !format) {
            fprintf(stderr, "alpharune-mac: no OpenGL\n");
            return 1;
        }
        CGLContextObj context;
        CGLCreateContext(format, NULL, &context);
        CGLSetCurrentContext(context);
        if (!ar_gl_init(0)) {
            fprintf(stderr, "alpharune-mac: the shader does not compile\n");
            return 1;
        }

        char save_dir[2048];
        snprintf(save_dir, sizeof(save_dir), "%s", save ? save : "build/save");
        mkdir(save_dir, 0755);
        mkdir(out, 0755);
        ARHostConfig hc;
        memset(&hc, 0, sizeof(hc));
        hc.base_dir = game_dir;
        hc.save_dir = save_dir;
        hc.data_name = data_name;
        hc.gfx = ar_gl_gfx();
        char error[256];
        double t0 = [NSDate timeIntervalSinceReferenceDate];
        ARHost *host = ar_host_open(&hc, error, sizeof(error));
        if (!host) {
            fprintf(stderr, "alpharune-mac: %s\n", error);
            return 1;
        }
        fprintf(stderr, "start: %.3f s, room %s\n", [NSDate timeIntervalSinceReferenceDate] - t0,
                ar_run_room_name(ar_host_run(host)));
        double busy = 0;
        long f = 0;
        char dir_now[1024] = "";
        uint8_t *pixels = NULL;
        for (; f < frames; f++) {
            ARRun *run = ar_host_run(host);
            for (int k = 0; k < key_count; k++) {
                if (keys[k].frame == f) ar_run_key(run, keys[k].key, 1);
                if (keys[k].frame + keys[k].length == f) ar_run_key(run, keys[k].key, 0);
            }
            for (int k = 0; k < debug_count; k++) {
                if (debug_frames[k] == f) ar_run_debug(run, debug_commands[k]);
            }
            if (room && f == 1) {
                char command[128];
                snprintf(command, sizeof(command), "room %s", room);
                ar_run_debug(run, command);
            }
            double s = [NSDate timeIntervalSinceReferenceDate];
            int going = ar_host_frame(host);
            busy += [NSDate timeIntervalSinceReferenceDate] - s;
            if (!going) {
                fprintf(stderr, "end at frame %ld: %s\n", f + 1, ar_host_error(host)[0] ? ar_host_error(host) : "game_end");
                break;
            }
            run = ar_host_run(host);
            if (strcmp(ar_host_dir(host), dir_now) != 0) {
                snprintf(dir_now, sizeof(dir_now), "%s", ar_host_dir(host));
                fprintf(stderr, "frame %ld: data in %s\n", f + 1, dir_now);
            }
            if ((shot > 0 && (f + 1) % shot == 0) || f + 1 == frames) {
                int w, h;
                int window = ar_run_window(run, &w, &h);
                pixels = realloc(pixels, (size_t)w * (size_t)h * 4);
                glFinish();
                hc.gfx.read(hc.gfx.context, window, w, h, pixels);
                char name[2048];
                snprintf(name, sizeof(name), "%s/frame%05ld.png", out, f + 1);
                save_png(name, pixels, w, h);
                fprintf(stderr, "frame %ld: room %s\n", f + 1, ar_run_room_name(run));
            }
        }
        free(pixels);
        ar_host_close(host);
        fprintf(stderr, "%.2f ms each frame\n", busy * 1000 / (f > 0 ? f : 1));
        CGLSetCurrentContext(NULL);
        CGLDestroyContext(context);
    }
    return 0;
}
