// Tests of ar_texture with small images that the tests encode.

#import "ARTest.h"
#include <bzlib.h>
#include "ar_texture.h"

// Makes a "fioq" image of w x h from the codes in body.
static NSData *qoi(int w, int h, const uint8_t *body, size_t length) {
    NSMutableData *d = [NSMutableData data];
    [d appendBytes:"fioq" length:4];
    uint8_t head[8] = {(uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8),
                       (uint8_t)length, (uint8_t)(length >> 8), (uint8_t)(length >> 16), (uint8_t)(length >> 24)};
    [d appendBytes:head length:8];
    [d appendBytes:body length:length];
    return d;
}

static uint32_t pixel(const uint8_t *pixels, int i) {
    const uint8_t *p = pixels + 4 * i;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

AR_TEST(test_texture_decodes_colors_runs_and_the_index) {
    const uint8_t body[] = {
        0xFF, 10, 20, 30, 255,  // a full color: 0a141eff
        0x41,                   // this color and one more
        0xF8, 200,              // a new red: c8141eff
        0x00 | 63,             // the first color again: (10 ^ 20 ^ 30 ^ 255) & 63
    };
    NSData *d = qoi(5, 1, body, sizeof(body));
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_decode_qoi(d.bytes, d.length, &w, &h);
    AR_EXPECT(pixels != NULL);
    AR_EQUAL_INT(w, 5);
    AR_EQUAL_INT(h, 1);
    AR_EQUAL_INT(pixel(pixels, 0), 0x0a141eff);
    AR_EQUAL_INT(pixel(pixels, 1), 0x0a141eff);
    AR_EQUAL_INT(pixel(pixels, 2), 0x0a141eff);
    AR_EQUAL_INT(pixel(pixels, 3), 0xc8141eff);
    AR_EQUAL_INT(pixel(pixels, 4), 0x0a141eff);
    free(pixels);
}

AR_TEST(test_texture_decodes_the_three_differences) {
    const uint8_t body[] = {
        0xFF, 100, 100, 100, 100,
        0x80 | 3 << 4 | 1 << 2 | 2,  // +1, -1, +0
        0xC0 | 20, 0x9 << 4 | 7,     // +4, +1, -1
        0xE0 | 9, 0x80 | 15 << 2 | 2, 3 << 5 | 18,  // +3, -1, +3, +2
        0x5F,                        // a run of 32
        0x60, 0x00,                  // a run of 33
    };
    NSData *d = qoi(4 + 32 + 33, 1, body, sizeof(body));
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_decode_qoi(d.bytes, d.length, &w, &h);
    AR_EXPECT(pixels != NULL);
    AR_EQUAL_INT(pixel(pixels, 1), 0x65636464);
    AR_EQUAL_INT(pixel(pixels, 2), 0x69646364);
    AR_EQUAL_INT(pixel(pixels, 3), 0x6c636666);
    AR_EQUAL_INT(pixel(pixels, 68), 0x6c636666);
    free(pixels);
}

AR_TEST(test_texture_fills_the_rest_when_the_stream_stops) {
    const uint8_t body[] = {0xFF, 1, 2, 3, 4};
    NSData *d = qoi(2, 2, body, sizeof(body));
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_decode_qoi(d.bytes, d.length, &w, &h);
    AR_EXPECT(pixels != NULL);
    AR_EQUAL_INT(pixel(pixels, 3), 0x01020304);
    free(pixels);
}

AR_TEST(test_texture_refuses_a_length_past_the_end) {
    const uint8_t body[] = {0xFF, 1, 2, 3, 4};
    NSMutableData *d = [qoi(1, 1, body, sizeof(body)) mutableCopy];
    ((uint8_t *)d.mutableBytes)[8] = 200;
    int w, h;
    AR_EXPECT(ar_texture_decode_qoi(d.bytes, d.length, &w, &h) == NULL);
    AR_EXPECT(ar_texture_decode((const uint8_t *)"PNG?", 4, &w, &h) == NULL);
}

static NSData *bzip(NSData *image, BOOL withLength) {
    unsigned int size = (unsigned int)image.length * 2 + 600;
    NSMutableData *packed = [NSMutableData dataWithLength:size];
    int status = BZ2_bzBuffToBuffCompress(packed.mutableBytes, &size, (char *)image.bytes,
                                          (unsigned int)image.length, 9, 0, 0);
    if (status != BZ_OK) return nil;
    packed.length = size;
    const uint8_t *q = image.bytes;
    NSMutableData *d = [NSMutableData data];
    [d appendBytes:"2zoq" length:4];
    [d appendBytes:q + 4 length:4];
    if (withLength) {
        uint32_t n = (uint32_t)image.length;
        uint8_t len[4] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
        [d appendBytes:len length:4];
    }
    [d appendData:packed];
    return d;
}

AR_TEST(test_texture_decodes_an_image_in_a_bzip2_stream) {
    const uint8_t body[] = {0xFF, 9, 8, 7, 6, 0x42};
    NSData *image = qoi(4, 1, body, sizeof(body));
    for (int withLength = 0; withLength < 2; withLength++) {
        NSData *d = bzip(image, withLength);
        int w = 0, h = 0;
        uint8_t *pixels = ar_texture_decode(d.bytes, d.length, &w, &h);
        AR_EXPECT(pixels != NULL);
        AR_EQUAL_INT(w, 4);
        AR_EQUAL_INT(pixel(pixels, 3), 0x09080706);
        free(pixels);
    }
}
