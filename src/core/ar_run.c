#include "ar_run_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

ARRun *ar_run_of(ARVM *vm) {
    return (ARRun *)ar_vm_user(vm);
}

double ar_run_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

double ar_run_random(ARRun *run) {
    // xorshift64*
    uint64_t x = run->random_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    run->random_state = x;
    return (double)((x * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}

static void copy_dir(char *out, size_t size, const char *dir) {
    snprintf(out, size, "%s", dir ? dir : ".");
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
}

void ar_run_path(ARRun *run, const char *name, int write, char *out, size_t size) {
    // A name in the game directory becomes relative to it.
    size_t gl = strlen(run->game_dir);
    if (strncmp(name, run->game_dir, gl) == 0 && name[gl] == '/') name += gl + 1;
    size_t sl = strlen(run->save_dir);
    if (strncmp(name, run->save_dir, sl) == 0 && name[sl] == '/') name += sl + 1;
    if (name[0] == '/') {
        snprintf(out, size, "%s", name);
        return;
    }
    snprintf(out, size, "%s/%s", run->save_dir, name);
    if (write) return;
    struct stat st;
    if (stat(out, &st) == 0) return;
    snprintf(out, size, "%s/%s", run->game_dir, name);
    if (stat(out, &st) == 0) return;
    // The music is in a directory of its own.
    if (run->music_dir[0]) {
        char music[1024];
        snprintf(music, sizeof(music), "%s/%s", run->music_dir, strncmp(name, "mus/", 4) == 0 ? name + 4 : name);
        if (stat(music, &st) == 0) snprintf(out, size, "%s", music);
    }
}

// ------------------------------------------------------------------ events

static const AREvent *find_event(const ARData *d, int object, int type, int subtype, int *owner) {
    for (int guard = 0; object >= 0 && object < d->object_count && guard < 64; guard++) {
        const ARObject *o = &d->objects[object];
        for (int i = 0; i < o->event_count[type]; i++) {
            if (o->events[type][i].subtype == subtype) {
                if (owner) *owner = object;
                return &o->events[type][i];
            }
        }
        object = o->parent;
    }
    return NULL;
}

int ar_run_has_event(ARRun *run, int object, int type, int subtype) {
    return find_event(run->data, object, type, subtype, NULL) != NULL;
}

static void run_event_from(ARRun *run, ARInstance *inst, int object, int type, int subtype, ARStruct *other) {
    int owner;
    const AREvent *ev = find_event(run->data, object, type, subtype, &owner);
    if (!ev || ev->code < 0) return;
    if (run->event_depth >= 64) return;
    AREventContext *c = &run->event_stack[run->event_depth++];
    c->object = owner;
    c->type = type;
    c->subtype = subtype;
    c->self = inst;
    ar_vm_run(run->vm, ev->code, inst->vars, other ? other : inst->vars, 0, NULL);
    run->event_depth--;
}

void ar_run_event(ARRun *run, ARInstance *inst, int type, int subtype) {
    if (inst->destroyed) return;
    run_event_from(run, inst, inst->object, type, subtype, NULL);
}

static void run_event_other(ARRun *run, ARInstance *inst, int type, int subtype, ARInstance *other) {
    if (inst->destroyed) return;
    run_event_from(run, inst, inst->object, type, subtype, other ? other->vars : NULL);
}

void ar_run_event_inherited(ARRun *run) {
    if (run->event_depth == 0) return;
    AREventContext c = run->event_stack[run->event_depth - 1];
    int parent = run->data->objects[c.object].parent;
    if (parent < 0) return;
    ARInstance *self = ar_vm_self_instance(run->vm);
    if (!self) self = c.self;
    run_event_from(run, self, parent, c.type, c.subtype, ar_vm_other(run->vm));
}

// -------------------------------------------------------------- instances

ARInstance *ar_run_instance(ARRun *run, ARValue v) {
    if (v.type == AR_STRUCT) return (ARInstance *)v.o->instance;
    int32_t n = ar_to_int(v);
    if (n == AR_SELF) return ar_vm_self_instance(run->vm);
    if (n == AR_OTHER) return ar_vm_other_instance(run->vm);
    if (n >= AR_FIRST_INSTANCE_ID) {
        ARInstance *inst = ar_vm_find_instance(run->vm, n);
        return inst && !inst->destroyed ? inst : NULL;
    }
    if (n >= 0) {
        int count = ar_vm_instance_count(run->vm);
        for (int i = 0; i < count; i++) {
            ARInstance *inst = ar_vm_instance_at(run->vm, i);
            if (!inst->destroyed && inst->active && ar_vm_is_object(run->vm, inst->object, n)) return inst;
        }
    }
    return NULL;
}

static ARLayerState *add_layer(ARRun *run, int id, const char *name, double depth) {
    if (run->layer_count == run->layer_capacity) {
        run->layer_capacity = run->layer_capacity ? run->layer_capacity * 2 : 32;
        run->layers = realloc(run->layers, sizeof(ARLayerState) * (size_t)run->layer_capacity);
    }
    ARLayerState *l = &run->layers[run->layer_count++];
    memset(l, 0, sizeof(*l));
    l->id = id;
    snprintf(l->name, sizeof(l->name), "%s", name ? name : "");
    l->depth = depth;
    l->visible = 1;
    if (id >= run->next_layer_id) run->next_layer_id = id + 1;
    return l;
}

int ar_run_layer_find(ARRun *run, int id) {
    for (int i = 0; i < run->layer_count; i++) {
        if (run->layers[i].id == id) return i;
    }
    return -1;
}

ARLayerState *ar_run_layer(ARRun *run, int id) {
    int i = ar_run_layer_find(run, id);
    return i >= 0 ? &run->layers[i] : NULL;
}

int ar_run_layer_for_depth(ARRun *run, double depth) {
    for (int i = 0; i < run->layer_count; i++) {
        if (run->layers[i].managed && run->layers[i].depth == depth) return run->layers[i].id;
    }
    char name[64];
    snprintf(name, sizeof(name), "_managed_%g", depth);
    ARLayerState *l = add_layer(run, run->next_layer_id, name, depth);
    l->managed = 1;
    return l->id;
}

ARElement *ar_run_element(ARRun *run, int id) {
    // The ids grow, so a binary search finds an element.
    int lo = 0, hi = run->element_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (run->elements[mid].id == id) return &run->elements[mid];
        if (run->elements[mid].id < id) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int ar_run_element_new(ARRun *run, int layer, int type) {
    if (run->element_count == run->element_capacity) {
        run->element_capacity = run->element_capacity ? run->element_capacity * 2 : 256;
        run->elements = realloc(run->elements, sizeof(ARElement) * (size_t)run->element_capacity);
    }
    ARElement *e = &run->elements[run->element_count++];
    memset(e, 0, sizeof(*e));
    e->id = run->next_element_id++;
    e->layer = layer;
    e->type = type;
    e->visible = 1;
    e->blend = 0xFFFFFF;
    e->alpha = 1;
    e->xscale = e->yscale = 1;
    e->sprite = -1;
    ARLayerState *l = ar_run_layer(run, layer);
    if (l) {
        if (l->element_count == l->element_capacity) {
            l->element_capacity = l->element_capacity ? l->element_capacity * 2 : 8;
            l->elements = realloc(l->elements, sizeof(int) * (size_t)l->element_capacity);
        }
        l->elements[l->element_count++] = e->id;
    }
    return e->id;
}

void ar_run_element_remove(ARRun *run, int id) {
    ARElement *e = ar_run_element(run, id);
    if (!e) return;
    ARLayerState *l = ar_run_layer(run, e->layer);
    if (l) {
        for (int i = 0; i < l->element_count; i++) {
            if (l->elements[i] == id) {
                memmove(&l->elements[i], &l->elements[i + 1], sizeof(int) * (size_t)(l->element_count - i - 1));
                l->element_count--;
                break;
            }
        }
    }
    e->type = 0;
    e->visible = 0;
}

ARInstance *ar_run_instance_create(ARRun *run, double x, double y, int object, double depth, int layer) {
    if (object < 0 || object >= run->data->object_count) {
        ar_vm_log(run->vm, "instance_create of object %d", object);
        return NULL;
    }
    ARInstance *inst = ar_vm_instance_new(run->vm, object, 0);
    inst->x = inst->xstart = inst->xprevious = x;
    inst->y = inst->ystart = inst->yprevious = y;
    if (layer >= 0) {
        ARLayerState *l = ar_run_layer(run, layer);
        inst->layer = layer;
        inst->depth = l ? l->depth : depth;
    } else {
        inst->depth = depth;
        inst->layer = -1;
    }
    ar_run_event(run, inst, AR_EV_CREATE, 0);
    inst->created = 1;
    return inst;
}

void ar_run_instance_destroy(ARRun *run, ARInstance *inst, int run_event) {
    if (!inst || inst->destroyed) return;
    if (run_event) ar_run_event(run, inst, AR_EV_DESTROY, 0);
    ar_run_event(run, inst, AR_EV_CLEANUP, 0);
    inst->destroyed = 1;
}

// ---------------------------------------------------------------- sprites

int ar_run_image_number(ARRun *run, ARInstance *inst) {
    if (inst->sprite_index < 0 || inst->sprite_index >= run->data->sprite_count) return 0;
    return run->data->sprites[inst->sprite_index].frame_count;
}

static const ARSprite *mask_sprite(ARRun *run, ARInstance *inst) {
    int s = inst->mask_index >= 0 ? inst->mask_index : inst->sprite_index;
    if (s < 0 || s >= run->data->sprite_count) return NULL;
    return &run->data->sprites[s];
}

// The four corners of the mask box of a sprite, at (x, y).
static void mask_corners(const ARSprite *s, ARInstance *inst, double x, double y, double cx[4], double cy[4]) {
    double l = s->margin_left - s->origin_x, r = s->margin_right + 1 - s->origin_x;
    double t = s->margin_top - s->origin_y, b = s->margin_bottom + 1 - s->origin_y;
    double lx[4] = {l, r, r, l}, ly[4] = {t, t, b, b};
    double a = inst->image_angle * M_PI / 180.0, c = cos(a), sn = sin(a);
    for (int i = 0; i < 4; i++) {
        double px = lx[i] * inst->image_xscale, py = ly[i] * inst->image_yscale;
        if (inst->image_angle != 0) {
            cx[i] = x + px * c + py * sn;
            cy[i] = y - px * sn + py * c;
        } else {
            cx[i] = x + px;
            cy[i] = y + py;
        }
    }
}

static int bbox_at(ARRun *run, ARInstance *inst, double x, double y, double *left, double *top, double *right,
                   double *bottom) {
    const ARSprite *s = mask_sprite(run, inst);
    if (!s) {
        *left = *right = x;
        *top = *bottom = y;
        return 0;
    }
    double cx[4], cy[4];
    mask_corners(s, inst, x, y, cx, cy);
    *left = *right = cx[0];
    *top = *bottom = cy[0];
    for (int i = 1; i < 4; i++) {
        if (cx[i] < *left) *left = cx[i];
        if (cx[i] > *right) *right = cx[i];
        if (cy[i] < *top) *top = cy[i];
        if (cy[i] > *bottom) *bottom = cy[i];
    }
    return 1;
}

int ar_run_bbox(ARRun *run, ARInstance *inst, double *left, double *top, double *right, double *bottom) {
    return bbox_at(run, inst, inst->x, inst->y, left, top, right, bottom);
}

// 1 when the point is in the mask of the instance at (x, y).
static int point_in(ARRun *run, ARInstance *inst, double x, double y, double px, double py, int precise) {
    const ARSprite *s = mask_sprite(run, inst);
    if (!s) return 0;
    // The point in the coordinates of the sprite.
    double dx = px - x, dy = py - y;
    if (inst->image_angle != 0) {
        double a = inst->image_angle * M_PI / 180.0, c = cos(a), sn = sin(a);
        double rx = dx * c - dy * sn, ry = dx * sn + dy * c;
        dx = rx;
        dy = ry;
    }
    if (inst->image_xscale == 0 || inst->image_yscale == 0) return 0;
    double sx = dx / inst->image_xscale + s->origin_x, sy = dy / inst->image_yscale + s->origin_y;
    if (sx < s->margin_left || sx >= s->margin_right + 1 || sy < s->margin_top || sy >= s->margin_bottom + 1) return 0;
    if (!precise || s->sep_masks != 1 || s->mask_count == 0) return 1;
    int ix = (int)floor(sx), iy = (int)floor(sy);
    if (ix < 0 || iy < 0 || ix >= s->width || iy >= s->height) return 0;
    int frame = 0;
    if (s->mask_count > 1 && inst->mask_index < 0) {
        frame = (int)floor(inst->image_index) % s->mask_count;
        if (frame < 0) frame += s->mask_count;
    }
    size_t row = (size_t)((s->width + 7) / 8);
    const uint8_t *mask = s->masks + row * (size_t)s->height * (size_t)frame;
    return (mask[row * (size_t)iy + (size_t)(ix / 8)] >> (7 - ix % 8)) & 1;
}

static int needs_precise(ARRun *run, ARInstance *inst) {
    const ARSprite *s = mask_sprite(run, inst);
    return s && (s->sep_masks == 1 || inst->image_angle != 0);
}

int ar_run_collide(ARRun *run, ARInstance *a, double x, double y, ARInstance *b) {
    double al, at, ar, ab, bl, bt, br, bb;
    if (!bbox_at(run, a, x, y, &al, &at, &ar, &ab)) return 0;
    if (!bbox_at(run, b, b->x, b->y, &bl, &bt, &br, &bb)) return 0;
    if (!(al < br && bl < ar && at < bb && bt < ab)) return 0;
    if (!needs_precise(run, a) && !needs_precise(run, b)) return 1;
    // Test the center of each pixel in the overlap.
    int x1 = (int)floor(al > bl ? al : bl), x2 = (int)ceil(ar < br ? ar : br);
    int y1 = (int)floor(at > bt ? at : bt), y2 = (int)ceil(ab < bb ? ab : bb);
    for (int py = y1; py < y2; py++) {
        for (int px = x1; px < x2; px++) {
            if (point_in(run, a, x, y, px + 0.5, py + 0.5, 1) && point_in(run, b, b->x, b->y, px + 0.5, py + 0.5, 1)) {
                return 1;
            }
        }
    }
    return 0;
}

int ar_run_collide_rect(ARRun *run, ARInstance *inst, double x1, double y1, double x2, double y2, int precise) {
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
    double l, t, r, b;
    if (!ar_run_bbox(run, inst, &l, &t, &r, &b)) return 0;
    // The rectangle of GML includes its right and bottom edges.
    if (!(l < x2 + 1 && x1 < r && t < y2 + 1 && y1 < b)) return 0;
    if (!precise || !needs_precise(run, inst)) return 1;
    int ix1 = (int)floor(l > x1 ? l : x1), ix2 = (int)ceil(r < x2 + 1 ? r : x2 + 1);
    int iy1 = (int)floor(t > y1 ? t : y1), iy2 = (int)ceil(b < y2 + 1 ? b : y2 + 1);
    for (int py = iy1; py < iy2; py++) {
        for (int px = ix1; px < ix2; px++) {
            if (point_in(run, inst, inst->x, inst->y, px + 0.5, py + 0.5, 1)) return 1;
        }
    }
    return 0;
}

int ar_run_collide_point(ARRun *run, ARInstance *inst, double px, double py, int precise) {
    return point_in(run, inst, inst->x, inst->y, px, py, precise);
}

// ------------------------------------------------------------------ rooms

static void clear_room_state(ARRun *run) {
    for (int i = 0; i < run->layer_count; i++) free(run->layers[i].elements);
    run->layer_count = 0;
    run->element_count = 0;
    run->next_layer_id = 0;
    run->next_element_id = 1;
}

static void load_room_layers(ARRun *run, const ARRoom *room) {
    for (int i = 0; i < room->layer_count; i++) {
        const ARLayer *dl = &room->layers[i];
        ARLayerState *l = add_layer(run, dl->id, dl->name, dl->depth);
        l->x = dl->x;
        l->y = dl->y;
        l->hspeed = dl->hspeed;
        l->vspeed = dl->vspeed;
        l->visible = dl->visible;
        int lid = l->id;
        if (dl->type == AR_LAYER_BACKGROUND) {
            ARElement *e = ar_run_element(run, ar_run_element_new(run, lid, AR_ELEMENT_BACKGROUND));
            e->sprite = dl->bg_sprite;
            e->visible = dl->bg_visible;
            e->htiled = dl->bg_htiled;
            e->vtiled = dl->bg_vtiled;
            e->stretch = dl->bg_stretch;
            e->blend = dl->bg_color & 0xFFFFFF;
            e->alpha = dl->bg_alpha;
            e->speed = dl->bg_speed;
            e->speed_type = dl->bg_speed_type;
        } else if (dl->type == AR_LAYER_ASSETS) {
            for (int k = 0; k < dl->tile_count; k++) {
                const ARRoomTile *t = &dl->tiles[k];
                ARElement *e = ar_run_element(run, ar_run_element_new(run, lid, AR_ELEMENT_TILE));
                e->sprite = t->sprite;
                e->x = t->x;
                e->y = t->y;
                e->source_x = t->source_x;
                e->source_y = t->source_y;
                e->width = t->width;
                e->height = t->height;
                e->xscale = t->scale_x;
                e->yscale = t->scale_y;
                e->blend = t->color & 0xFFFFFF;
                e->alpha = (double)(t->color >> 24) / 255.0;
            }
        } else if (dl->type == AR_LAYER_TILES) {
            ARElement *e = ar_run_element(run, ar_run_element_new(run, lid, AR_ELEMENT_TILEMAP));
            e->tileset = dl->tileset;
            e->grid_w = dl->grid_w;
            e->grid_h = dl->grid_h;
            e->grid = dl->grid;
        }
    }
}

static int layer_of_instance(const ARRoom *room, int id) {
    for (int i = 0; i < room->layer_count; i++) {
        const ARLayer *l = &room->layers[i];
        if (l->type != AR_LAYER_INSTANCES) continue;
        for (int k = 0; k < l->instance_count; k++) {
            if ((int)l->instances[k] == id) return l->id;
        }
    }
    return -1;
}

static void setup_views(ARRun *run, const ARRoom *room) {
    run->views_enabled = (room->flags & 1) != 0;
    for (int i = 0; i < AR_MAX_VIEWS; i++) {
        ARView *v = &run->views[i];
        ARCamera *c = &run->cameras[i];
        memset(v, 0, sizeof(*v));
        memset(c, 0, sizeof(*c));
        v->camera = i;
        v->surface = -1;
        c->used = 1;
        c->target = -1;
        if (i < room->view_count) {
            const ARRoomView *rv = &room->views[i];
            v->visible = rv->enabled;
            v->xport = rv->port_x;
            v->yport = rv->port_y;
            v->wport = rv->port_w;
            v->hport = rv->port_h;
            c->x = rv->view_x;
            c->y = rv->view_y;
            c->w = rv->view_w;
            c->h = rv->view_h;
            c->border_x = rv->border_x;
            c->border_y = rv->border_y;
            c->speed_x = rv->speed_x;
            c->speed_y = rv->speed_y;
            c->target = rv->object;
        } else {
            c->w = run->window_w;
            c->h = run->window_h;
            v->wport = run->window_w;
            v->hport = run->window_h;
        }
    }
}

// With views, the application surface takes the size of the box of the
// visible ports, as in GameMaker. The draw of the window stretches it.
static void size_app_surface(ARRun *run) {
    int w = run->window_w, h = run->window_h;
    if (run->views_enabled) {
        int right = 0, bottom = 0;
        for (int i = 0; i < AR_MAX_VIEWS; i++) {
            ARView *v = &run->views[i];
            if (!v->visible) continue;
            if (v->xport + v->wport > right) right = v->xport + v->wport;
            if (v->yport + v->hport > bottom) bottom = v->yport + v->hport;
        }
        if (right > 0 && bottom > 0) {
            w = right;
            h = bottom;
        }
    }
    ARSurface *app = &run->surfaces[0];
    if (app->width == w && app->height == h) return;
    ar_draw_flush(run);
    if (run->gfx.texture_free && app->handle) run->gfx.texture_free(run->gfx.context, app->handle);
    app->width = w;
    app->height = h;
    app->handle = run->gfx.target_new ? run->gfx.target_new(run->gfx.context, w, h) : 0;
}

void ar_run_update_camera(ARRun *run, int camera) {
    if (camera < 0 || camera >= AR_MAX_CAMERAS) return;
    ARCamera *c = &run->cameras[camera];
    if (c->target < 0) return;
    ARInstance *inst = ar_run_instance(run, ar_real(c->target));
    if (!inst) return;
    double bx = c->border_x, by = c->border_y;
    if (2 * bx >= c->w) bx = c->w / 2;
    if (2 * by >= c->h) by = c->h / 2;
    double nx = c->x, ny = c->y;
    if (inst->x - bx < nx) nx = inst->x - bx;
    if (inst->x + bx > nx + c->w) nx = inst->x + bx - c->w;
    if (inst->y - by < ny) ny = inst->y - by;
    if (inst->y + by > ny + c->h) ny = inst->y + by - c->h;
    if (c->speed_x >= 0 && fabs(nx - c->x) > c->speed_x) nx = c->x + (nx > c->x ? c->speed_x : -c->speed_x);
    if (c->speed_y >= 0 && fabs(ny - c->y) > c->speed_y) ny = c->y + (ny > c->y ? c->speed_y : -c->speed_y);
    if (nx + c->w > run->room_w) nx = run->room_w - c->w;
    if (ny + c->h > run->room_h) ny = run->room_h - c->h;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    c->x = nx;
    c->y = ny;
}

static void enter_room(ARRun *run, int index, int first) {
    const ARData *d = run->data;
    if (index < 0 || index >= d->room_count) return;
    const ARRoom *room = &d->rooms[index];
    run->room = index;
    run->room_w = room->width;
    run->room_h = room->height;
    run->room_speed = room->speed > 0 ? room->speed : d->fps;
    run->room_persistent = room->persistent;
    run->background_color = room->color & 0xFFFFFF;
    run->show_background_color = (room->flags & 2) != 0;
    clear_room_state(run);
    load_room_layers(run, room);
    setup_views(run, room);
    size_app_surface(run);
    // The persistent instances stay on a layer of their depth.
    int count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (!inst->destroyed) inst->layer = ar_run_layer_for_depth(run, inst->depth);
    }

    // The instances of the room: the create event, then the creation code
    // of each, in the order of the room.
    ARInstance **made = malloc(sizeof(ARInstance *) * (size_t)(room->instance_count + 1));
    int made_count = 0;
    for (int i = 0; i < room->instance_count; i++) {
        const ARRoomInstance *ri = &room->instances[i];
        if (ri->object < 0 || ri->object >= d->object_count) continue;
        if (ar_vm_find_instance(run->vm, ri->id)) continue;
        ARInstance *inst = ar_vm_instance_new(run->vm, ri->object, ri->id);
        inst->x = inst->xstart = inst->xprevious = ri->x;
        inst->y = inst->ystart = inst->yprevious = ri->y;
        inst->image_xscale = ri->scale_x;
        inst->image_yscale = ri->scale_y;
        inst->image_speed = ri->image_speed;
        inst->image_index = ri->image_index;
        inst->image_blend = (int32_t)(ri->color & 0xFFFFFF);
        inst->image_alpha = (double)(ri->color >> 24) / 255.0;
        inst->image_angle = ri->rotation;
        int layer = layer_of_instance(room, ri->id);
        ARLayerState *l = layer >= 0 ? ar_run_layer(run, layer) : NULL;
        inst->layer = layer;
        if (l) inst->depth = l->depth;
        if (ri->pre_create_code >= 0) ar_vm_run(run->vm, ri->pre_create_code, inst->vars, inst->vars, 0, NULL);
        ar_run_event(run, inst, AR_EV_CREATE, 0);
        inst->created = 1;
        if (ri->creation_code >= 0 && !inst->destroyed) {
            ar_vm_run(run->vm, ri->creation_code, inst->vars, inst->vars, 0, NULL);
        }
        made[made_count++] = inst;
    }
    if (first) {
        count = ar_vm_instance_count(run->vm);
        for (int i = 0; i < count; i++) {
            ARInstance *inst = ar_vm_instance_at(run->vm, i);
            ar_run_event(run, inst, AR_EV_OTHER, AR_OTHER_GAME_START);
        }
    }
    if (room->creation_code >= 0) ar_vm_run(run->vm, room->creation_code, NULL, NULL, 0, NULL);
    count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        ar_run_event(run, inst, AR_EV_OTHER, AR_OTHER_ROOM_START);
    }
    free(made);
    for (int i = 0; i < AR_MAX_VIEWS; i++) {
        if (run->views[i].visible) ar_run_update_camera(run, run->views[i].camera);
    }
}

void ar_run_goto_room(ARRun *run, int room) {
    if (room < 0 || room >= run->data->room_count) {
        ar_vm_log(run->vm, "room_goto of room %d", room);
        return;
    }
    run->next_room = room;
}

static void change_room(ARRun *run) {
    int next = run->next_room;
    run->next_room = -1;
    int count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        ar_run_event(run, inst, AR_EV_OTHER, AR_OTHER_ROOM_END);
    }
    count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (!inst->persistent && !inst->destroyed) {
            ar_run_event(run, inst, AR_EV_CLEANUP, 0);
            inst->destroyed = 1;
        }
    }
    ar_vm_remove_destroyed(run->vm);
    enter_room(run, next, 0);
}

// ------------------------------------------------------------------ frame

typedef struct {
    double depth;
    int kind;  // 0: an element of a layer, 1: an instance
    int order;
    ARInstance *inst;
    int element;
} DrawItem;

static int compare_draw(const void *a, const void *b) {
    const DrawItem *x = a, *y = b;
    if (x->depth != y->depth) return x->depth > y->depth ? -1 : 1;
    if (x->kind != y->kind) return x->kind - y->kind;
    return x->order - y->order;
}

static DrawItem *gItems;
static int gItemCapacity;

static int collect_draw_items(ARRun *run) {
    int count = ar_vm_instance_count(run->vm);
    int need = count + run->element_count + 1;
    if (need > gItemCapacity) {
        gItemCapacity = need * 2;
        gItems = realloc(gItems, sizeof(DrawItem) * (size_t)gItemCapacity);
    }
    int n = 0;
    for (int i = 0; i < run->layer_count; i++) {
        ARLayerState *l = &run->layers[i];
        if (!l->visible) continue;
        for (int k = 0; k < l->element_count; k++) {
            ARElement *e = ar_run_element(run, l->elements[k]);
            if (!e || e->type == AR_ELEMENT_INSTANCE || !e->visible) continue;
            DrawItem *it = &gItems[n++];
            it->depth = l->depth;
            it->kind = 0;
            it->order = e->id;
            it->inst = NULL;
            it->element = e->id;
        }
    }
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (inst->destroyed || !inst->active || !inst->visible) continue;
        ARLayerState *l = inst->layer >= 0 ? ar_run_layer(run, inst->layer) : NULL;
        if (l && !l->visible) continue;
        DrawItem *it = &gItems[n++];
        it->depth = inst->depth;
        it->kind = 1;
        it->order = inst->order;
        it->inst = inst;
        it->element = 0;
    }
    qsort(gItems, (size_t)n, sizeof(DrawItem), compare_draw);
    return n;
}

static void draw_element(ARRun *run, ARElement *e) {
    ARLayerState *l = ar_run_layer(run, e->layer);
    double lx = l ? l->x : 0, ly = l ? l->y : 0;
    if (e->type == AR_ELEMENT_BACKGROUND) {
        if (e->sprite < 0) {
            if (e->alpha > 0) {
                uint32_t c = e->blend;
                ar_draw_rect(run, -100000, -100000, 100000, 100000, c, c, c, c, e->alpha, 0);
            }
            return;
        }
        const ARSprite *s = &run->data->sprites[e->sprite];
        if (e->stretch) {
            ar_draw_sprite_stretched(run, e->sprite, e->index, lx, ly, run->room_w, run->room_h, e->blend, e->alpha);
        } else if (e->htiled || e->vtiled) {
            ar_draw_sprite_tiled(run, e->sprite, e->index, lx, ly, e->xscale, e->yscale, e->htiled, e->vtiled, 0, 0,
                                 run->room_w, run->room_h, e->blend, e->alpha);
        } else {
            ar_draw_sprite(run, e->sprite, e->index, lx + s->origin_x * e->xscale, ly + s->origin_y * e->yscale,
                           e->xscale, e->yscale, 0, e->blend, e->alpha);
        }
    } else if (e->type == AR_ELEMENT_TILE) {
        if (e->sprite < 0 || e->sprite >= run->data->sprite_count) return;
        ar_draw_sprite_part(run, e->sprite, 0, e->source_x, e->source_y, e->width, e->height, e->x + lx, e->y + ly,
                            e->xscale, e->yscale, e->blend, e->alpha);
    } else if (e->type == AR_ELEMENT_SPRITE) {
        if (e->sprite < 0 || e->sprite >= run->data->sprite_count) return;
        ar_draw_sprite(run, e->sprite, e->index, e->x + lx, e->y + ly, e->xscale, e->yscale, 0, e->blend, e->alpha);
    } else if (e->type == AR_ELEMENT_TILEMAP) {
        if (e->tileset < 0 || e->tileset >= run->data->tileset_count) return;
        const ARTileset *ts = &run->data->tilesets[e->tileset];
        if (ts->item < 0 || ts->columns <= 0) return;
        for (int gy = 0; gy < e->grid_h; gy++) {
            for (int gx = 0; gx < e->grid_w; gx++) {
                uint32_t cell = e->grid[gy * e->grid_w + gx];
                int tile = (int)(cell & 0x7FFFF);
                if (tile == 0) continue;
                int col = tile % ts->columns, row = tile / ts->columns;
                double sx = col * (ts->tile_w + 2 * ts->border_x) + ts->border_x;
                double sy = row * (ts->tile_h + 2 * ts->border_y) + ts->border_y;
                ar_draw_item_part(run, ts->item, sx, sy, ts->tile_w, ts->tile_h, lx + gx * ts->tile_w,
                                  ly + gy * ts->tile_h, 1, 1, 0xFFFFFF, 1);
            }
        }
    }
}

static void default_draw(ARRun *run, ARInstance *inst) {
    if (inst->sprite_index < 0 || inst->sprite_index >= run->data->sprite_count) return;
    ar_draw_sprite(run, inst->sprite_index, inst->image_index, inst->x, inst->y, inst->image_xscale,
                   inst->image_yscale, inst->image_angle, (uint32_t)inst->image_blend, inst->image_alpha);
}

static void draw_pass(ARRun *run, int subtype, int with_layers) {
    int n = collect_draw_items(run);
    // A copy, because an event can draw a list of its own.
    DrawItem *items = malloc(sizeof(DrawItem) * (size_t)(n + 1));
    memcpy(items, gItems, sizeof(DrawItem) * (size_t)n);
    for (int i = 0; i < n; i++) {
        if (items[i].kind == 0) {
            if (with_layers) {
                ARElement *e = ar_run_element(run, items[i].element);
                if (e) draw_element(run, e);
            }
            continue;
        }
        ARInstance *inst = items[i].inst;
        if (inst->destroyed || !inst->visible) continue;
        if (ar_run_has_event(run, inst->object, AR_EV_DRAW, subtype)) {
            ar_run_event(run, inst, AR_EV_DRAW, subtype);
        } else if (subtype == AR_DRAW_NORMAL) {
            default_draw(run, inst);
        }
    }
    free(items);
}

static void draw_frame(ARRun *run) {
    run->draw_calls = 0;
    // The application surface.
    ARSurface *app = &run->surfaces[0];
    ar_draw_set_target(run, app->handle, app->width, app->height);
    ar_draw_identity(run);
    ar_draw_clear(run, run->show_background_color ? run->background_color : 0, 1);
    draw_pass(run, AR_DRAW_PRE, 0);
    int any = 0;
    if (run->views_enabled) {
        for (int v = 0; v < AR_MAX_VIEWS; v++) {
            ARView *view = &run->views[v];
            if (!view->visible) continue;
            any = 1;
            run->view_current = v;
            ARCamera *c = &run->cameras[view->camera >= 0 && view->camera < AR_MAX_CAMERAS ? view->camera : 0];
            int target_w = app->width, target_h = app->height, handle = app->handle;
            if (view->surface >= 0 && view->surface < run->surface_count && run->surfaces[view->surface].used) {
                handle = run->surfaces[view->surface].handle;
                target_w = run->surfaces[view->surface].width;
                target_h = run->surfaces[view->surface].height;
            }
            ar_draw_set_target(run, handle, target_w, target_h);
            ar_draw_set_view(run, c->x, c->y, c->w, c->h, c->angle, view->xport, view->yport, view->wport,
                             view->hport);
            draw_pass(run, AR_DRAW_BEGIN, 0);
            draw_pass(run, AR_DRAW_NORMAL, 1);
            draw_pass(run, AR_DRAW_END, 0);
        }
    }
    if (!any) {
        run->view_current = 0;
        ar_draw_set_target(run, app->handle, app->width, app->height);
        ar_draw_set_view(run, 0, 0, app->width, app->height, 0, 0, 0, app->width, app->height);
        draw_pass(run, AR_DRAW_BEGIN, 0);
        draw_pass(run, AR_DRAW_NORMAL, 1);
        draw_pass(run, AR_DRAW_END, 0);
    }
    run->view_current = 0;
    // The window: the application surface, then the post draw and the GUI.
    ar_draw_set_target(run, run->window_handle, run->window_w, run->window_h);
    ar_draw_identity(run);
    ar_draw_clear(run, 0, 1);
    if (run->app_surface_draw) {
        ar_draw_blend(run, 0, 0, 0);
        ar_draw_surface(run, 0, 0, 0, (double)run->window_w / app->width, (double)run->window_h / app->height, 0,
                        0xFFFFFF, 1);
        ar_draw_blend(run, 1, 0, 0);
    }
    draw_pass(run, AR_DRAW_POST, 0);
    ar_draw_identity(run);
    draw_pass(run, AR_DRAW_GUI_BEGIN, 0);
    draw_pass(run, AR_DRAW_GUI, 0);
    draw_pass(run, AR_DRAW_GUI_END, 0);
    ar_draw_flush(run);
    run->target_depth = 0;
}

static void step_all(ARRun *run, int type, int subtype) {
    int count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count && run->next_room < 0 && !run->game_end; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (!inst->active || inst->destroyed) continue;
        ar_run_event(run, inst, type, subtype);
        count = ar_vm_instance_count(run->vm);
    }
}

static void motion(ARInstance *inst) {
    if (inst->friction != 0 && inst->speed != 0) {
        double s = inst->speed > 0 ? inst->speed - inst->friction : inst->speed + inst->friction;
        if ((inst->speed > 0 && s < 0) || (inst->speed < 0 && s > 0)) s = 0;
        double a = inst->direction * M_PI / 180.0;
        inst->speed = s;
        inst->hspeed = cos(a) * s;
        inst->vspeed = -sin(a) * s;
    }
    if (inst->gravity != 0) {
        double a = inst->gravity_direction * M_PI / 180.0;
        inst->hspeed += cos(a) * inst->gravity;
        inst->vspeed -= sin(a) * inst->gravity;
        inst->speed = sqrt(inst->hspeed * inst->hspeed + inst->vspeed * inst->vspeed);
        if (inst->speed != 0) {
            double d = atan2(-inst->vspeed, inst->hspeed) * 180.0 / M_PI;
            inst->direction = d < 0 ? d + 360 : d;
        }
    }
    inst->x += inst->hspeed;
    inst->y += inst->vspeed;
}

static void animate(ARRun *run, ARInstance *inst) {
    int n = ar_run_image_number(run, inst);
    if (n <= 0) return;
    const ARSprite *s = &run->data->sprites[inst->sprite_index];
    double rate = s->speed_type == 1 ? s->speed : s->speed / run->room_speed;
    inst->image_index += inst->image_speed * rate;
    if (inst->image_index >= n) {
        inst->image_index -= n;
        if (inst->image_index >= n) inst->image_index = fmod(inst->image_index, n);
        ar_run_event(run, inst, AR_EV_OTHER, AR_OTHER_ANIMATION_END);
    } else if (inst->image_index < 0) {
        inst->image_index += n;
        if (inst->image_index < 0) inst->image_index = fmod(inst->image_index, n) + n;
        ar_run_event(run, inst, AR_EV_OTHER, AR_OTHER_ANIMATION_END);
    }
}

static void collisions(ARRun *run) {
    const ARData *d = run->data;
    int count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *a = ar_vm_instance_at(run->vm, i);
        if (a->destroyed || !a->active) continue;
        // The collision events of this object and of its parents.
        for (int o = a->object, guard = 0; o >= 0 && guard < 64; o = d->objects[o].parent, guard++) {
            const ARObject *obj = &d->objects[o];
            for (int e = 0; e < obj->event_count[AR_EV_COLLISION]; e++) {
                int target = obj->events[AR_EV_COLLISION][e].subtype;
                for (int k = 0; k < count; k++) {
                    ARInstance *b = ar_vm_instance_at(run->vm, k);
                    if (b == a || b->destroyed || !b->active || !ar_vm_is_object(run->vm, b->object, target)) continue;
                    if (!ar_run_collide(run, a, a->x, a->y, b)) continue;
                    if (b->solid || a->solid) {
                        a->x = a->xprevious;
                        a->y = a->yprevious;
                    }
                    run_event_other(run, a, AR_EV_COLLISION, target, b);
                }
            }
        }
    }
}

static void keyboard_events(ARRun *run) {
    int count = ar_vm_instance_count(run->vm);
    for (int k = 0; k < 256; k++) {
        if (!run->keys[k] && !run->pressed[k] && !run->released[k]) continue;
        for (int i = 0; i < count; i++) {
            ARInstance *inst = ar_vm_instance_at(run->vm, i);
            if (!inst->active || inst->destroyed) continue;
            if (run->keys[k]) ar_run_event(run, inst, AR_EV_KEYBOARD, k);
            if (run->pressed[k]) ar_run_event(run, inst, AR_EV_KEYPRESS, k);
            if (run->released[k]) ar_run_event(run, inst, AR_EV_KEYRELEASE, k);
        }
    }
}

static void mark_more(ARVM *vm, void *context) {
    (void)vm;
    ARRun *run = context;
    for (int i = 0; i < run->ds_count; i++) {
        ARDs *ds = run->ds[i];
        if (!ds) continue;
        for (int k = 0; k < ds->count; k++) ar_mark_value(ds->items[k]);
        for (int k = 0; k < ds->entry_count; k++) {
            ar_mark_value(ds->entries[k].key);
            ar_mark_value(ds->entries[k].value);
        }
    }
    ar_audio_mark(run);
}

int ar_run_frame(ARRun *run) {
    if (run->game_end) return 0;
    // The keys of this frame. A key that goes down and up in one frame
    // stays down for this frame and goes up in the next.
    for (int k = 0; k < 256; k++) {
        int down = run->queued_down[k], up = run->queued_up[k];
        run->pressed[k] = 0;
        run->released[k] = 0;
        run->queued_down[k] = 0;
        if (down) {
            if (!run->keys[k]) run->pressed[k] = 1;
            run->keys[k] = 1;
            run->last_key = k;
        }
        if (up && !down) {
            if (run->keys[k]) run->released[k] = 1;
            run->keys[k] = 0;
            run->queued_up[k] = 0;
        }
    }
    int count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        inst->xprevious = inst->x;
        inst->yprevious = inst->y;
    }
    // Move the layers.
    for (int i = 0; i < run->layer_count; i++) {
        run->layers[i].x += run->layers[i].hspeed;
        run->layers[i].y += run->layers[i].vspeed;
    }
    for (int i = 0; i < run->element_count; i++) {
        ARElement *e = &run->elements[i];
        if (e->type == AR_ELEMENT_BACKGROUND || e->type == AR_ELEMENT_SPRITE) {
            double rate = e->speed_type == 1 ? e->speed : e->speed / run->room_speed;
            e->index += rate;
        }
    }

    step_all(run, AR_EV_STEP, AR_STEP_BEGIN);
    // The alarms.
    count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count && run->next_room < 0; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (!inst->active || inst->destroyed) continue;
        for (int a = 0; a < 12; a++) {
            if (inst->alarm[a] > 0) {
                inst->alarm[a]--;
                if (inst->alarm[a] == 0) {
                    inst->alarm[a] = -1;
                    ar_run_event(run, inst, AR_EV_ALARM, a);
                }
            }
        }
        count = ar_vm_instance_count(run->vm);
    }
    if (run->next_room < 0) keyboard_events(run);
    step_all(run, AR_EV_STEP, AR_STEP_NORMAL);
    count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (inst->active && !inst->destroyed) motion(inst);
    }
    if (run->next_room < 0) collisions(run);
    step_all(run, AR_EV_STEP, AR_STEP_END);
    count = ar_vm_instance_count(run->vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(run->vm, i);
        if (inst->active && !inst->destroyed) animate(run, inst);
    }
    ar_vm_remove_destroyed(run->vm);
    if (run->next_room >= 0 && !run->game_end) change_room(run);
    for (int v = 0; v < AR_MAX_VIEWS; v++) {
        if (run->views[v].visible) ar_run_update_camera(run, run->views[v].camera);
    }
    if (!run->game_end) draw_frame(run);
    ar_vm_remove_destroyed(run->vm);
    if (run->next_room >= 0 && !run->game_end) change_room(run);

    if (ar_heap_new_bytes(ar_vm_heap(run->vm)) > 4u * 1024 * 1024) ar_vm_collect(run->vm, mark_more, run);
    ar_audio_advance(run, 1.0 / (run->room_speed > 0 ? run->room_speed : 30));
    run->frame++;
    return !run->game_end && !run->change && !run->restart;
}

void ar_run_key(ARRun *run, int key, int down) {
    if (key < 0 || key > 255) return;
    if (down) run->queued_down[key] = 1;
    else run->queued_up[key] = 1;
}

double ar_run_speed(ARRun *run) {
    return run->room_speed;
}

int ar_run_window(ARRun *run, int *width, int *height) {
    *width = run->window_w;
    *height = run->window_h;
    return run->window_handle;
}

int ar_run_surface(ARRun *run, int surface, int *width, int *height) {
    if (surface < 0 || surface >= run->surface_count || !run->surfaces[surface].used) return 0;
    *width = run->surfaces[surface].width;
    *height = run->surfaces[surface].height;
    return run->surfaces[surface].handle;
}

long ar_run_frame_count(ARRun *run) {
    return run->frame;
}

const char *ar_run_room_name(ARRun *run) {
    if (run->room < 0 || run->room >= run->data->room_count) return "";
    return run->data->rooms[run->room].name;
}

int ar_run_change_request(ARRun *run, char *dir, size_t dir_size, char *params, size_t params_size) {
    if (!run->change) return 0;
    snprintf(dir, dir_size, "%s", run->change_dir);
    snprintf(params, params_size, "%s", run->change_params);
    return 1;
}

int ar_run_restart_request(ARRun *run) {
    return run->restart;
}

void ar_run_debug(ARRun *run, const char *command) {
    char word[64];
    if (strcmp(command, "instances") == 0) {
        int count = ar_vm_instance_count(run->vm);
        for (int i = 0; i < count; i++) {
            ARInstance *inst = ar_vm_instance_at(run->vm, i);
            fprintf(stderr, "  %d %s at %.0f,%.0f depth %.0f%s%s%s\n", inst->id,
                    run->data->objects[inst->object].name, inst->x, inst->y, inst->depth,
                    inst->active ? "" : " inactive", inst->destroyed ? " destroyed" : "",
                    inst->persistent ? " persistent" : "");
        }
        return;
    }
    if (strcmp(command, "layers") == 0) {
        for (int i = 0; i < run->layer_count; i++) {
            ARLayerState *l = &run->layers[i];
            fprintf(stderr, "  layer %d %s depth %.0f at %.0f,%.0f%s\n", l->id, l->name, l->depth, l->x, l->y,
                    l->visible ? "" : " hidden");
            for (int k = 0; k < l->element_count; k++) {
                ARElement *e = ar_run_element(run, l->elements[k]);
                if (!e) continue;
                fprintf(stderr, "    element %d type %d sprite %s at %.0f,%.0f%s\n", e->id, e->type,
                        e->sprite >= 0 && e->sprite < run->data->sprite_count ? run->data->sprites[e->sprite].name : "-",
                        e->x, e->y, e->visible ? "" : " hidden");
            }
        }
        for (int v = 0; v < AR_MAX_VIEWS; v++) {
            ARView *view = &run->views[v];
            if (!view->visible) continue;
            ARCamera *c = &run->cameras[view->camera];
            fprintf(stderr, "  view %d camera %d %.0f,%.0f %.0fx%.0f port %d,%d %dx%d\n", v, view->camera, c->x, c->y, c->w,
                    c->h, view->xport, view->yport, view->wport, view->hport);
        }
        return;
    }
    if (sscanf(command, "room %63s", word) == 1) {
        int r = ar_data_find_room(run->data, word);
        if (r < 0) r = atoi(word);
        ar_run_goto_room(run, r);
    }
}

// ----------------------------------------------------- builtin variables

#define INST_REAL(field)                                                                \
    static ARValue get_##field(ARVM *vm, ARInstance *s, int i) {                        \
        (void)vm;                                                                       \
        (void)i;                                                                        \
        return ar_real(s ? s->field : 0);                                               \
    }                                                                                   \
    static void set_##field(ARVM *vm, ARInstance *s, int i, ARValue v) {                \
        (void)vm;                                                                       \
        (void)i;                                                                        \
        if (s) s->field = ar_to_real(v);                                                \
    }

INST_REAL(x)
INST_REAL(y)
INST_REAL(xprevious)
INST_REAL(yprevious)
INST_REAL(xstart)
INST_REAL(ystart)
INST_REAL(friction)
INST_REAL(gravity)
INST_REAL(gravity_direction)
INST_REAL(image_speed)
INST_REAL(image_xscale)
INST_REAL(image_yscale)
INST_REAL(image_angle)
INST_REAL(image_alpha)

static ARValue get_image_index(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    if (!s) return ar_real(0);
    ARRun *run = ar_run_of(vm);
    int n = ar_run_image_number(run, s);
    double v = s->image_index;
    // A read gives the index in the frames of the sprite.
    if (n > 0 && (v >= n || v < 0)) {
        v = fmod(v, n);
        if (v < 0) v += n;
    }
    return ar_real(v);
}

static void set_image_index(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (s) s->image_index = ar_to_real(v);
}

static void set_speeds_from_hv(ARInstance *s) {
    s->speed = sqrt(s->hspeed * s->hspeed + s->vspeed * s->vspeed);
    if (s->hspeed != 0 || s->vspeed != 0) {
        double d = atan2(-s->vspeed, s->hspeed) * 180.0 / M_PI;
        s->direction = d < 0 ? d + 360 : d;
    }
}

static void set_speeds_from_polar(ARInstance *s) {
    double a = s->direction * M_PI / 180.0;
    s->hspeed = cos(a) * s->speed;
    s->vspeed = -sin(a) * s->speed;
    if (fabs(s->hspeed) < 1e-10) s->hspeed = 0;
    if (fabs(s->vspeed) < 1e-10) s->vspeed = 0;
}

static ARValue get_hspeed(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->hspeed : 0);
}

static void set_hspeed(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (!s) return;
    s->hspeed = ar_to_real(v);
    set_speeds_from_hv(s);
}

static ARValue get_vspeed(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->vspeed : 0);
}

static void set_vspeed(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (!s) return;
    s->vspeed = ar_to_real(v);
    set_speeds_from_hv(s);
}

static ARValue get_speed(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->speed : 0);
}

static void set_speed(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (!s) return;
    s->speed = ar_to_real(v);
    set_speeds_from_polar(s);
}

static ARValue get_direction(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->direction : 0);
}

static void set_direction(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (!s) return;
    double d = fmod(ar_to_real(v), 360);
    if (d < 0) d += 360;
    s->direction = d;
    set_speeds_from_polar(s);
}

static ARValue get_sprite_index(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->sprite_index : -1);
}

static void set_sprite_index(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (!s) return;
    int n = ar_to_int(v);
    // A new sprite keeps the index, as in GameMaker.
    s->sprite_index = n;
}

static ARValue get_mask_index(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->mask_index : -1);
}

static void set_mask_index(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (s) s->mask_index = ar_to_int(v);
}

static ARValue get_image_blend(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->image_blend : 0xFFFFFF);
}

static void set_image_blend(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)i;
    if (s) s->image_blend = ar_to_int(v) & 0xFFFFFF;
}

static ARValue get_image_number(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    return ar_real(s ? ar_run_image_number(ar_run_of(vm), s) : 0);
}

static ARValue get_sprite_width(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    if (!s || s->sprite_index < 0 || s->sprite_index >= ar_run_of(vm)->data->sprite_count) return ar_real(0);
    return ar_real(ar_run_of(vm)->data->sprites[s->sprite_index].width * fabs(s->image_xscale));
}

static ARValue get_sprite_height(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    if (!s || s->sprite_index < 0 || s->sprite_index >= ar_run_of(vm)->data->sprite_count) return ar_real(0);
    return ar_real(ar_run_of(vm)->data->sprites[s->sprite_index].height * fabs(s->image_yscale));
}

static ARValue get_depth(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->depth : 0);
}

static void set_depth(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)i;
    if (!s) return;
    s->depth = ar_to_real(v);
    s->layer = ar_run_layer_for_depth(ar_run_of(vm), s->depth);
}

#define INST_FLAG(field)                                                                \
    static ARValue get_##field(ARVM *vm, ARInstance *s, int i) {                        \
        (void)vm;                                                                       \
        (void)i;                                                                        \
        return ar_bool(s ? s->field : 0);                                               \
    }                                                                                   \
    static void set_##field(ARVM *vm, ARInstance *s, int i, ARValue v) {                \
        (void)vm;                                                                       \
        (void)i;                                                                        \
        if (s) s->field = (uint8_t)ar_truthy(v);                                        \
    }

INST_FLAG(visible)
INST_FLAG(solid)
INST_FLAG(persistent)

static ARValue get_id(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->id : AR_NOONE);
}

static ARValue get_object_index(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->object : -1);
}

static ARValue get_alarm(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    if (!s || i < 0 || i >= 12) return ar_real(-1);
    return ar_real(s->alarm[i]);
}

static void set_alarm(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    if (!s) return;
    if (i < 0) i = 0;
    if (i < 12) s->alarm[i] = ar_to_int(v);
}

static ARValue get_bbox(ARVM *vm, ARInstance *s, int which) {
    if (!s) return ar_real(0);
    double l, t, r, b;
    ar_run_bbox(ar_run_of(vm), s, &l, &t, &r, &b);
    switch (which) {
    case 0: return ar_real(l);
    case 1: return ar_real(t);
    case 2: return ar_real(r - 1);
    default: return ar_real(b - 1);
    }
}

static ARValue get_bbox_left(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    return get_bbox(vm, s, 0);
}

static ARValue get_bbox_top(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    return get_bbox(vm, s, 1);
}

static ARValue get_bbox_right(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    return get_bbox(vm, s, 2);
}

static ARValue get_bbox_bottom(ARVM *vm, ARInstance *s, int i) {
    (void)i;
    return get_bbox(vm, s, 3);
}

static ARValue get_layer(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)i;
    return ar_real(s ? s->layer : -1);
}

static void set_layer(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)i;
    if (!s) return;
    ARLayerState *l = ar_run_layer(ar_run_of(vm), ar_to_int(v));
    if (!l) return;
    s->layer = l->id;
    s->depth = l->depth;
}

// The variables of the path functions. The game sets them; no path moves.
static ARValue get_zero(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)s;
    (void)i;
    return ar_real(0);
}

static void set_nothing(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)vm;
    (void)s;
    (void)i;
    (void)v;
}

static ARValue get_room(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->room);
}

static void set_room(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    ar_run_goto_room(ar_run_of(vm), ar_to_int(v));
}

static ARValue get_room_width(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->room_w);
}

static void set_room_width(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    ar_run_of(vm)->room_w = ar_to_int(v);
}

static ARValue get_room_height(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->room_h);
}

static void set_room_height(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    ar_run_of(vm)->room_h = ar_to_int(v);
}

static ARValue get_room_speed(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->room_speed);
}

static void set_room_speed(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    double r = ar_to_real(v);
    if (r > 0) ar_run_of(vm)->room_speed = r;
}

static ARValue get_room_persistent(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_bool(ar_run_of(vm)->room_persistent);
}

static void set_room_persistent(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    ar_run_of(vm)->room_persistent = ar_truthy(v);
}

static ARValue get_fps(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->room_speed);
}

static ARValue get_os_type(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)s;
    (void)i;
    return ar_real(1);  // os_macosx: the data file is of the Mac version.
}

static ARValue get_working_directory(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    ARRun *run = ar_run_of(vm);
    char text[1100];
    snprintf(text, sizeof(text), "%s/", run->game_dir);
    return ar_strv(ar_str_cstr(ar_vm_heap(vm), text));
}

static ARValue get_application_surface(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)s;
    (void)i;
    return ar_real(0);
}

static ARValue get_undefined(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)s;
    (void)i;
    return ar_undefined();
}

static ARValue get_view_camera(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    if (i < 0) i = 0;
    if (i >= AR_MAX_VIEWS) return ar_real(-1);
    return ar_real(ar_run_of(vm)->views[i].camera);
}

static void set_view_camera(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    if (i < 0) i = 0;
    if (i < AR_MAX_VIEWS) ar_run_of(vm)->views[i].camera = ar_to_int(v);
}

static ARValue get_view_current(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->view_current);
}

static ARValue get_view_wport(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    if (i < 0) i = 0;
    if (i >= AR_MAX_VIEWS) return ar_real(0);
    return ar_real(ar_run_of(vm)->views[i].wport);
}

static void set_view_wport(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    if (i < 0) i = 0;
    if (i < AR_MAX_VIEWS) ar_run_of(vm)->views[i].wport = ar_to_int(v);
}

static ARValue get_async_load(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    ARRun *run = ar_run_of(vm);
    ARValue *v = ar_vars_find(&ar_vm_global(vm)->vars, run->slot_async_load);
    return v ? *v : ar_real(-1);
}

static ARValue get_event_type(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    ARRun *run = ar_run_of(vm);
    return ar_real(run->event_depth ? run->event_stack[run->event_depth - 1].type : -1);
}

static ARValue get_event_number(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    ARRun *run = ar_run_of(vm);
    return ar_real(run->event_depth ? run->event_stack[run->event_depth - 1].subtype : -1);
}

static ARValue get_keyboard_key(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    ARRun *run = ar_run_of(vm);
    for (int k = 255; k > 0; k--) {
        if (run->keys[k]) return ar_real(k);
    }
    return ar_real(0);
}

static ARValue get_keyboard_lastkey(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(ar_run_of(vm)->last_key);
}

static void set_keyboard_lastkey(ARVM *vm, ARInstance *s, int i, ARValue v) {
    (void)s;
    (void)i;
    ar_run_of(vm)->last_key = ar_to_int(v);
}

static ARValue get_current_time(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    return ar_real(floor(ar_run_time_ms() - ar_run_of(vm)->start_time));
}

static ARValue get_instance_count(ARVM *vm, ARInstance *s, int i) {
    (void)s;
    (void)i;
    int n = 0, count = ar_vm_instance_count(vm);
    for (int k = 0; k < count; k++) n += !ar_vm_instance_at(vm, k)->destroyed;
    return ar_real(n);
}

static ARValue get_debug_mode(ARVM *vm, ARInstance *s, int i) {
    (void)vm;
    (void)s;
    (void)i;
    return ar_bool(0);
}

static const ARBuiltinVar kVariables[] = {
    {"x", get_x, set_x, 1},
    {"y", get_y, set_y, 1},
    {"xprevious", get_xprevious, set_xprevious, 1},
    {"yprevious", get_yprevious, set_yprevious, 1},
    {"xstart", get_xstart, set_xstart, 1},
    {"ystart", get_ystart, set_ystart, 1},
    {"hspeed", get_hspeed, set_hspeed, 1},
    {"vspeed", get_vspeed, set_vspeed, 1},
    {"speed", get_speed, set_speed, 1},
    {"direction", get_direction, set_direction, 1},
    {"friction", get_friction, set_friction, 1},
    {"gravity", get_gravity, set_gravity, 1},
    {"gravity_direction", get_gravity_direction, set_gravity_direction, 1},
    {"sprite_index", get_sprite_index, set_sprite_index, 1},
    {"image_index", get_image_index, set_image_index, 1},
    {"image_speed", get_image_speed, set_image_speed, 1},
    {"image_xscale", get_image_xscale, set_image_xscale, 1},
    {"image_yscale", get_image_yscale, set_image_yscale, 1},
    {"image_angle", get_image_angle, set_image_angle, 1},
    {"image_alpha", get_image_alpha, set_image_alpha, 1},
    {"image_blend", get_image_blend, set_image_blend, 1},
    {"image_number", get_image_number, NULL, 1},
    {"sprite_width", get_sprite_width, NULL, 1},
    {"sprite_height", get_sprite_height, NULL, 1},
    {"mask_index", get_mask_index, set_mask_index, 1},
    {"depth", get_depth, set_depth, 1},
    {"visible", get_visible, set_visible, 1},
    {"solid", get_solid, set_solid, 1},
    {"persistent", get_persistent, set_persistent, 1},
    {"id", get_id, NULL, 1},
    {"object_index", get_object_index, NULL, 1},
    {"alarm", get_alarm, set_alarm, 1},
    {"bbox_left", get_bbox_left, NULL, 1},
    {"bbox_top", get_bbox_top, NULL, 1},
    {"bbox_right", get_bbox_right, NULL, 1},
    {"bbox_bottom", get_bbox_bottom, NULL, 1},
    {"layer", get_layer, set_layer, 1},
    {"path_position", get_zero, set_nothing, 1},
    {"path_speed", get_zero, set_nothing, 1},
    {"room", get_room, set_room, 0},
    {"room_width", get_room_width, set_room_width, 0},
    {"room_height", get_room_height, set_room_height, 0},
    {"room_speed", get_room_speed, set_room_speed, 0},
    {"room_persistent", get_room_persistent, set_room_persistent, 0},
    {"fps", get_fps, NULL, 0},
    {"fps_real", get_fps, NULL, 0},
    {"os_type", get_os_type, NULL, 0},
    {"working_directory", get_working_directory, NULL, 0},
    {"program_directory", get_working_directory, NULL, 0},
    {"application_surface", get_application_surface, NULL, 0},
    {"undefined", get_undefined, NULL, 0},
    {"view_camera", get_view_camera, set_view_camera, 0},
    {"view_current", get_view_current, NULL, 0},
    {"view_wport", get_view_wport, set_view_wport, 0},
    {"async_load", get_async_load, NULL, 0},
    {"event_type", get_event_type, NULL, 0},
    {"event_number", get_event_number, NULL, 0},
    {"keyboard_key", get_keyboard_key, NULL, 0},
    {"keyboard_lastkey", get_keyboard_lastkey, set_keyboard_lastkey, 0},
    {"current_time", get_current_time, NULL, 0},
    {"instance_count", get_instance_count, NULL, 0},
    {"debug_mode", get_debug_mode, NULL, 0},
};

// ------------------------------------------------------------- the runtime

ARRun *ar_run_new(const ARData *data, const ARRunConfig *config, char *error, size_t size) {
    ar_vm_register(NULL, 0, kVariables, (int)(sizeof(kVariables) / sizeof(kVariables[0])));
    ar_lib_register_all();
    ARRun *run = calloc(1, sizeof(ARRun));
    run->data = data;
    run->gfx = config->gfx;
    run->audio_host = config->audio;
    copy_dir(run->game_dir, sizeof(run->game_dir), config->game_dir);
    copy_dir(run->save_dir, sizeof(run->save_dir), config->save_dir ? config->save_dir : config->game_dir);
    if (config->music_dir) copy_dir(run->music_dir, sizeof(run->music_dir), config->music_dir);
    run->parameters = config->parameters;
    run->parameter_count = config->parameter_count;
    run->vm = ar_vm_new(data);
    if (!run->vm) {
        snprintf(error, size, "the code of the game does not link");
        free(run);
        return NULL;
    }
    ar_vm_set_user(run->vm, run);
    run->slot_async_load = ar_vm_slot(run->vm, "@@async_load@@");
    run->room = -1;
    run->next_room = -1;
    run->room_speed = data->fps;
    run->window_w = data->window_w > 0 ? data->window_w : 640;
    run->window_h = data->window_h > 0 ? data->window_h : 480;
    run->draw_color = 0xFFFFFF;
    run->draw_alpha = 1;
    run->draw_font = -1;
    run->blend_enabled = 1;
    run->app_surface_draw = 1;
    run->random_state = 0x9E3779B97F4A7C15ULL;
    run->start_time = ar_run_time_ms();
    for (int i = 0; i < 64; i++) run->buffers[i].used = 0;
    ar_draw_init(run);
    ar_audio_init(run);
    return run;
}

void ar_run_free(ARRun *run) {
    if (!run) return;
    ar_audio_free(run);
    ar_draw_free(run);
    clear_room_state(run);
    free(run->layers);
    free(run->elements);
    for (int i = 0; i < run->ds_count; i++) {
        ARDs *ds = run->ds[i];
        if (!ds) continue;
        free(ds->items);
        free(ds->entries);
        free(ds->index);
        free(ds);
    }
    free(run->ds);
    for (int i = 0; i < 32; i++) {
        if (run->files[i].file) fclose(run->files[i].file);
    }
    for (int i = 0; i < run->ini.count; i++) {
        free(run->ini.keys[i].section);
        free(run->ini.keys[i].key);
        free(run->ini.keys[i].value);
    }
    free(run->ini.keys);
    for (int i = 0; i < 64; i++) free(run->buffers[i].bytes);
    free(run->sprite_fonts);
    ar_vm_free(run->vm);
    free(run);
}

void ar_run_start(ARRun *run) {
    ar_vm_run_global_scripts(run->vm);
    int first = run->data->room_order_count > 0 ? (int)run->data->room_order[0] : 0;
    enter_room(run, first, 1);
    if (run->next_room >= 0) change_room(run);
}
