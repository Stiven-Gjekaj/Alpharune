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
//   --script FILE     Reads more keys from a file. Each line is
//                     "FRAME KEY [FRAMES]" or "FRAME debug COMMAND".
//                     A line can start with "+N" for N frames after the
//                     frame of the line before it. "#" starts a comment.
//   --serve FIFO      After the other frames, reads commands from the named
//                     pipe FIFO: "step N" runs N frames, "key K [N]" holds
//                     the key K for N frames from now, "shot FILE" saves the
//                     window, "surface N FILE" saves a surface of the
//                     game, "debug COMMAND", and "quit". Each key goes to
//                     FIFO.keys as a line of a script, and each answer goes
//                     to FIFO.out.
// The seed of the random numbers is 1, so that a script gives the same run
// each time, unless ALPHARUNE_SEED is set.
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
        const char *out = "build/frames", *save = NULL, *room = NULL, *data_name = "game.ios", *serve = NULL;
        static KeyPress keys[4096];
        __block int key_count = 0;
        static long debug_frames[256];
        static const char *debug_commands[256];
        __block int debug_count = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atol(argv[++i]);
            else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = atol(argv[++i]);
            else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
            else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = argv[++i];
            else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) room = argv[++i];
            else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data_name = argv[++i];
            else if (strcmp(argv[i], "--serve") == 0 && i + 1 < argc) serve = argv[++i];
            else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc && debug_count < 256) {
                char *colon = strchr(argv[i + 1], ':');
                if (colon) {
                    debug_frames[debug_count] = atol(argv[i + 1]);
                    debug_commands[debug_count++] = colon + 1;
                }
                i++;
            }
            else if ((strcmp(argv[i], "--key") == 0 || strcmp(argv[i], "--hold") == 0) && i + 1 < argc &&
                     key_count < 4096) {
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
            } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
                FILE *f = fopen(argv[++i], "r");
                if (!f) {
                    fprintf(stderr, "alpharune-mac: cannot read %s\n", argv[i]);
                    return 1;
                }
                char line[512];
                long last = 0;
                while (fgets(line, sizeof(line), f)) {
                    char *hash = strchr(line, '#');
                    if (hash) *hash = 0;
                    line[strcspn(line, "\r\n")] = 0;
                    char when[32], name[64];
                    int length = 1;
                    int n = sscanf(line, "%31s %63s %d", when, name, &length);
                    if (n < 2) continue;
                    long frame = when[0] == '+' ? last + atol(when + 1) : atol(when);
                    last = frame;
                    if (strcmp(name, "debug") == 0) {
                        const char *rest = strstr(line, "debug") + 6;
                        if (debug_count < 256) {
                            debug_frames[debug_count] = frame;
                            debug_commands[debug_count++] = strdup(rest);
                        }
                    } else if (key_count < 4096) {
                        keys[key_count].frame = frame;
                        keys[key_count].key = key_code(name);
                        keys[key_count].length = n >= 3 && length > 0 ? length : 1;
                        key_count++;
                    }
                }
                fclose(f);
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
        if (!getenv("ALPHARUNE_SEED")) setenv("ALPHARUNE_SEED", "1", 1);
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
        __block double busy = 0;
        __block long f = 0;
        char *dir_now = calloc(1, 1024);
        uint8_t *pixels = NULL;
        // Runs one frame with the keys of the script. Returns 0 at the end.
        int (^one_frame)(void) = ^int(void) {
            ARRun *run = ar_host_run(host);
            // The releases go first, and a key that another hold still
            // covers stays down.
            for (int k = 0; k < key_count; k++) {
                if (keys[k].frame + keys[k].length != f) continue;
                int held = 0;
                for (int j = 0; j < key_count; j++) {
                    if (j != k && keys[j].key == keys[k].key && keys[j].frame <= f &&
                        f < keys[j].frame + keys[j].length)
                        held = 1;
                }
                if (!held) ar_run_key(run, keys[k].key, 0);
            }
            for (int k = 0; k < key_count; k++) {
                if (keys[k].frame == f) ar_run_key(run, keys[k].key, 1);
            }
            for (int k = 0; k < debug_count; k++) {
                if (debug_frames[k] == f) ar_run_debug(run, debug_commands[k]);
            }
            if (room && f == 1) {
                char command[128];
                snprintf(command, sizeof(command), "room %s", room);
                ar_run_debug(run, command);
            }
            double s0 = [NSDate timeIntervalSinceReferenceDate];
            int going = ar_host_frame(host);
            busy += [NSDate timeIntervalSinceReferenceDate] - s0;
            if (!going) {
                fprintf(stderr, "end at frame %ld: %s\n", f + 1, ar_host_error(host)[0] ? ar_host_error(host) : "game_end");
                return 0;
            }
            if (strcmp(ar_host_dir(host), dir_now) != 0) {
                snprintf(dir_now, 1024, "%s", ar_host_dir(host));
                fprintf(stderr, "frame %ld: data in %s\n", f + 1, dir_now);
            }
            f++;
            return 1;
        };
        void (^save_surface)(const char *, int) = ^(const char *name, int surface) {
            ARRun *run = ar_host_run(host);
            int w, h;
            int handle = ar_run_surface(run, surface, &w, &h);
            if (!handle) return;
            uint8_t *px = malloc((size_t)w * (size_t)h * 4);
            glFinish();
            hc.gfx.read(hc.gfx.context, handle, w, h, px);
            save_png(name, px, w, h);
            free(px);
        };
        void (^save_shot)(const char *) = ^(const char *name) {
            ARRun *run = ar_host_run(host);
            int w, h;
            int window = ar_run_window(run, &w, &h);
            uint8_t *px = malloc((size_t)w * (size_t)h * 4);
            glFinish();
            hc.gfx.read(hc.gfx.context, window, w, h, px);
            save_png(name, px, w, h);
            free(px);
        };
        while (f < frames) {
            if (!one_frame()) break;
            if ((shot > 0 && f % shot == 0) || f == frames) {
                char name[2048];
                snprintf(name, sizeof(name), "%s/frame%05ld.png", out, f);
                save_shot(name);
                fprintf(stderr, "frame %ld: room %s\n", f, ar_run_room_name(ar_host_run(host)));
            }
        }
        if (serve) {
            char out_path[2048], keys_path[2048];
            snprintf(out_path, sizeof(out_path), "%s.out", serve);
            snprintf(keys_path, sizeof(keys_path), "%s.keys", serve);
            FILE *record = fopen(keys_path, "a");
            int quit = 0;
            while (!quit) {
                FILE *in = fopen(serve, "r");
                if (!in) break;
                char line[512];
                while (!quit && fgets(line, sizeof(line), in)) {
                    line[strcspn(line, "\r\n")] = 0;
                    char word[64], arg[448];
                    arg[0] = 0;
                    if (sscanf(line, "%63s %447[^\n]", word, arg) < 1) continue;
                    FILE *answer = fopen(out_path, "a");
                    if (strcmp(word, "step") == 0) {
                        long n = atol(arg);
                        for (long i = 0; i < n; i++) {
                            if (!one_frame()) {
                                quit = 1;
                                break;
                            }
                        }
                        fprintf(answer, "frame %ld room %s\n", f, ar_run_room_name(ar_host_run(host)));
                    } else if (strcmp(word, "key") == 0) {
                        char name[32];
                        int length = 1;
                        sscanf(arg, "%31s %d", name, &length);
                        if (key_count < 4096) {
                            keys[key_count].frame = f;
                            keys[key_count].key = key_code(name);
                            keys[key_count].length = length > 0 ? length : 1;
                            key_count++;
                        }
                        if (record) {
                            if (length > 1) fprintf(record, "%ld %s %d\n", f, name, length);
                            else fprintf(record, "%ld %s\n", f, name);
                            fflush(record);
                        }
                        fprintf(answer, "key %s at %ld\n", name, f);
                    } else if (strcmp(word, "shot") == 0) {
                        save_shot(arg);
                        fprintf(answer, "shot %s at %ld\n", arg, f);
                    } else if (strcmp(word, "surface") == 0) {
                        int index = 0;
                        char name[400];
                        if (sscanf(arg, "%d %399s", &index, name) == 2) save_surface(name, index);
                        fprintf(answer, "surface %d at %ld\n", index, f);
                    } else if (strcmp(word, "texel") == 0) {
                        int handle = 0, x = 0, y = 0;
                        sscanf(arg, "%d %d %d", &handle, &x, &y);
                        GLuint tex = ar_gl_texture(handle);
                        GLint w = 0, h = 0;
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
                        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
                        uint8_t *px = malloc((size_t)w * (size_t)h * 4 + 4);
                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                        uint8_t *q = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
                        fprintf(answer, "texel %d (%dx%d) %d,%d = %d %d %d %d\n", handle, w, h, x, y, q[0], q[1], q[2], q[3]);
                        free(px);
                    } else if (strcmp(word, "debug") == 0) {
                        ar_run_debug(ar_host_run(host), arg);
                        fprintf(answer, "debug at %ld\n", f);
                    } else if (strcmp(word, "quit") == 0) {
                        quit = 1;
                        fprintf(answer, "quit at %ld\n", f);
                    }
                    fclose(answer);
                }
                fclose(in);
            }
            if (record) fclose(record);
        }
        free(pixels);
        ar_host_close(host);
        fprintf(stderr, "%.2f ms each frame\n", busy * 1000 / (f > 0 ? f : 1));
        CGLSetCurrentContext(NULL);
        CGLDestroyContext(context);
    }
    return 0;
}
