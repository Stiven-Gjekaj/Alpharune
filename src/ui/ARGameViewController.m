#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <QuartzCore/QuartzCore.h>
#import "ARGameViewController.h"
#import "ARAudioOutput.h"
#include <sys/stat.h>
#include "ar_gl.h"
#include "ar_host.h"
#include "ar_pads.h"
#include "ar_run.h"

// The files of the game: game.ios, the chapter directories, and mus/.
static NSString *const kBaseDir = @"/var/mobile/Media/Alpharune";

@interface ARGLView : UIView
@end

@implementation ARGLView
+ (Class)layerClass {
    return [CAEAGLLayer class];
}
@end

@implementation ARGameViewController {
    EAGLContext *_context;
    GLuint _framebuffer;
    GLuint _colorbuffer;
    GLint _width;
    GLint _height;
    CADisplayLink *_link;
    BOOL _started;
    UILabel *_message;
    ARHost *_host;
    ARPads _pads;
    NSMutableSet *_touches;
    uint8_t _keys[256];
    ARVertex *_padVertices;
    int _frameInterval;
}

- (void)loadView {
    ARGLView *view = [[ARGLView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    view.multipleTouchEnabled = YES;
    view.contentScaleFactor = [UIScreen mainScreen].scale;
    CAEAGLLayer *layer = (CAEAGLLayer *)view.layer;
    layer.opaque = YES;
    layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @NO,
                                 kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};
    self.view = view;
    _touches = [NSMutableSet set];
    _padVertices = malloc(sizeof(ARVertex) * 4096);
}

- (void)viewDidLoad {
    [super viewDidLoad];
    _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    [EAGLContext setCurrentContext:_context];
    glGenFramebuffers(1, &_framebuffer);
    glGenRenderbuffers(1, &_colorbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorbuffer);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    // The storage follows the size of the layer, which is landscape after
    // the rotation.
    [EAGLContext setCurrentContext:_context];
    glBindRenderbuffer(GL_RENDERBUFFER, _colorbuffer);
    [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)self.view.layer];
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_height);
    if (_width > _height && !_started) [self start];
}

- (void)start {
    _started = YES;
    ar_pads_layout(&_pads, _width, _height);
#ifdef AR_DEBUG
    // A debug build writes its messages to a file that ssh can read.
    NSString *log = [kBaseDir stringByAppendingPathComponent:@"log.txt"];
    freopen(log.fileSystemRepresentation, "w", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
#endif
    if (!ar_gl_init(_framebuffer)) {
        [self showMessage:@"The shader of the renderer does not compile."];
        return;
    }
    NSString *save = [kBaseDir stringByAppendingPathComponent:@"save"];
    mkdir(save.fileSystemRepresentation, 0755);
    ARHostConfig config;
    memset(&config, 0, sizeof(config));
    config.base_dir = kBaseDir.fileSystemRepresentation;
    config.save_dir = save.fileSystemRepresentation;
    config.gfx = ar_gl_gfx();
    char error[256];
    _host = ar_host_open(&config, error, sizeof(error));
    if (!_host) {
        NSString *text = [NSString stringWithFormat:
            @"Alpharune needs the files of your own copy of DELTARUNE.\n\n"
            @"Copy the contents of DELTARUNE.app/Contents/Resources to\n%@\n\n(%s)", kBaseDir, error];
        [self showMessage:text];
        return;
    }
    if (!ar_audio_output_start()) fprintf(stderr, "audio: the output does not start\n");
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
    [self setRate];
    [_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

// The display runs at 60 frames each second. A room of 30 takes each
// second display frame.
- (void)setRate {
    double speed = _host ? ar_run_speed(ar_host_run(_host)) : 30;
    int interval = speed > 0 ? (int)lround(60.0 / speed) : 2;
    if (interval < 1) interval = 1;
    if (interval != _frameInterval) {
        _frameInterval = interval;
        _link.frameInterval = interval;
    }
}

- (void)showMessage:(NSString *)text {
    _message = [[UILabel alloc] initWithFrame:CGRectInset(self.view.bounds, 24, 24)];
    _message.numberOfLines = 0;
    _message.textAlignment = NSTextAlignmentCenter;
    _message.backgroundColor = [UIColor clearColor];
    _message.textColor = [UIColor whiteColor];
    _message.font = [UIFont boldSystemFontOfSize:15];
    _message.text = text;
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:_message];
}

- (void)updateKeys {
    float xs[16], ys[16];
    int count = 0;
    CGFloat scale = self.view.contentScaleFactor;
    for (UITouch *touch in _touches) {
        if (count >= 16) break;
        CGPoint p = [touch locationInView:self.view];
        xs[count] = (float)(p.x * scale);
        ys[count] = (float)(p.y * scale);
        count++;
    }
    uint8_t keys[256];
    memcpy(keys, _keys, sizeof(keys));
    ar_pads_keys(&_pads, xs, ys, count, keys);
    ARRun *run = ar_host_run(_host);
    for (int k = 0; k < 256; k++) {
        if (keys[k] != _keys[k]) ar_run_key(run, k, keys[k]);
    }
    memcpy(_keys, keys, sizeof(keys));
}

- (void)frame:(CADisplayLink *)link {
    [EAGLContext setCurrentContext:_context];
    [self updateKeys];
#ifdef AR_DEBUG
    [self debugCommands];
#endif
#ifdef AR_DEBUG
    CFTimeInterval t0 = CACurrentMediaTime();
#endif
    if (!ar_host_frame(_host)) {
        [_link invalidate];
        _link = nil;
        const char *error = ar_host_error(_host);
        if (error[0]) {
            [self showMessage:@(error)];
        } else {
            // game_end: SpringBoard shows the home screen.
            exit(0);
        }
        return;
    }
    const char *notice = ar_host_notice(_host);
    if (notice) {
        UIAlertView *alert = [[UIAlertView alloc] initWithTitle:nil
                                                        message:@(notice)
                                                       delegate:nil
                                              cancelButtonTitle:@"OK"
                                              otherButtonTitles:nil];
        [alert show];
    }
    [self setRate];
    int w, h;
    int window = ar_run_window(ar_host_run(_host), &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ar_gl_present(window, _pads.game_x, _pads.game_y, _pads.game_w, _pads.game_h, _width, _height);
    int n = ar_pads_draw(&_pads, _keys, _padVertices);
    ar_gl_screen_triangles(_padVertices, n, _width, _height);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorbuffer);
    [_context presentRenderbuffer:GL_RENDERBUFFER];
#ifdef AR_DEBUG
    // The time of each frame, each 150 frames.
    static double total, worst;
    static int frames;
    double ms = (CACurrentMediaTime() - t0) * 1000;
    total += ms;
    if (ms > worst) worst = ms;
    if (++frames == 150) {
        fprintf(stderr, "speed: %.1f ms each frame, %.1f ms at most, room %s, %ld MB of textures\n", total / frames,
                worst, ar_run_room_name(ar_host_run(_host)), ar_gl_texture_bytes() >> 20);
        total = worst = 0;
        frames = 0;
    }
#endif
}

#ifdef AR_DEBUG
// The debug commands in /tmp/Alpharune.cmd, as scripts/tap.sh writes them:
// "key Z" taps a key, "hold Z 30" holds it for frames, and the other lines
// go to ar_run_debug.
- (void)debugCommands {
    static struct timespec last;
    static int holds[256];
    for (int k = 0; k < 256; k++) {
        if (holds[k] > 0 && --holds[k] == 0) ar_run_key(ar_host_run(_host), k, 0);
    }
    struct stat st;
    if (stat("/tmp/Alpharune.cmd", &st) != 0) return;
    if (st.st_mtimespec.tv_sec == last.tv_sec && st.st_mtimespec.tv_nsec == last.tv_nsec) return;
    last = st.st_mtimespec;
    FILE *f = fopen("/tmp/Alpharune.cmd", "r");
    if (!f) return;
    char line[256];
    ARRun *run = ar_host_run(_host);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char name[32];
        int frames = 1;
        if (sscanf(line, "key %31s", name) == 1 || sscanf(line, "hold %31s %d", name, &frames) >= 1) {
            int key = 0;
            if (strcmp(name, "UP") == 0) key = AR_VK_UP;
            else if (strcmp(name, "DOWN") == 0) key = AR_VK_DOWN;
            else if (strcmp(name, "LEFT") == 0) key = AR_VK_LEFT;
            else if (strcmp(name, "RIGHT") == 0) key = AR_VK_RIGHT;
            else if (strcmp(name, "ENTER") == 0) key = AR_VK_ENTER;
            else key = toupper((unsigned char)name[0]);
            ar_run_key(run, key, 1);
            holds[key & 255] = frames > 0 ? frames : 1;
        } else {
            ar_run_debug(run, line);
        }
    }
    fclose(f);
}
#endif

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    [_touches unionSet:touches];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    [_touches minusSet:touches];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    [_touches minusSet:touches];
}

- (void)setPaused:(BOOL)paused {
    _link.paused = paused;
    ar_audio_output_set_active(!paused);
    if (paused) [_touches removeAllObjects];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)shouldAutorotate {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
    return UIInterfaceOrientationLandscapeRight;
}

@end
