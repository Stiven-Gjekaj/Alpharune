// Tests of the test runner itself.

#import "ARTest.h"

AR_TEST(test_runner_compares_equal_objects) {
    AR_EQUAL(([NSString stringWithFormat:@"%d", 42]), @"42");
    AR_EQUAL_INT(6 * 7, 42);
    AR_EXPECT(YES);
}

AR_TEST(test_runner_makes_an_empty_temporary_directory) {
    NSString *first = ARTestTemporaryDirectory();
    NSString *second = ARTestTemporaryDirectory();
    BOOL isDirectory = NO;
    AR_EXPECT([[NSFileManager defaultManager] fileExistsAtPath:first isDirectory:&isDirectory] && isDirectory);
    AR_EXPECT(![first isEqualToString:second]);
    AR_EQUAL_INT([[NSFileManager defaultManager] contentsOfDirectoryAtPath:first error:NULL].count, 0);
}
