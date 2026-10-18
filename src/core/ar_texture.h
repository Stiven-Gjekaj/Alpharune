// ar_texture: decodes the texture pages of a GameMaker data file.
//
// GameMaker 2022 and later keep a page as a QOI image of its own variant
// ("fioq"), often in a bzip2 stream ("2zoq"). The variant uses the codes
// of an early draft of QOI.

#ifndef AR_TEXTURE_H
#define AR_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

// Decodes a page into RGBA pixels, four bytes for each pixel, with the
// rows from the top. Returns NULL when the bytes are not a page that it
// can decode. The caller frees the pixels.
uint8_t *ar_texture_decode(const uint8_t *bytes, size_t size, int *width, int *height);

// Decodes only a "fioq" image.
uint8_t *ar_texture_decode_qoi(const uint8_t *bytes, size_t size, int *width, int *height);

#endif
