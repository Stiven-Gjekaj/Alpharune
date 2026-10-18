#include "ar_texture.h"

#include <bzlib.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

uint8_t *ar_texture_decode_qoi(const uint8_t *bytes, size_t size, int *width, int *height) {
    if (size < 12 || memcmp(bytes, "fioq", 4) != 0) return NULL;
    int w = le16(bytes + 4), h = le16(bytes + 6);
    size_t length = le32(bytes + 8);
    if (w == 0 || h == 0 || length > size - 12) return NULL;
    uint8_t *out = malloc((size_t)w * (size_t)h * 4);
    if (!out) return NULL;

    const uint8_t *p = bytes + 12, *end = p + length;
    uint8_t index[64][4];
    memset(index, 0, sizeof(index));
    int r = 0, g = 0, b = 0, a = 255;
    int run = 0;
    size_t count = (size_t)w * (size_t)h;
    uint8_t *o = out;
    for (size_t i = 0; i < count; i++, o += 4) {
        if (run > 0) {
            run--;
        } else if (p < end) {
            int b1 = *p++;
            if ((b1 & 0xC0) == 0x00) {
                // An index into the table of recent colors.
                r = index[b1][0];
                g = index[b1][1];
                b = index[b1][2];
                a = index[b1][3];
            } else if ((b1 & 0xE0) == 0x40) {
                // A run of the last color: this pixel and up to 31 more.
                run = b1 & 0x1F;
            } else if ((b1 & 0xE0) == 0x60) {
                if (p >= end) break;
                run = (((b1 & 0x1F) << 8) | *p++) + 32;
            } else if ((b1 & 0xC0) == 0x80) {
                r += ((b1 >> 4) & 3) - 2;
                g += ((b1 >> 2) & 3) - 2;
                b += (b1 & 3) - 2;
            } else if ((b1 & 0xE0) == 0xC0) {
                if (p >= end) break;
                int b2 = *p++;
                r += (b1 & 31) - 16;
                g += (b2 >> 4) - 8;
                b += (b2 & 15) - 8;
            } else if ((b1 & 0xF0) == 0xE0) {
                if (end - p < 2) break;
                int b2 = p[0], b3 = p[1];
                p += 2;
                r += (((b1 & 15) << 1) | (b2 >> 7)) - 16;
                g += ((b2 & 124) >> 2) - 16;
                b += (((b2 & 3) << 3) | ((b3 & 224) >> 5)) - 16;
                a += (b3 & 31) - 16;
            } else {
                // A new value for each channel with a bit in b1.
                if (b1 & 8) r = p < end ? *p++ : r;
                if (b1 & 4) g = p < end ? *p++ : g;
                if (b1 & 2) b = p < end ? *p++ : b;
                if (b1 & 1) a = p < end ? *p++ : a;
            }
            r &= 255;
            g &= 255;
            b &= 255;
            a &= 255;
            uint8_t *slot = index[(r ^ g ^ b ^ a) & 63];
            slot[0] = (uint8_t)r;
            slot[1] = (uint8_t)g;
            slot[2] = (uint8_t)b;
            slot[3] = (uint8_t)a;
        }
        o[0] = (uint8_t)r;
        o[1] = (uint8_t)g;
        o[2] = (uint8_t)b;
        o[3] = (uint8_t)a;
    }
    // A stream that stops early leaves the last color in the rest.
    for (; o < out + count * 4; o += 4) {
        o[0] = (uint8_t)r;
        o[1] = (uint8_t)g;
        o[2] = (uint8_t)b;
        o[3] = (uint8_t)a;
    }
    *width = w;
    *height = h;
    return out;
}

uint8_t *ar_texture_decode(const uint8_t *bytes, size_t size, int *width, int *height) {
    if (size >= 4 && memcmp(bytes, "fioq", 4) == 0) return ar_texture_decode_qoi(bytes, size, width, height);
    if (size < 12 || memcmp(bytes, "2zoq", 4) != 0) return NULL;
    // The header: "2zoq", the width, the height, and since GameMaker 2022.5
    // the length of the image in the stream. The stream starts with "BZh".
    size_t start = 8;
    unsigned int length = 0;
    if (size >= 15 && memcmp(bytes + 12, "BZh", 3) == 0) {
        start = 12;
        length = le32(bytes + 8);
    } else if (memcmp(bytes + 8, "BZh", 3) != 0) {
        return NULL;
    }
    if (length == 0) length = (unsigned int)le16(bytes + 4) * le16(bytes + 6) * 5 + 64;
    char *image = malloc(length);
    if (!image) return NULL;
    unsigned int got = length;
    int status = BZ2_bzBuffToBuffDecompress(image, &got, (char *)bytes + start, (unsigned int)(size - start), 0, 0);
    uint8_t *pixels = NULL;
    if (status == BZ_OK) pixels = ar_texture_decode_qoi((const uint8_t *)image, got, width, height);
    free(image);
    return pixels;
}
