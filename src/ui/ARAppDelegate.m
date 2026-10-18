#import "ARAppDelegate.h"
#import "ARGameViewController.h"

@implementation ARAppDelegate {
    ARGameViewController *_game;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    [application setStatusBarHidden:YES];
    application.idleTimerDisabled = YES;
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    _game = [[ARGameViewController alloc] init];
    self.window.rootViewController = _game;
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
    // iOS stops the app when it draws with OpenGL in the background.
    [_game setPaused:YES];
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    [_game setPaused:NO];
}

@end
