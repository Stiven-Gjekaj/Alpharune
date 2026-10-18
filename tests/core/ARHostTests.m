// Tests of the host: the change from the chapter select to a chapter.

#import "ARTest.h"
#include "ARAsm.h"
#include "ar_host.h"

// A chapter select that asks for chapter2_mac in its first step.
static NSString *write_select(NSString *dir) {
    ARAsm *a = ar_asm_new();
    int obj = ar_asm_object(a, "obj_select", -1);
    ar_asm_event(a, obj, AR_EV_STEP, 0,
                 ar_asm_code(a, "gml_Object_obj_select_Step_0", 0,
                             "push.s \"\"\nconv.s.v\npush.s \"/chapter2_mac/\"\nconv.s.v\n"
                             "call game_change 2\npopz.v\n"));
    ar_asm_room(a, "PLACE_SELECT", 320, 240, -1);
    ar_asm_instance(a, obj, 0, 0, -1);
    size_t size;
    const uint8_t *bytes = ar_asm_finish(a, &size);
    NSString *path = [dir stringByAppendingPathComponent:@"game.ios"];
    [[NSData dataWithBytes:bytes length:size] writeToFile:path atomically:YES];
    ar_asm_free(a);
    return path;
}

AR_TEST(test_host_opens_the_select_again_for_a_chapter_with_no_data) {
    NSString *dir = ARTestTemporaryDirectory();
    write_select(dir);
    ARHostConfig config;
    memset(&config, 0, sizeof(config));
    config.base_dir = dir.fileSystemRepresentation;
    config.save_dir = [dir stringByAppendingPathComponent:@"save"].fileSystemRepresentation;
    char error[256];
    ARHost *host = ar_host_open(&config, error, sizeof(error));
    AR_EXPECT(host != NULL);
    if (!host) return;
    AR_EXPECT(ar_host_notice(host) == NULL);
    AR_EXPECT(ar_host_frame(host));
    AR_EXPECT(ar_host_frame(host));
    const char *notice = ar_host_notice(host);
    AR_EQUAL(@(notice ? notice : ""), @"Chapter 2 comes in a later version of Alpharune.");
    AR_EQUAL(@(ar_host_dir(host)), dir);
    AR_EXPECT(ar_host_notice(host) == NULL);
    ar_host_close(host);
}
