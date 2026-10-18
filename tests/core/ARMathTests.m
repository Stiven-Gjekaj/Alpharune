// Tests of ar_math: the results must be the same bits on the Mac and on the
// phone, so each test compares the bits with a fixed value.

#import "ARTest.h"
#include <math.h>
#include <string.h>
#include "ar_math.h"

static uint64_t bits(double v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    return b;
}

// A hash of the results of f for many inputs, as the Mac and the phone
// must both give it.
static uint64_t sweep(double (*f)(double)) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < 20000; i++) {
        double x = (i - 10000) * 0.0137 + 0.000123 * (i % 97);
        uint64_t b = bits(f(x));
        for (int k = 0; k < 8; k++) h = (h ^ ((b >> (8 * k)) & 0xff)) * 1099511628211ULL;
    }
    return h;
}

static double sin_of(double x) { return ar_sin(x); }
static double cos_of(double x) { return ar_cos(x); }
static double atan2_of(double x) { return ar_atan2(x, 1.7); }
static double pow_of(double x) { return ar_pow(fabs(x) + 1, 1.37); }

AR_TEST(test_math_gives_fixed_bits) {
    AR_EQUAL_INT(bits(ar_sin(0.5)), 0x3FDEAEE8744B05F0ULL);
    AR_EQUAL_INT(bits(ar_cos(2.5)), 0xBFE9A2F7EF858B7DULL);
    AR_EQUAL_INT(bits(ar_atan2(1.0, 3.0)), 0x3FD4978FA3269EE1ULL);
    AR_EQUAL_INT(bits(ar_pow(2.5, 1.37)), 0x400C12570E081699ULL);
}

AR_TEST(test_math_gives_fixed_bits_for_many_inputs) {
    AR_EQUAL_INT(sweep(sin_of), 0xD1C44D42E7910B3FULL);
    AR_EQUAL_INT(sweep(cos_of), 0xFE5BA1E9BCFEFD4EULL);
    AR_EQUAL_INT(sweep(atan2_of), 0x1E31F42AB4D3919DULL);
    AR_EQUAL_INT(sweep(pow_of), 0x40FF5D10CDEA1E64ULL);
}

AR_TEST(test_math_is_near_the_c_library) {
    for (int i = -50; i <= 50; i++) {
        double x = i * 0.731;
        AR_EXPECT(fabs(ar_sin(x) - sin(x)) < 1e-15);
        AR_EXPECT(fabs(ar_cos(x) - cos(x)) < 1e-15);
        AR_EXPECT(fabs(ar_atan2(x, 2.0) - atan2(x, 2.0)) < 1e-15);
        AR_EXPECT(fabs(ar_exp(x / 10) - exp(x / 10)) < 1e-13 * exp(x / 10));
    }
    AR_EXPECT(fabs(ar_log2(8.0) - 3.0) < 1e-15);
    AR_EXPECT(fabs(ar_log10(1000.0) - 3.0) < 1e-15);
}
