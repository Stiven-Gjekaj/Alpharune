// alpharune-mac: runs a GameMaker data file on the Mac with no window. It
// draws with OpenGL in an offscreen context, saves frames as PNG files, and
// presses keys from a script. The tools and the tests of the runtime use it.
//
// Usage: alpharune-mac GAME_DIR [options]
//   --frames N        Run N frames (default 300).
//   --shot N          Save a PNG of the window each N frames (default 30).
//   --out DIR         The directory of the PNG files (default build/frames).
//   --save DIR        The directory for the files that the game writes
//                     (default: a new empty directory).
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
#include "ar_keys.h"
#include "ar_run.h"

// The keys of the script, and the host functions that it calls.
static void host_key(void *context, int key, int down) {
    ar_run_key(ar_host_run((ARHost *)context), key, down);
}

static void host_debug(void *context, const char *command) {
    ar_run_debug(ar_host_run((ARHost *)context), command);
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
        ARKeys *keys = ar_keys_new();
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atol(argv[++i]);
            else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = atol(argv[++i]);
            else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
            else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = argv[++i];
            else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) room = argv[++i];
            else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data_name = argv[++i];
            else if (strcmp(argv[i], "--serve") == 0 && i + 1 < argc) serve = argv[++i];
            else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc) {
                char *colon = strchr(argv[i + 1], ':');
                if (colon) ar_keys_add_debug(keys, atol(argv[i + 1]), colon + 1);
                i++;
            }
            else if ((strcmp(argv[i], "--key") == 0 || strcmp(argv[i], "--hold") == 0) && i + 1 < argc) {
                char name[32];
                long f = 0;
                int length = 1;
                if (sscanf(argv[i + 1], "%ld:%31[^:]:%d", &f, name, &length) >= 2) {
                    ar_keys_add_key(keys, f, ar_keys_key_code(name), length);
                }
                i++;
            } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
                NSString *text = [NSString stringWithContentsOfFile:@(argv[++i]) encoding:NSUTF8StringEncoding error:nil];
                if (!text) {
                    fprintf(stderr, "alpharune-mac: cannot read %s\n", argv[i]);
                    return 1;
                }
                ar_keys_parse(keys, text.UTF8String);
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
        ar_keys_sort(keys);
        if (!ar_gl_init(0)) {
            fprintf(stderr, "alpharune-mac: the shader does not compile\n");
            return 1;
        }

        // With no --save, each run starts with an empty directory of its
        // own, so that two runs of one script give the same frames.
        char save_dir[2048];
        if (save) {
            snprintf(save_dir, sizeof(save_dir), "%s", save);
            mkdir(save_dir, 0755);
        } else {
            snprintf(save_dir, sizeof(save_dir), "%s/alpharune-save-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
            if (!mkdtemp(save_dir)) {
                fprintf(stderr, "alpharune-mac: no directory for the save files\n");
                return 1;
            }
        }
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
            ar_keys_step(keys, f, host_key, host_debug, host);
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
            const char *notice = ar_host_notice(host);
            if (notice) fprintf(stderr, "frame %ld: notice: %s\n", f + 1, notice);
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
                        ar_keys_add_key(keys, f, ar_keys_key_code(name), length);
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
                        // A "set" changes the game, so the record keeps it.
                        if (record && strncmp(arg, "set ", 4) == 0) {
                            fprintf(record, "%ld debug %s\n", f, arg);
                            fflush(record);
                        }
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
