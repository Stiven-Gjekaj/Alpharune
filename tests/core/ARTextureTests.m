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
        // The fields of a difference are signed: 3 in 2 bits is -1.
        0x80 | 3 << 4 | 1 << 2 | 2,  // -1, +1, -2
        0xC0 | 20, 0x9 << 4 | 7,     // -12, -7, +7
        0xE0 | 9, 0x80 | 15 << 2 | 2, 3 << 5 | 18,  // -13, +15, -13, -14
        0x5F,                        // a run of 32
        0x60, 0x00,                  // a run of 33
    };
    NSData *d = qoi(4 + 32 + 33, 1, body, sizeof(body));
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_decode_qoi(d.bytes, d.length, &w, &h);
    AR_EXPECT(pixels != NULL);
    AR_EQUAL_INT(pixel(pixels, 1), 0x63656264);
    AR_EQUAL_INT(pixel(pixels, 2), 0x575e6964);
    AR_EQUAL_INT(pixel(pixels, 3), 0x4a6d5c56);
    AR_EQUAL_INT(pixel(pixels, 68), 0x4a6d5c56);
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

// A new directory in the temporary directory, for one test.
static NSString *temp_dir(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/ar-texture-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    return mkdtemp(path) ? @(path) : nil;
}

AR_TEST(test_texture_cache_writes_the_pixels_of_a_new_page) {
    const uint8_t body[] = {0xFE, 10, 20, 30};
    NSData *d = qoi(1, 1, body, sizeof(body));
    NSString *dir = temp_dir();
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_cached(dir.fileSystemRepresentation, d.bytes, d.length, &w, &h);
    AR_EXPECT(pixels != NULL);
    AR_EQUAL_INT(pixel(pixels, 0), 0x0A141EFFu);
    free(pixels);
    char path[1024];
    ar_texture_cache_path(dir.fileSystemRepresentation, d.bytes, d.length, path, sizeof(path));
    NSData *file = [NSData dataWithContentsOfFile:@(path)];
    AR_EQUAL_INT(file.length, 12 + 4);
    AR_EXPECT(memcmp(file.bytes, "ARTC", 4) == 0);
    [[NSFileManager defaultManager] removeItemAtPath:dir error:nil];
}

AR_TEST(test_texture_cache_reads_a_page_from_its_file) {
    const uint8_t body[] = {0xFE, 10, 20, 30};
    NSData *d = qoi(1, 1, body, sizeof(body));
    NSString *dir = temp_dir();
    // A cache file with other pixels than the page: the cache must use the
    // file and not decode.
    char path[1024];
    ar_texture_cache_path(dir.fileSystemRepresentation, d.bytes, d.length, path, sizeof(path));
    const uint8_t file[] = {'A', 'R', 'T', 'C', 2, 0, 0, 0, 1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    [[NSData dataWithBytes:file length:sizeof(file)] writeToFile:@(path) atomically:NO];
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_cached(dir.fileSystemRepresentation, d.bytes, d.length, &w, &h);
    AR_EQUAL_INT(w, 2);
    AR_EQUAL_INT(h, 1);
    AR_EQUAL_INT(pixel(pixels, 1), 0x05060708u);
    free(pixels);
    [[NSFileManager defaultManager] removeItemAtPath:dir error:nil];
}

AR_TEST(test_texture_cache_decodes_again_when_the_file_is_short) {
    const uint8_t body[] = {0xFE, 10, 20, 30};
    NSData *d = qoi(1, 1, body, sizeof(body));
    NSString *dir = temp_dir();
    char path[1024];
    ar_texture_cache_path(dir.fileSystemRepresentation, d.bytes, d.length, path, sizeof(path));
    const uint8_t file[] = {'A', 'R', 'T', 'C', 1, 0, 0, 0, 1, 0, 0, 0, 9};
    [[NSData dataWithBytes:file length:sizeof(file)] writeToFile:@(path) atomically:NO];
    int w = 0, h = 0;
    uint8_t *pixels = ar_texture_cached(dir.fileSystemRepresentation, d.bytes, d.length, &w, &h);
    AR_EQUAL_INT(pixel(pixels, 0), 0x0A141EFFu);
    free(pixels);
    [[NSFileManager defaultManager] removeItemAtPath:dir error:nil];
}
