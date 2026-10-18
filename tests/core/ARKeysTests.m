// Tests of ar_keys: the parse of a script of keys, and the events of each
// frame.

#import "ARTest.h"
#include "ar_run.h"
#include "ar_keys.h"

// The events of the frames, as text: "f:+Z" for a press, "f:-Z" for a
// release, "f:!cmd" for a debug command.
typedef struct {
    NSMutableArray *events;
    long frame;
} Log;

static void on_key(void *context, int key, int down) {
    Log *log = context;
    [log->events addObject:[NSString stringWithFormat:@"%ld:%c%c", log->frame, down ? '+' : '-', (char)key]];
}

static void on_debug(void *context, const char *command) {
    Log *log = context;
    [log->events addObject:[NSString stringWithFormat:@"%ld:!%s", log->frame, command]];
}

static NSArray *play(ARKeys *s, long frames) {
    Log log = {[NSMutableArray array], 0};
    ar_keys_sort(s);
    for (long f = 0; f < frames; f++) {
        log.frame = f;
        ar_keys_step(s, f, on_key, on_debug, &log);
    }
    return log.events;
}

AR_TEST(test_keys_reads_frames_relative_frames_holds_and_comments) {
    ARKeys *s = ar_keys_new();
    int n = ar_keys_parse(s, "# a comment\n"
                               "2 Z\n"
                               "+3 X 2   # X for two frames\n"
                               "\n"
                               "9 debug set hp 1 900\n");
    AR_EQUAL_INT(n, 3);
    NSArray *events = play(s, 12);
    AR_EQUAL(events, (@[ @"2:+Z", @"3:-Z", @"5:+X", @"7:-X", @"9:!set hp 1 900" ]));
    AR_EQUAL_INT(ar_keys_last_frame(s), 9);
    ar_keys_free(s);
}

AR_TEST(test_keys_keeps_a_key_down_while_another_hold_covers_it) {
    ARKeys *s = ar_keys_new();
    // UP from 0 to 10, and a tap of UP at 4: the tap ends at 5, but the
    // long hold still covers 5, so UP stays down until 10.
    ar_keys_parse(s, "0 UP 10\n4 UP\n");
    NSArray *events = play(s, 12);
    NSString *up = [NSString stringWithFormat:@"%c", (char)AR_VK_UP];
    AR_EQUAL(events, (@[ [@"0:+" stringByAppendingString:up], [@"4:+" stringByAppendingString:up],
                         [@"10:-" stringByAppendingString:up] ]));
    ar_keys_free(s);
}

AR_TEST(test_keys_sorts_keys_that_the_file_gives_out_of_order) {
    ARKeys *s = ar_keys_new();
    ar_keys_parse(s, "6 X\n1 Z\n");
    NSArray *events = play(s, 8);
    AR_EQUAL(events, (@[ @"1:+Z", @"2:-Z", @"6:+X", @"7:-X" ]));
    ar_keys_free(s);
}

AR_TEST(test_keys_names_the_keys) {
    AR_EQUAL_INT(ar_keys_key_code("z"), 'Z');
    AR_EQUAL_INT(ar_keys_key_code("ENTER"), AR_VK_ENTER);
    AR_EQUAL_INT(ar_keys_key_code("NOSUCHKEY"), 0);
}
