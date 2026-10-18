// decode: measures the decode of each texture page of a data file on the
// phone, and the read of the same pixels from a raw file.
// Usage: decode DATA.ios TEMP_DIR
// Build: ./build.sh build/probe/decode -I$PWD/src/core probe/decode.m \
//     src/core/ar_data.c src/core/ar_texture.c -lbz2 -lz -framework Foundation
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include "ar_data.h"
#include "ar_texture.h"

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 3) return 2;
        NSData *file = [NSData dataWithContentsOfFile:@(argv[1])];
        ARData data;
        if (!file || !ar_data_read(&data, file.bytes, file.length)) return 1;
        double total_decode = 0, total_read = 0;
        for (int i = 0; i < data.page_count; i++) {
            int w, h;
            CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
            uint8_t *px = ar_texture_decode(data.pages[i].bytes, data.pages[i].size, &w, &h);
            double decode = (CFAbsoluteTimeGetCurrent() - t0) * 1000;
            if (!px) continue;
            NSString *path = [NSString stringWithFormat:@"%s/page%d.rgba", argv[2], i];
            NSData *raw = [NSData dataWithBytesNoCopy:px length:(NSUInteger)w * h * 4 freeWhenDone:YES];
            [raw writeToFile:path atomically:NO];
            t0 = CFAbsoluteTimeGetCurrent();
            NSData *back = [NSData dataWithContentsOfFile:path];
            double read = (CFAbsoluteTimeGetCurrent() - t0) * 1000;
            printf("page %d: %dx%d, %zu bytes, decode %.0f ms, read %.0f ms\n", i, w, h, (size_t)data.pages[i].size,
                   decode, read);
            total_decode += decode;
            total_read += read;
            (void)back;
            [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        }
        printf("total: decode %.0f ms, read %.0f ms\n", total_decode, total_read);
    }
    return 0;
}
