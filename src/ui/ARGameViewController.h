#import <UIKit/UIKit.h>

// Shows the game in an OpenGL ES 2.0 view, in landscape, with the touch
// controls over its sides.
@interface ARGameViewController : UIViewController

// Stops the frames while the app is in the background.
- (void)setPaused:(BOOL)paused;

@end
