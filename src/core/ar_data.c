#include "ar_data.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The reader keeps a flag for a read out of the file. Each read out of the
// file gives 0, and ar_data_read then fails.
typedef struct {
    ARData *data;
    const uint8_t *b;
    size_t size;
    int bad;
} Reader;

static uint32_t u32(Reader *r, uint32_t at) {
    if ((size_t)at + 4 > r->size) {
        r->bad = 1;
        return 0;
    }
    const uint8_t *p = r->b + at;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t i32(Reader *r, uint32_t at) {
    return (int32_t)u32(r, at);
}

static uint16_t u16(Reader *r, uint32_t at) {
    if ((size_t)at + 2 > r->size) {
        r->bad = 1;
        return 0;
    }
    return (uint16_t)(r->b[at] | r->b[at + 1] << 8);
}

static float f32(Reader *r, uint32_t at) {
    uint32_t bits = u32(r, at);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

// A string pointer points to the text. The length is in the four bytes
// before the text.
static const char *str(Reader *r, uint32_t at) {
    uint32_t ptr = u32(r, at);
    if (ptr == 0) return NULL;
    if (ptr < 4 || (size_t)ptr >= r->size) {
        r->bad = 1;
        return NULL;
    }
    uint32_t length = u32(r, ptr - 4);
    if ((size_t)ptr + length >= r->size || r->b[ptr + length] != 0) {
        r->bad = 1;
        return NULL;
    }
    return (const char *)r->b + ptr;
}

static int fail(ARData *data, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(data->error, sizeof(data->error), format, args);
    va_end(args);
    return 0;
}

static void *alloc(Reader *r, size_t count, size_t size) {
    ARData *data = r->data;
    if (count == 0) count = 1;
    if (count > (1u << 26) / size) {
        r->bad = 1;
        return NULL;
    }
    void *p = calloc(count, size);
    if (!p) {
        r->bad = 1;
        return NULL;
    }
    if (data->allocation_count == data->allocation_capacity) {
        int capacity = data->allocation_capacity ? data->allocation_capacity * 2 : 64;
        void **grown = realloc(data->allocations, (size_t)capacity * sizeof(void *));
        if (!grown) {
            free(p);
            r->bad = 1;
            return NULL;
        }
        data->allocations = grown;
        data->allocation_capacity = capacity;
    }
    data->allocations[data->allocation_count++] = p;
    return p;
}

// A list of pointers: a count, then one file offset for each entry.
static uint32_t list_count(Reader *r, uint32_t at) {
    uint32_t count = u32(r, at);
    if ((size_t)count * 4 + at + 4 > r->size) {
        r->bad = 1;
        return 0;
    }
    return count;
}

static uint32_t list_item(Reader *r, uint32_t at, uint32_t index) {
    return u32(r, at + 4 + index * 4);
}

int ar_data_chunk(const uint8_t *bytes, size_t size, const char *name, uint32_t *offset, uint32_t *length) {
    if (size < 8 || memcmp(bytes, "FORM", 4) != 0) return 0;
    size_t p = 8;
    while (p + 8 <= size) {
        uint32_t n = (uint32_t)bytes[p + 4] | (uint32_t)bytes[p + 5] << 8 | (uint32_t)bytes[p + 6] << 16 |
                     (uint32_t)bytes[p + 7] << 24;
        if (p + 8 + n > size) return 0;
        if (memcmp(bytes + p, name, 4) == 0) {
            *offset = (uint32_t)(p + 8);
            *length = n;
            return 1;
        }
        p += 8 + n;
    }
    return 0;
}

// The texture items are in a list of pointers. A sprite, a font, and a
// tile set point to an item by its file offset. This table maps an offset
// to the index of its item.
typedef struct {
    uint32_t offset;
    int32_t index;
} OffsetIndex;

static int compare_offset(const void *a, const void *b) {
    uint32_t x = ((const OffsetIndex *)a)->offset, y = ((const OffsetIndex *)b)->offset;
    return x < y ? -1 : x > y;
}

static int32_t find_offset(const OffsetIndex *table, int32_t count, uint32_t offset) {
    int32_t lo = 0, hi = count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        if (table[mid].offset == offset) return table[mid].index;
        if (table[mid].offset < offset) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static int read_gen8(Reader *r, uint32_t o) {
    ARData *d = r->data;
    d->bytecode = r->b[o + 1];
    d->name = str(r, o + 40);
    d->window_w = i32(r, o + 60);
    d->window_h = i32(r, o + 64);
    d->display_name = str(r, o + 100);
    uint32_t q = o + 128;
    d->room_order_count = (int32_t)list_count(r, q);
    d->room_order = (const uint32_t *)(r->b + q + 4);
    q += 4 + 4 * (uint32_t)d->room_order_count;
    // GameMaker 2 adds five random numbers of 8 bytes, then the frame rate.
    d->fps = f32(r, q + 40);
    if (!(d->fps > 0 && d->fps < 1000)) d->fps = 30;
    return 1;
}

static int read_strings(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->string_count = (int32_t)n;
    d->strings = alloc(r, n, sizeof(ARString));
    if (!d->strings) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        uint32_t length = u32(r, at);
        if ((size_t)at + 4 + length >= r->size) {
            r->bad = 1;
            return 0;
        }
        d->strings[i].text = (const char *)r->b + at + 4;
        d->strings[i].length = length;
    }
    return 1;
}

static OffsetIndex *read_items(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->item_count = (int32_t)n;
    d->items = alloc(r, n, sizeof(ARTexItem));
    OffsetIndex *table = alloc(r, n, sizeof(OffsetIndex));
    if (!d->items || !table) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        ARTexItem *t = &d->items[i];
        t->source_x = u16(r, at);
        t->source_y = u16(r, at + 2);
        t->source_w = u16(r, at + 4);
        t->source_h = u16(r, at + 6);
        t->target_x = u16(r, at + 8);
        t->target_y = u16(r, at + 10);
        t->target_w = u16(r, at + 12);
        t->target_h = u16(r, at + 14);
        t->bound_w = u16(r, at + 16);
        t->bound_h = u16(r, at + 18);
        t->page = u16(r, at + 20);
        table[i].offset = at;
        table[i].index = (int32_t)i;
    }
    qsort(table, n, sizeof(OffsetIndex), compare_offset);
    return table;
}

static int read_pages(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->page_count = (int32_t)n;
    d->pages = alloc(r, n, sizeof(ARTexPage));
    if (!d->pages) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        // scaled, generated mips, block size, width, height, index, data.
        ARTexPage *p = &d->pages[i];
        uint32_t size = u32(r, at + 8);
        p->width = u32(r, at + 12);
        p->height = u32(r, at + 16);
        uint32_t bytes = u32(r, at + 24);
        if (bytes == 0 || (size_t)bytes + size > r->size) {
            // A page outside the file has no bytes. The runtime shows it empty.
            p->bytes = NULL;
            p->size = 0;
            continue;
        }
        p->bytes = r->b + bytes;
        p->size = size;
    }
    return 1;
}

static int read_sprites(Reader *r, uint32_t o, const OffsetIndex *items) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->sprite_count = (int32_t)n;
    d->sprites = alloc(r, n, sizeof(ARSprite));
    if (!d->sprites) return 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        uint32_t at = list_item(r, o, i);
        ARSprite *s = &d->sprites[i];
        s->name = str(r, at);
        s->width = i32(r, at + 4);
        s->height = i32(r, at + 8);
        s->margin_left = i32(r, at + 12);
        s->margin_right = i32(r, at + 16);
        s->margin_bottom = i32(r, at + 20);
        s->margin_top = i32(r, at + 24);
        s->bbox_mode = i32(r, at + 40);
        s->sep_masks = i32(r, at + 44);
        s->origin_x = i32(r, at + 48);
        s->origin_y = i32(r, at + 52);
        uint32_t q = at + 56;
        s->speed = 1;
        s->speed_type = 1;
        if (i32(r, q) == -1) {
            // GameMaker 2: a version, a type, the speed, and the offsets of
            // a sequence (version 2) and of a nine slice (version 3).
            int32_t version = i32(r, q + 4);
            s->speed = f32(r, q + 12);
            s->speed_type = i32(r, q + 16);
            q += 20;
            if (version >= 2) q += 4;
            if (version >= 3) q += 4;
        }
        uint32_t count = list_count(r, q);
        s->frame_count = (int32_t)count;
        int32_t *frames = alloc(r, count, sizeof(int32_t));
        if (!frames) return 0;
        for (uint32_t k = 0; k < count; k++) frames[k] = find_offset(items, d->item_count, list_item(r, q, k));
        s->frames = frames;
        q += 4 + 4 * count;
        s->mask_count = i32(r, q);
        s->masks = r->b + q + 4;
        if (s->mask_count < 0 || s->width < 0 || s->height < 0) r->bad = 1;
        size_t mask_size = (size_t)((s->width + 7) / 8) * (size_t)s->height;
        if ((size_t)q + 4 + mask_size * (size_t)s->mask_count > r->size) r->bad = 1;
    }
    return !r->bad;
}

static int read_objects(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->object_count = (int32_t)n;
    d->objects = alloc(r, n, sizeof(ARObject));
    if (!d->objects) return 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        uint32_t at = list_item(r, o, i);
        ARObject *b = &d->objects[i];
        b->name = str(r, at);
        b->sprite = i32(r, at + 4);
        b->visible = i32(r, at + 8);
        // at + 12: "managed", since GameMaker 2022.5.
        b->solid = i32(r, at + 16);
        b->depth = i32(r, at + 20);
        b->persistent = i32(r, at + 24);
        b->parent = i32(r, at + 28);
        if (b->parent < 0) b->parent = -1;
        b->mask = i32(r, at + 32);
        uint32_t vertices = u32(r, at + 68);
        uint32_t q = at + 84 + vertices * 8;
        uint32_t types = list_count(r, q);
        if (types > AR_EV_COUNT) types = AR_EV_COUNT;
        for (uint32_t t = 0; t < types; t++) {
            uint32_t events = list_item(r, q, t);
            uint32_t count = list_count(r, events);
            AREvent *list = alloc(r, count, sizeof(AREvent));
            if (!list) return 0;
            for (uint32_t k = 0; k < count; k++) {
                uint32_t ev = list_item(r, events, k);
                list[k].subtype = i32(r, ev);
                list[k].code = -1;
                uint32_t actions = ev + 4;
                if (list_count(r, actions) > 0) list[k].code = i32(r, list_item(r, actions, 0) + 32);
            }
            b->event_count[t] = (int32_t)count;
            b->events[t] = list;
        }
    }
    return !r->bad;
}

static void read_layer(Reader *r, uint32_t at, ARLayer *l) {
    l->name = str(r, at);
    l->id = i32(r, at + 4);
    l->type = i32(r, at + 8);
    l->depth = i32(r, at + 12);
    l->x = f32(r, at + 16);
    l->y = f32(r, at + 20);
    l->hspeed = f32(r, at + 24);
    l->vspeed = f32(r, at + 28);
    l->visible = i32(r, at + 32);
    // at + 36: the effect is on. at + 40: the effect type. at + 44: a
    // list of effect properties, three words each.
    uint32_t props = u32(r, at + 44);
    uint32_t q = at + 48 + props * 12;
    if (l->type == AR_LAYER_INSTANCES) {
        l->instance_count = (int32_t)list_count(r, q);
        l->instances = (const uint32_t *)(r->b + q + 4);
    } else if (l->type == AR_LAYER_BACKGROUND) {
        l->bg_visible = i32(r, q);
        l->bg_foreground = i32(r, q + 4);
        l->bg_sprite = i32(r, q + 8);
        l->bg_htiled = i32(r, q + 12);
        l->bg_vtiled = i32(r, q + 16);
        l->bg_stretch = i32(r, q + 20);
        l->bg_color = u32(r, q + 24);
        l->bg_alpha = (float)(l->bg_color >> 24) / 255.0f;
        l->bg_speed = f32(r, q + 32);
        l->bg_speed_type = i32(r, q + 36);
    } else if (l->type == AR_LAYER_ASSETS) {
        uint32_t tiles = u32(r, q);
        uint32_t count = list_count(r, tiles);
        l->tile_count = (int32_t)count;
        l->tiles = alloc(r, count, sizeof(ARRoomTile));
        if (!l->tiles) return;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t t = list_item(r, tiles, k);
            ARRoomTile *tile = &l->tiles[k];
            tile->x = i32(r, t);
            tile->y = i32(r, t + 4);
            tile->sprite = i32(r, t + 8);
            tile->source_x = i32(r, t + 12);
            tile->source_y = i32(r, t + 16);
            tile->width = i32(r, t + 20);
            tile->height = i32(r, t + 24);
            tile->depth = i32(r, t + 28);
            tile->id = i32(r, t + 32);
            tile->scale_x = f32(r, t + 36);
            tile->scale_y = f32(r, t + 40);
            tile->color = u32(r, t + 44);
        }
    } else if (l->type == AR_LAYER_TILES) {
        l->tileset = i32(r, q);
        l->grid_w = i32(r, q + 4);
        l->grid_h = i32(r, q + 8);
        if (l->grid_w < 0 || l->grid_h < 0 ||
            (size_t)q + 12 + (size_t)l->grid_w * (size_t)l->grid_h * 4 > r->size) {
            r->bad = 1;
            return;
        }
        l->grid = (const uint32_t *)(r->b + q + 12);
    }
}

static int read_rooms(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->room_count = (int32_t)n;
    d->rooms = alloc(r, n, sizeof(ARRoom));
    if (!d->rooms) return 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        uint32_t at = list_item(r, o, i);
        ARRoom *m = &d->rooms[i];
        m->name = str(r, at);
        m->width = i32(r, at + 8);
        m->height = i32(r, at + 12);
        m->speed = i32(r, at + 16);
        m->persistent = i32(r, at + 20);
        m->color = u32(r, at + 24);
        m->draw_color = i32(r, at + 28);
        m->creation_code = i32(r, at + 32);
        m->flags = u32(r, at + 36);

        uint32_t views = u32(r, at + 44);
        uint32_t view_count = list_count(r, views);
        if (view_count > 8) view_count = 8;
        m->view_count = (int32_t)view_count;
        for (uint32_t k = 0; k < view_count; k++) {
            uint32_t v = list_item(r, views, k);
            ARRoomView *view = &m->views[k];
            int32_t *fields = &view->enabled;
            for (int f = 0; f < 14; f++) fields[f] = i32(r, v + 4 * (uint32_t)f);
        }

        uint32_t objects = u32(r, at + 48);
        uint32_t count = list_count(r, objects);
        m->instance_count = (int32_t)count;
        m->instances = alloc(r, count, sizeof(ARRoomInstance));
        if (!m->instances) return 0;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t q = list_item(r, objects, k);
            ARRoomInstance *e = &m->instances[k];
            e->x = i32(r, q);
            e->y = i32(r, q + 4);
            e->object = i32(r, q + 8);
            e->id = i32(r, q + 12);
            e->creation_code = i32(r, q + 16);
            e->scale_x = f32(r, q + 20);
            e->scale_y = f32(r, q + 24);
            e->image_speed = f32(r, q + 28);
            e->image_index = i32(r, q + 32);
            e->color = u32(r, q + 36);
            e->rotation = f32(r, q + 40);
            e->pre_create_code = i32(r, q + 44);
        }

        uint32_t layers = u32(r, at + 88);
        uint32_t layer_count = list_count(r, layers);
        m->layer_count = (int32_t)layer_count;
        m->layers = alloc(r, layer_count, sizeof(ARLayer));
        if (!m->layers) return 0;
        for (uint32_t k = 0; k < layer_count; k++) read_layer(r, list_item(r, layers, k), &m->layers[k]);
    }
    return !r->bad;
}

static int read_codes(Reader *r, uint32_t o, uint32_t length) {
    ARData *d = r->data;
    if (length == 0) return 1;
    uint32_t n = list_count(r, o);
    d->code_count = (int32_t)n;
    d->codes = alloc(r, n, sizeof(ARCode));
    if (!d->codes) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        ARCode *c = &d->codes[i];
        c->name = str(r, at);
        c->length = u32(r, at + 4);
        c->locals = u16(r, at + 8);
        c->arguments = u16(r, at + 10) & 0x7FFF;
        // The address of the bytecode is relative to this field.
        int64_t start = (int64_t)at + 12 + i32(r, at + 12);
        c->offset = u32(r, at + 16);
        if (start < 0 || (size_t)start + c->length > r->size || c->offset > c->length) {
            r->bad = 1;
            return 0;
        }
        c->blob = r->b + start;
    }
    return 1;
}

static int read_variables(Reader *r, uint32_t o, uint32_t length) {
    ARData *d = r->data;
    if (length < 12) return 1;
    uint32_t n = (length - 12) / 20;
    d->variable_count = (int32_t)n;
    d->variables = alloc(r, n, sizeof(ARVariable));
    if (!d->variables) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = o + 12 + i * 20;
        ARVariable *v = &d->variables[i];
        v->name = str(r, at);
        v->instance_type = i32(r, at + 4);
        v->id = i32(r, at + 8);
        v->occurrences = u32(r, at + 12);
        v->first = u32(r, at + 16);
    }
    return 1;
}

static int read_functions(Reader *r, uint32_t o, uint32_t length) {
    ARData *d = r->data;
    if (length == 0) return 1;
    uint32_t n = u32(r, o);
    if ((size_t)n * 12 + 4 > length) {
        r->bad = 1;
        return 0;
    }
    d->function_count = (int32_t)n;
    d->functions = alloc(r, n, sizeof(ARFunction));
    if (!d->functions) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = o + 4 + i * 12;
        ARFunction *f = &d->functions[i];
        f->name = str(r, at);
        f->occurrences = u32(r, at + 4);
        // The offset points to the operand after the call instruction.
        f->first = u32(r, at + 8) - 4;
    }
    return 1;
}

static int read_scripts(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->script_count = (int32_t)n;
    d->scripts = alloc(r, n, sizeof(ARScript));
    if (!d->scripts) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        d->scripts[i].name = str(r, at);
        uint32_t id = u32(r, at + 4);
        d->scripts[i].constructor = (id & 0x80000000u) != 0;
        d->scripts[i].code = (int32_t)(id & 0x7FFFFFFFu);
    }
    return 1;
}

static int read_fonts(Reader *r, uint32_t o, const OffsetIndex *items) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->font_count = (int32_t)n;
    d->fonts = alloc(r, n, sizeof(ARFont));
    if (!d->fonts) return 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        uint32_t at = list_item(r, o, i);
        ARFont *f = &d->fonts[i];
        f->name = str(r, at);
        f->font_name = str(r, at + 4);
        f->size = f32(r, at + 8);
        f->bold = i32(r, at + 12);
        f->italic = i32(r, at + 16);
        f->range_start = u16(r, at + 20);
        f->range_end = i32(r, at + 24);
        f->item = find_offset(items, d->item_count, u32(r, at + 28));
        f->scale_x = f32(r, at + 32);
        f->scale_y = f32(r, at + 36);
        f->ascender_offset = i32(r, at + 40);
        // at + 44 and at + 48: the ascender and the SDF spread.
        uint32_t q = at + 52;
        uint32_t count = list_count(r, q);
        f->glyph_count = (int32_t)count;
        f->glyphs = alloc(r, count, sizeof(ARGlyph));
        if (!f->glyphs) return 0;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t g = list_item(r, q, k);
            ARGlyph *glyph = &f->glyphs[k];
            glyph->character = u16(r, g);
            glyph->x = u16(r, g + 2);
            glyph->y = u16(r, g + 4);
            glyph->w = u16(r, g + 6);
            glyph->h = u16(r, g + 8);
            glyph->shift = (int16_t)u16(r, g + 10);
            glyph->offset = (int16_t)u16(r, g + 12);
        }
    }
    return !r->bad;
}

static int read_sounds(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->sound_count = (int32_t)n;
    d->sounds = alloc(r, n, sizeof(ARSoundInfo));
    if (!d->sounds) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        ARSoundInfo *s = &d->sounds[i];
        s->name = str(r, at);
        s->flags = u32(r, at + 4);
        s->type = str(r, at + 8);
        s->file = str(r, at + 12);
        s->volume = f32(r, at + 20);
        s->pitch = f32(r, at + 24);
        s->group = i32(r, at + 28);
        s->audio = i32(r, at + 32);
    }
    return 1;
}

static int read_audio(Reader *r, uint32_t o, ARAudio **audio, int32_t *count) {
    uint32_t n = list_count(r, o);
    *count = (int32_t)n;
    *audio = alloc(r, n, sizeof(ARAudio));
    if (!*audio) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        uint32_t size = u32(r, at);
        if ((size_t)at + 4 + size > r->size) {
            r->bad = 1;
            return 0;
        }
        (*audio)[i].bytes = r->b + at + 4;
        (*audio)[i].size = size;
    }
    return 1;
}

static int read_tilesets(Reader *r, uint32_t o, const OffsetIndex *items) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->tileset_count = (int32_t)n;
    d->tilesets = alloc(r, n, sizeof(ARTileset));
    if (!d->tilesets) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = list_item(r, o, i);
        ARTileset *t = &d->tilesets[i];
        t->name = str(r, at);
        t->item = find_offset(items, d->item_count, u32(r, at + 16));
        t->tile_w = i32(r, at + 24);
        t->tile_h = i32(r, at + 28);
        t->border_x = i32(r, at + 32);
        t->border_y = i32(r, at + 36);
        t->columns = i32(r, at + 40);
        t->tile_count = i32(r, at + 48);
    }
    return 1;
}

static int read_audio_groups(Reader *r, uint32_t o) {
    ARData *d = r->data;
    uint32_t n = list_count(r, o);
    d->audio_group_count = (int32_t)n;
    d->audio_groups = alloc(r, n, sizeof(const char *));
    if (!d->audio_groups) return 0;
    for (uint32_t i = 0; i < n; i++) d->audio_groups[i] = str(r, list_item(r, o, i));
    return 1;
}

int ar_data_read(ARData *data, const uint8_t *bytes, size_t size) {
    memset(data, 0, sizeof(*data));
    data->bytes = bytes;
    data->size = size;
    if (size < 8 || memcmp(bytes, "FORM", 4) != 0) return fail(data, "the file is not a GameMaker data file");
    Reader reader = {data, bytes, size, 0};
    Reader *r = &reader;
    uint32_t o, n;

    if (!ar_data_chunk(bytes, size, "GEN8", &o, &n)) return fail(data, "the file has no GEN8 chunk");
    read_gen8(r, o);
    if (data->bytecode < 15) return fail(data, "bytecode version %d is too old", data->bytecode);
    if (ar_data_chunk(bytes, size, "STRG", &o, &n)) read_strings(r, o);
    OffsetIndex *items = NULL;
    if (ar_data_chunk(bytes, size, "TPAG", &o, &n)) items = read_items(r, o);
    if (!items) items = alloc(r, 1, sizeof(OffsetIndex));
    if (ar_data_chunk(bytes, size, "TXTR", &o, &n)) read_pages(r, o);
    if (ar_data_chunk(bytes, size, "SPRT", &o, &n)) read_sprites(r, o, items);
    if (ar_data_chunk(bytes, size, "BGND", &o, &n)) read_tilesets(r, o, items);
    if (ar_data_chunk(bytes, size, "OBJT", &o, &n)) read_objects(r, o);
    if (ar_data_chunk(bytes, size, "ROOM", &o, &n)) read_rooms(r, o);
    if (ar_data_chunk(bytes, size, "CODE", &o, &n)) read_codes(r, o, n);
    if (ar_data_chunk(bytes, size, "VARI", &o, &n)) read_variables(r, o, n);
    if (ar_data_chunk(bytes, size, "FUNC", &o, &n)) read_functions(r, o, n);
    if (ar_data_chunk(bytes, size, "SCPT", &o, &n)) read_scripts(r, o);
    if (ar_data_chunk(bytes, size, "GLOB", &o, &n) && n >= 4) {
        data->global_init_count = (int32_t)list_count(r, o);
        data->global_inits = (const uint32_t *)(bytes + o + 4);
    }
    if (ar_data_chunk(bytes, size, "FONT", &o, &n)) read_fonts(r, o, items);
    if (ar_data_chunk(bytes, size, "SOND", &o, &n)) read_sounds(r, o);
    if (ar_data_chunk(bytes, size, "AUDO", &o, &n)) read_audio(r, o, &data->audio, &data->audio_count);
    if (ar_data_chunk(bytes, size, "AGRP", &o, &n)) read_audio_groups(r, o);
    if (r->bad) {
        ar_data_free(data);
        return fail(data, "the file is damaged or of an unknown version");
    }
    return 1;
}

int ar_data_read_audio_group(const uint8_t *bytes, size_t size, ARAudio **audio, int32_t *count) {
    uint32_t o, n;
    *audio = NULL;
    *count = 0;
    if (!ar_data_chunk(bytes, size, "AUDO", &o, &n)) return 0;
    ARData holder;
    memset(&holder, 0, sizeof(holder));
    Reader reader = {&holder, bytes, size, 0};
    if (!read_audio(&reader, o, audio, count) || reader.bad) {
        ar_data_free(&holder);
        *audio = NULL;
        *count = 0;
        return 0;
    }
    // The caller frees the table with free(); the holder keeps only it.
    for (int i = 0; i < holder.allocation_count; i++) {
        if (holder.allocations[i] != *audio) free(holder.allocations[i]);
    }
    free(holder.allocations);
    return 1;
}

void ar_data_free(ARData *data) {
    for (int i = 0; i < data->allocation_count; i++) free(data->allocations[i]);
    free(data->allocations);
    char error[sizeof(data->error)];
    memcpy(error, data->error, sizeof(error));
    memset(data, 0, sizeof(*data));
    memcpy(data->error, error, sizeof(error));
}

#define FIND(kind, table, count)                                                   \
    int ar_data_find_##kind(const ARData *data, const char *name) {                \
        for (int32_t i = 0; i < data->count; i++) {                                \
            if (data->table[i].name && strcmp(data->table[i].name, name) == 0) return i; \
        }                                                                          \
        return -1;                                                                 \
    }

FIND(sprite, sprites, sprite_count)
FIND(object, objects, object_count)
FIND(room, rooms, room_count)
FIND(code, codes, code_count)
