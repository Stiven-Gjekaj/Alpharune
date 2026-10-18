// The drawing of the runtime: a batch of triangles, sprites, text, the
// shapes of GML, and the surfaces.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ar_run_internal.h"
#include "ar_texture.h"

typedef struct {
    int handle, width, height;
    double ma, mb, mc, md, me, mf;
} SavedTarget;

static SavedTarget gStack[16];

void ar_draw_init(ARRun *run) {
    run->batch_capacity = 6 * 4096;
    run->batch = malloc(sizeof(ARVertex) * (size_t)run->batch_capacity);
    run->page_handles = calloc((size_t)run->data->page_count + 1, sizeof(int));
    for (int i = 0; i < run->data->page_count; i++) run->page_handles[i] = -1;
    run->surface_count = 64;
    run->surfaces = calloc((size_t)run->surface_count, sizeof(ARSurface));
    ARGfx *g = &run->gfx;
    if (g->target_new) {
        run->window_handle = g->target_new(g->context, run->window_w, run->window_h);
        run->surfaces[0].handle = g->target_new(g->context, run->window_w, run->window_h);
    }
    run->surfaces[0].used = 1;
    run->surfaces[0].width = run->window_w;
    run->surfaces[0].height = run->window_h;
    run->ma = run->md = 1;
    run->target_w = run->window_w;
    run->target_h = run->window_h;
}

void ar_draw_free(ARRun *run) {
    ARGfx *g = &run->gfx;
    if (g->texture_free) {
        for (int i = 0; i < run->data->page_count; i++) {
            if (run->page_handles[i] > 0) g->texture_free(g->context, run->page_handles[i]);
        }
        for (int i = 0; i < run->surface_count; i++) {
            if (run->surfaces[i].used && run->surfaces[i].handle > 0) g->texture_free(g->context, run->surfaces[i].handle);
        }
        if (run->window_handle > 0) g->texture_free(g->context, run->window_handle);
    }
    free(run->batch);
    free(run->page_handles);
    free(run->surfaces);
}

void ar_draw_flush(ARRun *run) {
    if (run->batch_count == 0) return;
    ARGfx *g = &run->gfx;
    if (g->triangles) {
        g->triangles(g->context, run->batch_texture, run->batch_filter, run->fog, run->batch, run->batch_count);
    }
    run->batch_count = 0;
    run->draw_calls++;
}

int ar_draw_page(ARRun *run, int page) {
    if (page < 0 || page >= run->data->page_count) return 0;
    if (run->page_handles[page] >= 0) return run->page_handles[page];
    run->page_handles[page] = 0;
    const ARTexPage *p = &run->data->pages[page];
    if (!p->bytes || !run->gfx.texture_new) return 0;
    int w, h;
    uint8_t *pixels = ar_texture_decode(p->bytes, p->size, &w, &h);
    if (!pixels) {
        ar_vm_log(run->vm, "texture page %d does not decode", page);
        return 0;
    }
    run->page_handles[page] = run->gfx.texture_new(run->gfx.context, pixels, w, h);
    free(pixels);
    return run->page_handles[page];
}

static void use_texture(ARRun *run, int texture) {
    int filter = run->tex_filter;
    if (texture != run->batch_texture || filter != run->batch_filter) {
        ar_draw_flush(run);
        run->batch_texture = texture;
        run->batch_filter = filter;
    }
}

static ARVertex *reserve(ARRun *run, int count) {
    if (run->batch_count + count > run->batch_capacity) ar_draw_flush(run);
    ARVertex *v = &run->batch[run->batch_count];
    run->batch_count += count;
    return v;
}

static inline void vertex(ARRun *run, ARVertex *v, double x, double y, float u, float tv, uint32_t color) {
    v->x = (float)(run->ma * x + run->mc * y + run->me);
    v->y = (float)(run->mb * x + run->md * y + run->mf);
    v->u = u;
    v->v = tv;
    v->color = color;
}

// A quad from four corners and four texture coordinates, as two triangles.
static void quad(ARRun *run, const double *xs, const double *ys, float u0, float v0, float u1, float v1,
                 const uint32_t *colors) {
    ARVertex *v = reserve(run, 6);
    vertex(run, &v[0], xs[0], ys[0], u0, v0, colors[0]);
    vertex(run, &v[1], xs[1], ys[1], u1, v0, colors[1]);
    vertex(run, &v[2], xs[3], ys[3], u0, v1, colors[3]);
    v[3] = v[2];
    v[4] = v[1];
    vertex(run, &v[5], xs[2], ys[2], u1, v1, colors[2]);
}

void ar_draw_set_target(ARRun *run, int handle, int width, int height) {
    ar_draw_flush(run);
    if (run->gfx.target_set) run->gfx.target_set(run->gfx.context, handle, width, height);
    run->target_handle_now = handle;
    run->target_w = width;
    run->target_h = height;
}

void ar_draw_identity(ARRun *run) {
    run->ma = 1;
    run->mb = 0;
    run->mc = 0;
    run->md = 1;
    run->me = 0;
    run->mf = 0;
}

void ar_draw_set_view(ARRun *run, double vx, double vy, double vw, double vh, double angle, double px, double py,
                      double pw, double ph) {
    if (vw == 0) vw = 1;
    if (vh == 0) vh = 1;
    double sx = pw / vw, sy = ph / vh;
    if (angle == 0) {
        run->ma = sx;
        run->mb = 0;
        run->mc = 0;
        run->md = sy;
        run->me = px - vx * sx;
        run->mf = py - vy * sy;
        return;
    }
    // A turn about the center of the view.
    double a = -angle * M_PI / 180.0, c = cos(a), s = sin(a);
    double cx = vx + vw / 2, cy = vy + vh / 2;
    run->ma = c * sx;
    run->mb = s * sy;
    run->mc = -s * sx;
    run->md = c * sy;
    run->me = px + pw / 2 - (run->ma * cx + run->mc * cy);
    run->mf = py + ph / 2 - (run->mb * cx + run->md * cy);
}

void ar_draw_clear(ARRun *run, uint32_t rgb, double alpha) {
    ar_draw_flush(run);
    if (run->gfx.clear) run->gfx.clear(run->gfx.context, ar_rgba(rgb, alpha));
}

void ar_draw_blend(ARRun *run, int enabled, int src, int dst) {
    ar_draw_flush(run);
    if (src == 0) {
        src = AR_BLEND_SRC_ALPHA;
        dst = AR_BLEND_INV_SRC_ALPHA;
    }
    if (run->gfx.blend) run->gfx.blend(run->gfx.context, enabled, src, dst, src, dst);
}

static uint32_t tint(ARRun *run, uint32_t color) {
    if (run->fog) return run->fog_color & 0xFFFFFF;
    return color;
}

// --------------------------------------------------------------- sprites

void ar_draw_item_part(ARRun *run, int item, double left, double top, double width, double height, double x,
                       double y, double xscale, double yscale, uint32_t color, double alpha) {
    if (item < 0 || item >= run->data->item_count) return;
    const ARTexItem *it = &run->data->items[item];
    const ARTexPage *page = &run->data->pages[it->page < run->data->page_count ? it->page : 0];
    int texture = ar_draw_page(run, it->page);
    if (!texture || page->width == 0) return;
    // The part, cut to the trimmed rectangle of the item.
    double l = left > it->target_x ? left : it->target_x;
    double t = top > it->target_y ? top : it->target_y;
    double r = left + width < it->target_x + it->target_w ? left + width : it->target_x + it->target_w;
    double b = top + height < it->target_y + it->target_h ? top + height : it->target_y + it->target_h;
    if (r <= l || b <= t) return;
    double kx = it->target_w ? (double)it->source_w / it->target_w : 1;
    double ky = it->target_h ? (double)it->source_h / it->target_h : 1;
    float u0 = (float)((it->source_x + (l - it->target_x) * kx) / page->width);
    float v0 = (float)((it->source_y + (t - it->target_y) * ky) / page->height);
    float u1 = (float)((it->source_x + (r - it->target_x) * kx) / page->width);
    float v1 = (float)((it->source_y + (b - it->target_y) * ky) / page->height);
    double x0 = x + (l - left) * xscale, y0 = y + (t - top) * yscale;
    double x1 = x + (r - left) * xscale, y1 = y + (b - top) * yscale;
    double xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    uint32_t c = ar_rgba(tint(run, color), alpha);
    uint32_t colors[4] = {c, c, c, c};
    use_texture(run, texture);
    quad(run, xs, ys, u0, v0, u1, v1, colors);
}

void ar_draw_item(ARRun *run, int item, double x, double y, double xscale, double yscale, double angle,
                  double ox, double oy, uint32_t color, double alpha) {
    if (item < 0 || item >= run->data->item_count) return;
    const ARTexItem *it = &run->data->items[item];
    const ARTexPage *page = &run->data->pages[it->page < run->data->page_count ? it->page : 0];
    int texture = ar_draw_page(run, it->page);
    if (!texture || page->width == 0) return;
    float u0 = (float)it->source_x / page->width, v0 = (float)it->source_y / page->height;
    float u1 = (float)(it->source_x + it->source_w) / page->width;
    float v1 = (float)(it->source_y + it->source_h) / page->height;
    double l = (it->target_x - ox) * xscale, t = (it->target_y - oy) * yscale;
    double r = (it->target_x + it->target_w - ox) * xscale, b = (it->target_y + it->target_h - oy) * yscale;
    double lx[4] = {l, r, r, l}, ly[4] = {t, t, b, b};
    double xs[4], ys[4];
    if (angle != 0) {
        double a = angle * M_PI / 180.0, c = cos(a), s = sin(a);
        for (int i = 0; i < 4; i++) {
            xs[i] = x + lx[i] * c + ly[i] * s;
            ys[i] = y - lx[i] * s + ly[i] * c;
        }
    } else {
        for (int i = 0; i < 4; i++) {
            xs[i] = x + lx[i];
            ys[i] = y + ly[i];
        }
    }
    uint32_t cc = ar_rgba(tint(run, color), alpha);
    uint32_t colors[4] = {cc, cc, cc, cc};
    use_texture(run, texture);
    quad(run, xs, ys, u0, v0, u1, v1, colors);
}

static int frame_item(ARRun *run, int sprite, double frame) {
    if (sprite < 0 || sprite >= run->data->sprite_count) return -1;
    const ARSprite *s = &run->data->sprites[sprite];
    if (s->frame_count <= 0) return -1;
    int f = (int)floor(frame);
    f %= s->frame_count;
    if (f < 0) f += s->frame_count;
    return s->frames[f];
}

void ar_draw_sprite(ARRun *run, int sprite, double frame, double x, double y, double xscale, double yscale,
                    double angle, uint32_t color, double alpha) {
    int item = frame_item(run, sprite, frame);
    if (item < 0) return;
    const ARSprite *s = &run->data->sprites[sprite];
    ar_draw_item(run, item, x, y, xscale, yscale, angle, s->origin_x, s->origin_y, color, alpha);
}

void ar_draw_sprite_part(ARRun *run, int sprite, double frame, double left, double top, double width,
                         double height, double x, double y, double xscale, double yscale, uint32_t color,
                         double alpha) {
    int item = frame_item(run, sprite, frame);
    if (item < 0) return;
    ar_draw_item_part(run, item, left, top, width, height, x, y, xscale, yscale, color, alpha);
}

void ar_draw_sprite_stretched(ARRun *run, int sprite, double frame, double x, double y, double w, double h,
                              uint32_t color, double alpha) {
    int item = frame_item(run, sprite, frame);
    if (item < 0) return;
    const ARSprite *s = &run->data->sprites[sprite];
    if (s->width <= 0 || s->height <= 0) return;
    ar_draw_item(run, item, x, y, w / s->width, h / s->height, 0, 0, 0, color, alpha);
}

void ar_draw_sprite_tiled(ARRun *run, int sprite, double frame, double x, double y, double xscale,
                          double yscale, int htiled, int vtiled, double area_x, double area_y, double area_w,
                          double area_h, uint32_t color, double alpha) {
    int item = frame_item(run, sprite, frame);
    if (item < 0) return;
    const ARSprite *s = &run->data->sprites[sprite];
    double tw = s->width * fabs(xscale), th = s->height * fabs(yscale);
    if (tw < 1 || th < 1) return;
    // The tiles that cover the area of the view, so a large room does not
    // draw each tile.
    double vx = area_x, vy = area_y, vw = area_w, vh = area_h;
    if (run->views_enabled) {
        ARView *view = &run->views[run->view_current];
        ARCamera *c = &run->cameras[view->camera >= 0 && view->camera < AR_MAX_CAMERAS ? view->camera : 0];
        vx = c->x;
        vy = c->y;
        vw = c->w;
        vh = c->h;
    }
    double x0 = x, y0 = y;
    if (htiled) x0 = x - ceil((x - vx) / tw) * tw;
    if (vtiled) y0 = y - ceil((y - vy) / th) * th;
    double x1 = htiled ? vx + vw : x + tw / 2;
    double y1 = vtiled ? vy + vh : y + th / 2;
    for (double ty = y0; ty < y1; ty += th) {
        for (double tx = x0; tx < x1; tx += tw) {
            ar_draw_item(run, item, tx, ty, xscale, yscale, 0, 0, 0, color, alpha);
            if (!htiled) break;
        }
        if (!vtiled) break;
    }
}

// ---------------------------------------------------------------- shapes

void ar_draw_rect(ARRun *run, double x1, double y1, double x2, double y2, uint32_t c1, uint32_t c2, uint32_t c3,
                  uint32_t c4, double alpha, int outline) {
    if (x1 > x2) {
        double t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        double t = y1;
        y1 = y2;
        y2 = t;
    }
    if (outline) {
        ar_draw_rect(run, x1, y1, x2, y1, c1, c2, c2, c1, alpha, 0);
        ar_draw_rect(run, x1, y2, x2, y2, c4, c3, c3, c4, alpha, 0);
        ar_draw_rect(run, x1, y1 + 1, x1, y2 - 1, c1, c1, c4, c4, alpha, 0);
        ar_draw_rect(run, x2, y1 + 1, x2, y2 - 1, c2, c2, c3, c3, alpha, 0);
        return;
    }
    double xs[4] = {x1, x2 + 1, x2 + 1, x1}, ys[4] = {y1, y1, y2 + 1, y2 + 1};
    uint32_t colors[4] = {ar_rgba(tint(run, c1), alpha), ar_rgba(tint(run, c2), alpha), ar_rgba(tint(run, c3), alpha),
                          ar_rgba(tint(run, c4), alpha)};
    use_texture(run, 0);
    quad(run, xs, ys, 0, 0, 1, 1, colors);
}

void ar_draw_line(ARRun *run, double x1, double y1, double x2, double y2, double width, uint32_t c1, uint32_t c2,
                  double alpha) {
    double dx = x2 - x1, dy = y2 - y1, len = sqrt(dx * dx + dy * dy);
    if (len == 0) {
        ar_draw_rect(run, x1, y1, x1, y1, c1, c1, c1, c1, alpha, 0);
        return;
    }
    if (width < 1) width = 1;
    double nx = -dy / len * width / 2, ny = dx / len * width / 2;
    // A line of width 1 covers the pixel centers on its path.
    double ox = 0.5, oy = 0.5;
    double xs[4] = {x1 + nx + ox, x2 + nx + ox, x2 - nx + ox, x1 - nx + ox};
    double ys[4] = {y1 + ny + oy, y2 + ny + oy, y2 - ny + oy, y1 - ny + oy};
    uint32_t a = ar_rgba(tint(run, c1), alpha), b = ar_rgba(tint(run, c2), alpha);
    uint32_t colors[4] = {a, b, b, a};
    use_texture(run, 0);
    quad(run, xs, ys, 0, 0, 1, 1, colors);
}

void ar_draw_triangle(ARRun *run, double x1, double y1, double x2, double y2, double x3, double y3, uint32_t c1,
                      uint32_t c2, uint32_t c3, double alpha, int outline) {
    if (outline) {
        ar_draw_line(run, x1, y1, x2, y2, 1, c1, c2, alpha);
        ar_draw_line(run, x2, y2, x3, y3, 1, c2, c3, alpha);
        ar_draw_line(run, x3, y3, x1, y1, 1, c3, c1, alpha);
        return;
    }
    use_texture(run, 0);
    ARVertex *v = reserve(run, 3);
    vertex(run, &v[0], x1, y1, 0, 0, ar_rgba(tint(run, c1), alpha));
    vertex(run, &v[1], x2, y2, 0, 0, ar_rgba(tint(run, c2), alpha));
    vertex(run, &v[2], x3, y3, 0, 0, ar_rgba(tint(run, c3), alpha));
}

void ar_draw_ellipse(ARRun *run, double x1, double y1, double x2, double y2, uint32_t c1, uint32_t c2,
                     double alpha, int outline) {
    double cx = (x1 + x2) / 2, cy = (y1 + y2) / 2, rx = fabs(x2 - x1) / 2, ry = fabs(y2 - y1) / 2;
    const int segments = 24;
    for (int i = 0; i < segments; i++) {
        double a0 = 2 * M_PI * i / segments, a1 = 2 * M_PI * (i + 1) / segments;
        double px0 = cx + cos(a0) * rx, py0 = cy + sin(a0) * ry;
        double px1 = cx + cos(a1) * rx, py1 = cy + sin(a1) * ry;
        if (outline) ar_draw_line(run, px0, py0, px1, py1, 1, c2, c2, alpha);
        else ar_draw_triangle(run, cx, cy, px0, py0, px1, py1, c1, c2, c2, alpha, 0);
    }
}

// ------------------------------------------------------------------ text

typedef struct {
    const ARFont *font;     // A font of the data, or NULL.
    const ARSpriteFont *sf; // A sprite font, or NULL.
    int item;
    double line_height;
} Font;

static int get_font(ARRun *run, Font *f) {
    memset(f, 0, sizeof(*f));
    int id = run->draw_font;
    if (id < 0) id = 0;
    if (id < run->data->font_count) {
        f->font = &run->data->fonts[id];
        f->item = f->font->item;
        f->line_height = f->font->glyph_count > 0 ? f->font->glyphs[0].h : 16;
        return 1;
    }
    id -= run->data->font_count;
    if (id < run->sprite_font_count) {
        f->sf = &run->sprite_fonts[id];
        if (f->sf->sprite < 0 || f->sf->sprite >= run->data->sprite_count) return 0;
        f->line_height = run->data->sprites[f->sf->sprite].height;
        return 1;
    }
    return 0;
}

static uint32_t next_char(const char **p) {
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t c = s[0];
    int n = 1;
    if (c >= 0xF0 && s[1] && s[2] && s[3]) {
        c = (c & 7) << 18 | (s[1] & 63) << 12 | (s[2] & 63) << 6 | (s[3] & 63);
        n = 4;
    } else if (c >= 0xE0 && s[1] && s[2]) {
        c = (c & 15) << 12 | (s[1] & 63) << 6 | (s[2] & 63);
        n = 3;
    } else if (c >= 0xC0 && s[1]) {
        c = (c & 31) << 6 | (s[1] & 63);
        n = 2;
    }
    *p += n;
    return c;
}

static const ARGlyph *find_glyph(const ARFont *font, uint32_t c) {
    int lo = 0, hi = font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font->glyphs[mid].character == c) return &font->glyphs[mid];
        if (font->glyphs[mid].character < c) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

// The frame of a character of a sprite font, or -1.
static int sprite_font_frame(const ARSpriteFont *sf, uint32_t c) {
    const char *p = sf->map;
    int i = 0;
    while (*p) {
        if (next_char(&p) == c) return i;
        i++;
    }
    return -1;
}

static double char_advance(ARRun *run, const Font *f, uint32_t c) {
    if (f->font) {
        const ARGlyph *g = find_glyph(f->font, c);
        if (g) return g->shift;
        const ARGlyph *space = find_glyph(f->font, ' ');
        return space ? space->shift : 0;
    }
    const ARSprite *s = &run->data->sprites[f->sf->sprite];
    int frame = sprite_font_frame(f->sf, c);
    if (frame < 0) return s->width + f->sf->sep;
    if (!f->sf->prop) return s->width + f->sf->sep;
    // A proportional font uses the width of the pixels of the frame.
    const ARTexItem *it = &run->data->items[s->frames[frame % s->frame_count]];
    return it->target_w + f->sf->sep;
}

static double line_width(ARRun *run, const Font *f, const char *start, const char *end) {
    double w = 0;
    const char *p = start;
    while (p < end) w += char_advance(run, f, next_char(&p));
    return w;
}

// Calls the line function for each line of the text, after the wrap at a
// width. Returns the count of lines.
typedef void (*LineFn)(ARRun *run, const Font *f, const char *start, const char *end, int index, void *context);

static int each_line(ARRun *run, const Font *f, const char *text, double width, LineFn fn, void *context) {
    int count = 0;
    const char *p = text;
    while (1) {
        const char *end = p;
        while (*end && *end != '\n' && *end != '\r') end++;
        if (width > 0) {
            // Wrap at the last space that keeps the line in the width.
            const char *start = p;
            while (start < end) {
                const char *cut = NULL;
                const char *q = start;
                double w = 0;
                while (q < end) {
                    const char *before = q;
                    uint32_t c = next_char(&q);
                    w += char_advance(run, f, c);
                    if (c == ' ') cut = before;
                    if (w > width && cut) break;
                    if (w > width && !cut && before > start) {
                        cut = before;
                        break;
                    }
                }
                if (q >= end || !cut || w <= width) {
                    if (fn) fn(run, f, start, end, count, context);
                    count++;
                    break;
                }
                if (fn) fn(run, f, start, cut, count, context);
                count++;
                start = *cut == ' ' ? cut + 1 : cut;
            }
            if (start >= end && end == p) {
                if (fn) fn(run, f, p, p, count, context);
                count++;
            }
        } else {
            if (fn) fn(run, f, p, end, count, context);
            count++;
        }
        if (!*end) break;
        p = end + 1;
        if (*end == '\r' && *p == '\n') p++;
    }
    return count;
}

typedef struct {
    double x, y, sep, xscale, yscale, angle;
    double total_h;
    uint32_t c1, c2, c3, c4;
    double alpha;
    double max_w;
} TextJob;

static void draw_char(ARRun *run, const Font *f, uint32_t c, double lx, double ly, const TextJob *job, double gw) {
    double cs = cos(job->angle * M_PI / 180.0), sn = sin(job->angle * M_PI / 180.0);
    // A gradient: c1 at the left and c2 at the right of the line; c4 and c3
    // at the bottom. Each character takes the colors of its place.
    (void)gw;
    if (f->font) {
        const ARGlyph *g = find_glyph(f->font, c);
        if (!g || g->w == 0 || f->item < 0) return;
        const ARTexItem *it = &run->data->items[f->item];
        const ARTexPage *page = &run->data->pages[it->page < run->data->page_count ? it->page : 0];
        int texture = ar_draw_page(run, it->page);
        if (!texture) return;
        float u0 = (float)(it->source_x + g->x) / page->width, v0 = (float)(it->source_y + g->y) / page->height;
        float u1 = (float)(it->source_x + g->x + g->w) / page->width;
        float v1 = (float)(it->source_y + g->y + g->h) / page->height;
        double l = (lx + g->offset) * job->xscale, t = ly * job->yscale;
        double r = (lx + g->offset + g->w) * job->xscale, b = (ly + g->h) * job->yscale;
        double px[4] = {l, r, r, l}, py[4] = {t, t, b, b};
        double xs[4], ys[4];
        for (int i = 0; i < 4; i++) {
            xs[i] = job->x + px[i] * cs + py[i] * sn;
            ys[i] = job->y - px[i] * sn + py[i] * cs;
        }
        double fx0 = job->max_w > 0 ? (lx + g->offset) / job->max_w : 0;
        double fx1 = job->max_w > 0 ? (lx + g->offset + g->w) / job->max_w : 0;
        (void)fx0;
        (void)fx1;
        uint32_t colors[4] = {ar_rgba(tint(run, job->c1), job->alpha), ar_rgba(tint(run, job->c2), job->alpha),
                              ar_rgba(tint(run, job->c3), job->alpha), ar_rgba(tint(run, job->c4), job->alpha)};
        use_texture(run, texture);
        quad(run, xs, ys, u0, v0, u1, v1, colors);
        return;
    }
    int frame = sprite_font_frame(f->sf, c);
    if (frame < 0) return;
    const ARSprite *s = &run->data->sprites[f->sf->sprite];
    int item = s->frames[frame % s->frame_count];
    double offset = 0;
    if (f->sf->prop) offset = -run->data->items[item].target_x;
    double ox = job->x + ((lx + offset) * job->xscale) * cs + (ly * job->yscale) * sn;
    double oy = job->y - ((lx + offset) * job->xscale) * sn + (ly * job->yscale) * cs;
    ar_draw_item(run, item, ox, oy, job->xscale, job->yscale, job->angle, 0, 0, job->c1, job->alpha);
}

static void draw_line_fn(ARRun *run, const Font *f, const char *start, const char *end, int index, void *context) {
    TextJob *job = context;
    double w = line_width(run, f, start, end);
    double lx = 0;
    if (run->halign == 1) lx = -floor(w / 2);
    else if (run->halign == 2) lx = -w;
    double ly = index * job->sep;
    if (run->valign == 1) ly -= floor(job->total_h / 2);
    else if (run->valign == 2) ly -= job->total_h;
    const char *p = start;
    while (p < end) {
        uint32_t c = next_char(&p);
        draw_char(run, f, c, lx, ly, job, w);
        lx += char_advance(run, f, c);
    }
}

void ar_draw_text(ARRun *run, double x, double y, const char *text, double sep, double width, double xscale,
                  double yscale, double angle, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double alpha) {
    Font f;
    if (!get_font(run, &f)) return;
    TextJob job = {x, y, sep >= 0 ? sep : f.line_height, xscale, yscale, angle, 0, c1, c2, c3, c4, alpha, 0};
    int lines = each_line(run, &f, text, width, NULL, NULL);
    job.total_h = lines * job.sep;
    each_line(run, &f, text, width, draw_line_fn, &job);
}

typedef struct {
    double max;
} Measure;

static void measure_fn(ARRun *run, const Font *f, const char *start, const char *end, int index, void *context) {
    (void)index;
    Measure *m = context;
    double w = line_width(run, f, start, end);
    if (w > m->max) m->max = w;
}

double ar_draw_text_width(ARRun *run, const char *text, double sep, double width) {
    (void)sep;
    Font f;
    if (!get_font(run, &f)) return 0;
    Measure m = {0};
    each_line(run, &f, text, width, measure_fn, &m);
    return m.max;
}

double ar_draw_text_height(ARRun *run, const char *text, double sep, double width) {
    Font f;
    if (!get_font(run, &f)) return 0;
    int lines = each_line(run, &f, text, width, NULL, NULL);
    return lines * (sep >= 0 ? sep : f.line_height);
}

// -------------------------------------------------------------- surfaces

int ar_draw_surface_new(ARRun *run, int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width > 2048) width = 2048;
    if (height > 2048) height = 2048;
    for (int i = 1; i < run->surface_count; i++) {
        if (run->surfaces[i].used) continue;
        ARSurface *s = &run->surfaces[i];
        s->used = 1;
        s->width = width;
        s->height = height;
        s->handle = run->gfx.target_new ? run->gfx.target_new(run->gfx.context, width, height) : 0;
        // A new surface is clear.
        if (s->handle && run->gfx.target_set) {
            ar_draw_flush(run);
            run->gfx.target_set(run->gfx.context, s->handle, width, height);
            if (run->gfx.clear) run->gfx.clear(run->gfx.context, 0);
            run->gfx.target_set(run->gfx.context, run->target_handle_now, run->target_w, run->target_h);
        }
        return i;
    }
    return -1;
}

void ar_draw_surface_free(ARRun *run, int surface) {
    if (surface <= 0 || surface >= run->surface_count || !run->surfaces[surface].used) return;
    ar_draw_flush(run);
    if (run->gfx.texture_free && run->surfaces[surface].handle > 0) {
        run->gfx.texture_free(run->gfx.context, run->surfaces[surface].handle);
    }
    memset(&run->surfaces[surface], 0, sizeof(ARSurface));
}

void ar_draw_surface(ARRun *run, int surface, double x, double y, double xscale, double yscale, double angle,
                     uint32_t color, double alpha) {
    if (surface < 0 || surface >= run->surface_count || !run->surfaces[surface].used) return;
    ARSurface *s = &run->surfaces[surface];
    if (!s->handle) return;
    double l = 0, t = 0, r = s->width * xscale, b = s->height * yscale;
    double lx[4] = {l, r, r, l}, ly[4] = {t, t, b, b};
    double xs[4], ys[4];
    double a = angle * M_PI / 180.0, c = cos(a), sn = sin(a);
    for (int i = 0; i < 4; i++) {
        xs[i] = x + lx[i] * c + ly[i] * sn;
        ys[i] = y - lx[i] * sn + ly[i] * c;
    }
    uint32_t cc = ar_rgba(tint(run, color), alpha);
    uint32_t colors[4] = {cc, cc, cc, cc};
    use_texture(run, s->handle);
    quad(run, xs, ys, 0, 0, 1, 1, colors);
}

int ar_draw_push_target(ARRun *run, int surface) {
    if (surface < 0 || surface >= run->surface_count || !run->surfaces[surface].used) return 0;
    if (run->target_depth >= 16) return 0;
    SavedTarget *t = &gStack[run->target_depth++];
    t->handle = run->target_handle_now;
    t->width = run->target_w;
    t->height = run->target_h;
    t->ma = run->ma;
    t->mb = run->mb;
    t->mc = run->mc;
    t->md = run->md;
    t->me = run->me;
    t->mf = run->mf;
    ARSurface *s = &run->surfaces[surface];
    ar_draw_set_target(run, s->handle, s->width, s->height);
    ar_draw_identity(run);
    return 1;
}

int ar_draw_pop_target(ARRun *run) {
    if (run->target_depth <= 0) return 0;
    SavedTarget *t = &gStack[--run->target_depth];
    ar_draw_set_target(run, t->handle, t->width, t->height);
    run->ma = t->ma;
    run->mb = t->mb;
    run->mc = t->mc;
    run->md = t->md;
    run->me = t->me;
    run->mf = t->mf;
    return 1;
}
