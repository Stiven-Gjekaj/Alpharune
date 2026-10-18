// logo: makes the logo of Alpharune from the pixel heart of the source art.
//
// The source is a white pixel heart with an alpha sign on black. The tool
// finds the grid of the pixels, reads one value from the center of each
// cell, and draws the cells again with sharp edges. The black outside the
// heart becomes transparent. A gloss in the style of iOS 6 goes on the top
// half of the heart.
//
// Usage: logo SOURCE.png OUTPUT.png CELL_SIZE [BACKGROUND]
// CELL_SIZE is the size of one pixel of the heart in the output.
// With BACKGROUND (for example 000000), the outside has that color and the
// output is a square, for an app icon. With BACKGROUND "none", the output
// is a square and the outside is transparent.

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

static uint8_t *load(const char *path, int *w, int *h) {
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (!source) return NULL;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
    CFRelease(source);
    if (!image) return NULL;
    *w = (int)CGImageGetWidth(image);
    *h = (int)CGImageGetHeight(image);
    uint8_t *pixels = calloc((size_t)*w * (size_t)*h, 4);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels, (size_t)*w, (size_t)*h, 8, (size_t)*w * 4, space,
                                                 (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(context, CGRectMake(0, 0, *w, *h), image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    CGImageRelease(image);
    return pixels;
}

static int save(const char *path, uint8_t *pixels, int w, int h) {
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels, (size_t)w, (size_t)h, 8, (size_t)w * 4, space,
                                                 (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGImageRef image = CGBitmapContextCreateImage(context);
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(dest, image, NULL);
    int ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return ok;
}

static int white(const uint8_t *pixels, int w, int x, int y) {
    const uint8_t *p = pixels + ((size_t)y * (size_t)w + (size_t)x) * 4;
    return p[0] + p[1] + p[2] > 3 * 128;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 4) {
            fprintf(stderr, "usage: logo SOURCE.png OUTPUT.png CELL_SIZE [BACKGROUND]\n");
            return 2;
        }
        int w, h;
        uint8_t *src = load(argv[1], &w, &h);
        if (!src) {
            fprintf(stderr, "logo: cannot read %s\n", argv[1]);
            return 1;
        }
        int cell_out = atoi(argv[3]);

        // The box of the white pixels.
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (!white(src, w, x, y)) continue;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
        // The size of a cell: the most common length of a short white run.
        int counts[200] = {0};
        for (int y = y0; y <= y1; y += 3) {
            int run = 0;
            for (int x = x0; x <= x1 + 1; x++) {
                if (x <= x1 && white(src, w, x, y)) {
                    run++;
                } else {
                    if (run >= 10 && run < 200) counts[run]++;
                    run = 0;
                }
            }
        }
        int best = 10;
        for (int n = 10; n < 200; n++) {
            if (counts[n] > counts[best]) best = n;
        }
        int cols = (int)lround((double)(x1 - x0 + 1) / best);
        int rows = (int)lround((double)(y1 - y0 + 1) / best);
        double cw = (double)(x1 - x0 + 1) / cols, ch = (double)(y1 - y0 + 1) / rows;
        fprintf(stderr, "logo: %d x %d cells of %.2f x %.2f pixels\n", cols, rows, cw, ch);

        // The grid, with one empty cell around it. 1 is white, 0 is black,
        // 2 is outside.
        int gw = cols + 2, gh = rows + 2;
        uint8_t *grid = calloc((size_t)gw * (size_t)gh, 1);
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int x = x0 + (int)((c + 0.5) * cw), y = y0 + (int)((r + 0.5) * ch);
                grid[(r + 1) * gw + c + 1] = (uint8_t)white(src, w, x, y);
            }
        }
        // A flood from the edge, through black cells that share a side.
        int *queue = malloc(sizeof(int) * (size_t)gw * (size_t)gh);
        int head = 0, tail = 0;
        grid[0] = 2;
        queue[tail++] = 0;
        while (head < tail) {
            int i = queue[head++], gx = i % gw, gy = i / gw;
            int next[4][2] = {{gx + 1, gy}, {gx - 1, gy}, {gx, gy + 1}, {gx, gy - 1}};
            for (int k = 0; k < 4; k++) {
                int nx = next[k][0], ny = next[k][1];
                if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                int j = ny * gw + nx;
                if (grid[j] != 0) continue;
                grid[j] = 2;
                queue[tail++] = j;
            }
        }

        // Draw the cells. The output has a margin of one cell.
        int ow = gw * cell_out, oh = gh * cell_out;
        int square = argc > 4;
        int clear = square && strcmp(argv[4], "none") == 0;
        unsigned background = square && !clear ? (unsigned)strtoul(argv[4], NULL, 16) : 0;
        int side = ow > oh ? ow : oh;
        int px = square ? (side - ow) / 2 : 0, py = square ? (side - oh) / 2 : 0;
        int tw = square ? side : ow, th = square ? side : oh;
        uint8_t *out = calloc((size_t)tw * (size_t)th, 4);
        if (square) {
            for (int i = 0; i < tw * th; i++) {
                out[i * 4] = (uint8_t)(background >> 16);
                out[i * 4 + 1] = (uint8_t)(background >> 8);
                out[i * 4 + 2] = (uint8_t)background;
                out[i * 4 + 3] = clear ? 0 : 255;
            }
        }
        // The gloss: an ellipse over the top of the heart. Its lower edge
        // is at 46% of the heart at the center and goes up at the sides.
        double hx0 = cell_out, hy0 = cell_out, hw = cols * cell_out, hh = rows * cell_out;
        double ex = hx0 + hw / 2, ey = hy0 - hh * 0.62, rx = hw * 0.78, ry = hh * 1.08;
        for (int y = 0; y < oh; y++) {
            for (int x = 0; x < ow; x++) {
                int v = grid[(y / cell_out) * gw + x / cell_out];
                if (v == 2) continue;
                double r = v ? 255 : 0, g = r, b = r;
                double dx = (x + 0.5 - ex) / rx, dy = (y + 0.5 - ey) / ry;
                if (dx * dx + dy * dy <= 1) {
                    double t = (y + 0.5 - hy0) / (hh * 0.46);
                    if (t < 0) t = 0;
                    if (t > 1) t = 1;
                    double a = 0.42 - 0.30 * t;
                    r += (255 - r) * a;
                    g += (255 - g) * a;
                    b += (255 - b) * a;
                }
                uint8_t *p = out + ((size_t)(y + py) * (size_t)tw + (size_t)(x + px)) * 4;
                p[0] = (uint8_t)lround(r);
                p[1] = (uint8_t)lround(g);
                p[2] = (uint8_t)lround(b);
                p[3] = 255;
            }
        }
        if (!save(argv[2], out, tw, th)) {
            fprintf(stderr, "logo: cannot write %s\n", argv[2]);
            return 1;
        }
        printf("%s: %d x %d\n", argv[2], tw, th);
    }
    return 0;
}
