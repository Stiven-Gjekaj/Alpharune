// ARTest: a small test runner that operates on macOS and on iOS 6.
// Each test is a function that AR_TEST makes and registers at load time.

#import <Foundation/Foundation.h>

typedef void (*ARTestFunction)(void);

void ARTestRegister(const char *name, ARTestFunction function);
void ARTestFail(const char *file, int line, NSString *message);
void ARTestSkip(void);

#define AR_TEST(name)                                                         \
    static void name(void);                                                   \
    __attribute__((constructor)) static void name##_register(void) {          \
        ARTestRegister(#name, name);                                          \
    }                                                                         \
    static void name(void)

#define AR_EXPECT(condition)                                                  \
    do {                                                                      \
        if (!(condition)) ARTestFail(__FILE__, __LINE__, @"expected: " #condition); \
    } while (0)

#define AR_EQUAL(actual, expected)                                            \
    do {                                                                      \
        id ar_a = (actual), ar_e = (expected);                                \
        if (!(ar_a == ar_e || [ar_a isEqual:ar_e]))                           \
            ARTestFail(__FILE__, __LINE__, [NSString stringWithFormat:        \
                @"%s is <%@>, expected <%@>", #actual, ar_a, ar_e]);           \
    } while (0)

#define AR_EQUAL_INT(actual, expected)                                        \
    do {                                                                      \
        long long ar_a = (long long)(actual), ar_e = (long long)(expected);   \
        if (ar_a != ar_e)                                                     \
            ARTestFail(__FILE__, __LINE__, [NSString stringWithFormat:        \
                @"%s is %lld, expected %lld", #actual, ar_a, ar_e]);           \
    } while (0)

// Runs the run loop until done is YES or the time is over.
BOOL ARTestWait(BOOL *done, NSTimeInterval seconds);

// Makes a new empty directory for one test and returns its path.
NSString *ARTestTemporaryDirectory(void);
