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

// Like ar_texture_decode, with a cache of the pixels in cache_dir. A page
// that is in the cache loads from its file; the A5 reads a page of 2048 x
// 2048 in about 100 ms, and decodes it in 220 to 600 ms. Each file has the
// hash of the page bytes in its name, so a new data file makes new files.
// With cache_dir NULL, it only decodes.
uint8_t *ar_texture_cached(const char *cache_dir, const uint8_t *bytes, size_t size, int *width, int *height);

// The path of the cache file of the page bytes in cache_dir.
void ar_texture_cache_path(const char *cache_dir, const uint8_t *bytes, size_t size, char *path, size_t path_size);

// Decodes only a "fioq" image.
uint8_t *ar_texture_decode_qoi(const uint8_t *bytes, size_t size, int *width, int *height);

#endif
