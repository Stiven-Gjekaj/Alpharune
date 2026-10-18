// The builtin functions of GML for instances, collisions, rooms, layers,
// cameras, drawing, sprites, surfaces, and the keyboard.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ar_run_internal.h"

#define FN(name) static ARValue name(ARVM *vm, int argc, ARValue *a)
#define UNUSED (void)vm, (void)argc, (void)a
#define HEAP ar_vm_heap(vm)
#define RUN ar_run_of(vm)
#define STR(text) ar_strv(ar_str_cstr(HEAP, text))
#define ARG_STR(i) (i < argc ? ar_to_str(HEAP, a[i]) : ar_str_cstr(HEAP, ""))
#define REAL(i, d) ar_arg_real(argc, a, i, d)
#define INT(i, d) ar_arg_int(argc, a, i, d)

static ARInstance *self_of(ARVM *vm) {
    return ar_vm_self_instance(vm);
}

// ------------------------------------------------------------- instances

FN(f_instance_create_depth) {
    UNUSED;
    ARInstance *inst = ar_run_instance_create(RUN, REAL(0, 0), REAL(1, 0), INT(3, -1), REAL(2, 0), -1);
    if (!inst) return ar_real(AR_NOONE);
    inst->layer = ar_run_layer_for_depth(RUN, inst->depth);
    return ar_real(inst->id);
}

FN(f_instance_create_layer) {
    UNUSED;
    ARRun *run = RUN;
    int layer = -1;
    if (argc > 2 && a[2].type == AR_STRING) {
        for (int i = 0; i < run->layer_count; i++) {
            if (strcmp(run->layers[i].name, a[2].s->text) == 0) layer = run->layers[i].id;
        }
    } else {
        layer = INT(2, -1);
    }
    ARLayerState *l = ar_run_layer(run, layer);
    ARInstance *inst = ar_run_instance_create(run, REAL(0, 0), REAL(1, 0), INT(3, -1), l ? l->depth : 0, l ? layer : -1);
    return ar_real(inst ? inst->id : AR_NOONE);
}

FN(f_instance_destroy) {
    UNUSED;
    ARRun *run = RUN;
    if (argc == 0) {
        ar_run_instance_destroy(run, self_of(vm), 1);
        return ar_undefined();
    }
    int run_event = argc > 1 ? ar_truthy(a[1]) : 1;
    ARStruct *targets[1024];
    int n = ar_vm_targets(vm, a[0], targets, 1024);
    if (n > 1024) n = 1024;
    for (int i = 0; i < n; i++) {
        if (targets[i]->instance) ar_run_instance_destroy(run, targets[i]->instance, run_event);
    }
    return ar_undefined();
}

FN(f_instance_exists) {
    UNUSED;
    if (argc == 0) return ar_bool(0);
    return ar_bool(ar_run_instance(RUN, a[0]) != NULL);
}

FN(f_instance_number) {
    UNUSED;
    int obj = INT(0, -1), n = 0, count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (!inst->destroyed && inst->active && (obj == AR_ALL || ar_vm_is_object(vm, inst->object, obj))) n++;
    }
    return ar_real(n);
}

FN(f_instance_find) {
    UNUSED;
    int obj = INT(0, -1), k = INT(1, 0), n = 0, count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (inst->destroyed || !inst->active) continue;
        if (obj != AR_ALL && !ar_vm_is_object(vm, inst->object, obj)) continue;
        if (n++ == k) return ar_real(inst->id);
    }
    return ar_real(AR_NOONE);
}

static void set_active(ARVM *vm, int object, int active, int not_self) {
    ARInstance *self = self_of(vm);
    int count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (inst->destroyed) continue;
        if (not_self && inst == self) continue;
        if (object == AR_ALL || ar_vm_is_object(vm, inst->object, object) || inst->id == object) {
            inst->active = (uint8_t)active;
        }
    }
}

FN(f_instance_activate_all) {
    UNUSED;
    set_active(vm, AR_ALL, 1, 0);
    return ar_undefined();
}

FN(f_instance_deactivate_all) {
    UNUSED;
    set_active(vm, AR_ALL, 0, argc > 0 && ar_truthy(a[0]));
    return ar_undefined();
}

FN(f_instance_activate_object) {
    UNUSED;
    set_active(vm, INT(0, -1), 1, 0);
    return ar_undefined();
}

FN(f_instance_deactivate_object) {
    UNUSED;
    set_active(vm, INT(0, -1), 0, 0);
    return ar_undefined();
}

FN(f_object_get_sprite) {
    UNUSED;
    int o = INT(0, -1);
    if (o < 0 || o >= ar_vm_data(vm)->object_count) return ar_real(-1);
    return ar_real(ar_vm_data(vm)->objects[o].sprite);
}

FN(f_object_get_name) {
    UNUSED;
    int o = INT(0, -1);
    if (o < 0 || o >= ar_vm_data(vm)->object_count) return STR("<undefined>");
    return STR(ar_vm_data(vm)->objects[o].name);
}

FN(f_object_exists) {
    UNUSED;
    int o = INT(0, -1);
    return ar_bool(o >= 0 && o < ar_vm_data(vm)->object_count);
}

FN(f_object_is_ancestor) {
    UNUSED;
    return ar_bool(INT(0, -1) != INT(1, -1) && ar_vm_is_object(vm, INT(0, -1), INT(1, -1)));
}

// ------------------------------------------------------------- collisions

// The instances of an object or one instance, for a collision test.
static int targets_of(ARVM *vm, ARValue obj, ARInstance **out, int max) {
    ARStruct *s[2048];
    int n = ar_vm_targets(vm, obj, s, 2048);
    if (n > 2048) n = 2048;
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (s[i]->instance) out[k++] = s[i]->instance;
    }
    return k;
}

static ARInstance *gTargets[2048];

static ARInstance *place(ARVM *vm, ARInstance *self, double x, double y, ARValue obj) {
    if (!self) return NULL;
    int n = targets_of(vm, obj, gTargets, 2048);
    for (int i = 0; i < n; i++) {
        if (gTargets[i] == self || gTargets[i]->destroyed) continue;
        if (ar_run_collide(RUN, self, x, y, gTargets[i])) return gTargets[i];
    }
    return NULL;
}

FN(f_place_meeting) {
    UNUSED;
    return ar_bool(argc > 2 && place(vm, self_of(vm), REAL(0, 0), REAL(1, 0), a[2]) != NULL);
}

FN(f_instance_place) {
    UNUSED;
    ARInstance *hit = argc > 2 ? place(vm, self_of(vm), REAL(0, 0), REAL(1, 0), a[2]) : NULL;
    return ar_real(hit ? hit->id : AR_NOONE);
}

FN(f_place_free) {
    UNUSED;
    ARInstance *self = self_of(vm);
    if (!self) return ar_bool(1);
    int count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *b = ar_vm_instance_at(vm, i);
        if (b == self || b->destroyed || !b->active || !b->solid) continue;
        if (ar_run_collide(RUN, self, REAL(0, 0), REAL(1, 0), b)) return ar_bool(0);
    }
    return ar_bool(1);
}

FN(f_place_empty) {
    UNUSED;
    ARValue all = ar_real(AR_ALL);
    return ar_bool(place(vm, self_of(vm), REAL(0, 0), REAL(1, 0), argc > 2 ? a[2] : all) == NULL);
}

static ARInstance *at_point(ARVM *vm, double x, double y, ARValue obj, int precise, int notme) {
    ARInstance *self = self_of(vm);
    int n = targets_of(vm, obj, gTargets, 2048);
    for (int i = 0; i < n; i++) {
        ARInstance *b = gTargets[i];
        if (b->destroyed || (notme && b == self)) continue;
        if (ar_run_collide_point(RUN, b, x, y, precise)) return b;
    }
    return NULL;
}

FN(f_position_meeting) {
    UNUSED;
    return ar_bool(argc > 2 && at_point(vm, REAL(0, 0), REAL(1, 0), a[2], 1, 0) != NULL);
}

FN(f_instance_position) {
    UNUSED;
    ARInstance *hit = argc > 2 ? at_point(vm, REAL(0, 0), REAL(1, 0), a[2], 1, 0) : NULL;
    return ar_real(hit ? hit->id : AR_NOONE);
}

FN(f_collision_point) {
    UNUSED;
    ARInstance *hit = argc > 2 ? at_point(vm, REAL(0, 0), REAL(1, 0), a[2], argc > 3 && ar_truthy(a[3]),
                                          argc > 4 && ar_truthy(a[4]))
                               : NULL;
    return ar_real(hit ? hit->id : AR_NOONE);
}

FN(f_collision_rectangle) {
    UNUSED;
    if (argc < 5) return ar_real(AR_NOONE);
    ARInstance *self = self_of(vm);
    int precise = argc > 5 && ar_truthy(a[5]), notme = argc > 6 && ar_truthy(a[6]);
    int n = targets_of(vm, a[4], gTargets, 2048);
    for (int i = 0; i < n; i++) {
        ARInstance *b = gTargets[i];
        if (b->destroyed || (notme && b == self)) continue;
        if (ar_run_collide_rect(RUN, b, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), precise)) {
            return ar_real(b->id);
        }
    }
    return ar_real(AR_NOONE);
}

FN(f_collision_line) {
    UNUSED;
    if (argc < 5) return ar_real(AR_NOONE);
    ARInstance *self = self_of(vm);
    double x1 = REAL(0, 0), y1 = REAL(1, 0), x2 = REAL(2, 0), y2 = REAL(3, 0);
    int precise = argc > 5 && ar_truthy(a[5]), notme = argc > 6 && ar_truthy(a[6]);
    int n = targets_of(vm, a[4], gTargets, 2048);
    double len = sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    int steps = (int)ceil(len) + 1;
    for (int i = 0; i < n; i++) {
        ARInstance *b = gTargets[i];
        if (b->destroyed || (notme && b == self)) continue;
        double l, t, r, bt;
        if (!ar_run_bbox(RUN, b, &l, &t, &r, &bt)) continue;
        // A quick test with the box of the line.
        if ((x1 < l && x2 < l) || (x1 >= r && x2 >= r) || (y1 < t && y2 < t) || (y1 >= bt && y2 >= bt)) continue;
        for (int s = 0; s <= steps; s++) {
            double f = steps ? (double)s / steps : 0;
            if (ar_run_collide_point(RUN, b, x1 + (x2 - x1) * f, y1 + (y2 - y1) * f, precise)) return ar_real(b->id);
        }
    }
    return ar_real(AR_NOONE);
}

FN(f_collision_circle) {
    UNUSED;
    if (argc < 4) return ar_real(AR_NOONE);
    ARInstance *self = self_of(vm);
    double cx = REAL(0, 0), cy = REAL(1, 0), rad = REAL(2, 0);
    int notme = argc > 5 && ar_truthy(a[5]);
    int n = targets_of(vm, a[3], gTargets, 2048);
    for (int i = 0; i < n; i++) {
        ARInstance *b = gTargets[i];
        if (b->destroyed || (notme && b == self)) continue;
        double l, t, r, bt;
        if (!ar_run_bbox(RUN, b, &l, &t, &r, &bt)) continue;
        double nx = cx < l ? l : cx > r ? r : cx, ny = cy < t ? t : cy > bt ? bt : cy;
        if ((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) <= rad * rad) return ar_real(b->id);
    }
    return ar_real(AR_NOONE);
}

static double box_distance(ARRun *run, ARInstance *a, ARInstance *b) {
    double al, at, ar, ab, bl, bt, br, bb;
    ar_run_bbox(run, a, &al, &at, &ar, &ab);
    ar_run_bbox(run, b, &bl, &bt, &br, &bb);
    double dx = bl > ar ? bl - ar : al > br ? al - br : 0;
    double dy = bt > ab ? bt - ab : at > bb ? at - bb : 0;
    return sqrt(dx * dx + dy * dy);
}

FN(f_distance_to_object) {
    UNUSED;
    ARInstance *self = self_of(vm);
    if (!self || argc == 0) return ar_real(1000000);
    int n = targets_of(vm, a[0], gTargets, 2048);
    double best = 1000000;
    for (int i = 0; i < n; i++) {
        if (gTargets[i] == self) continue;
        double d = box_distance(RUN, self, gTargets[i]);
        if (d < best) best = d;
    }
    return ar_real(best);
}

FN(f_distance_to_point) {
    UNUSED;
    ARInstance *self = self_of(vm);
    if (!self) return ar_real(0);
    double l, t, r, b, px = REAL(0, 0), py = REAL(1, 0);
    ar_run_bbox(RUN, self, &l, &t, &r, &b);
    double dx = px < l ? l - px : px > r ? px - r : 0;
    double dy = py < t ? t - py : py > b ? py - b : 0;
    return ar_real(sqrt(dx * dx + dy * dy));
}

FN(f_instance_nearest) {
    UNUSED;
    if (argc < 3) return ar_real(AR_NOONE);
    int n = targets_of(vm, a[2], gTargets, 2048);
    ARInstance *best = NULL;
    double bd = 0, x = REAL(0, 0), y = REAL(1, 0);
    for (int i = 0; i < n; i++) {
        double d = (gTargets[i]->x - x) * (gTargets[i]->x - x) + (gTargets[i]->y - y) * (gTargets[i]->y - y);
        if (!best || d < bd) {
            best = gTargets[i];
            bd = d;
        }
    }
    return ar_real(best ? best->id : AR_NOONE);
}

static void set_motion(ARInstance *s, double dir, double speed) {
    s->direction = fmod(dir, 360);
    if (s->direction < 0) s->direction += 360;
    s->speed = speed;
    double r = s->direction * M_PI / 180;
    s->hspeed = cos(r) * speed;
    s->vspeed = -sin(r) * speed;
}

FN(f_move_towards_point) {
    UNUSED;
    ARInstance *s = self_of(vm);
    if (!s) return ar_undefined();
    double dx = REAL(0, 0) - s->x, dy = REAL(1, 0) - s->y;
    double dir = (dx == 0 && dy == 0) ? s->direction : atan2(-dy, dx) * 180 / M_PI;
    set_motion(s, dir, REAL(2, 0));
    return ar_undefined();
}

FN(f_motion_set) {
    UNUSED;
    ARInstance *s = self_of(vm);
    if (s) set_motion(s, REAL(0, 0), REAL(1, 0));
    return ar_undefined();
}

FN(f_motion_add) {
    UNUSED;
    ARInstance *s = self_of(vm);
    if (!s) return ar_undefined();
    double r = REAL(0, 0) * M_PI / 180;
    s->hspeed += cos(r) * REAL(1, 0);
    s->vspeed -= sin(r) * REAL(1, 0);
    s->speed = sqrt(s->hspeed * s->hspeed + s->vspeed * s->vspeed);
    if (s->speed != 0) {
        double d = atan2(-s->vspeed, s->hspeed) * 180 / M_PI;
        s->direction = d < 0 ? d + 360 : d;
    }
    return ar_undefined();
}

FN(f_nothing) {
    UNUSED;
    return ar_undefined();
}

FN(f_zero) {
    UNUSED;
    return ar_real(0);
}

FN(f_minus_one) {
    UNUSED;
    return ar_real(-1);
}

FN(f_true) {
    UNUSED;
    return ar_bool(1);
}

FN(f_false) {
    UNUSED;
    return ar_bool(0);
}

// ------------------------------------------------------------------ rooms

FN(f_room_goto) {
    UNUSED;
    ar_run_goto_room(RUN, INT(0, -1));
    return ar_undefined();
}

static int room_step(ARRun *run, int room, int delta) {
    const ARData *d = run->data;
    for (int i = 0; i < d->room_order_count; i++) {
        if ((int)d->room_order[i] == room) {
            int k = i + delta;
            if (k < 0 || k >= d->room_order_count) return -1;
            return (int)d->room_order[k];
        }
    }
    return -1;
}

FN(f_room_goto_next) {
    UNUSED;
    ar_run_goto_room(RUN, room_step(RUN, RUN->room, 1));
    return ar_undefined();
}

FN(f_room_goto_previous) {
    UNUSED;
    ar_run_goto_room(RUN, room_step(RUN, RUN->room, -1));
    return ar_undefined();
}

FN(f_room_next) {
    UNUSED;
    return ar_real(room_step(RUN, INT(0, -1), 1));
}

FN(f_room_previous) {
    UNUSED;
    return ar_real(room_step(RUN, INT(0, -1), -1));
}

FN(f_room_restart) {
    UNUSED;
    ar_run_goto_room(RUN, RUN->room);
    return ar_undefined();
}

FN(f_room_exists) {
    UNUSED;
    int r = INT(0, -1);
    return ar_bool(r >= 0 && r < ar_vm_data(vm)->room_count);
}

FN(f_room_get_name) {
    UNUSED;
    int r = INT(0, -1);
    if (r < 0 || r >= ar_vm_data(vm)->room_count) return STR("<undefined>");
    return STR(ar_vm_data(vm)->rooms[r].name);
}

// ----------------------------------------------------------------- layers

static ARLayerState *layer_arg(ARVM *vm, int argc, ARValue *a, int i) {
    ARRun *run = RUN;
    if (i >= argc) return NULL;
    if (a[i].type == AR_STRING) {
        for (int k = 0; k < run->layer_count; k++) {
            if (strcmp(run->layers[k].name, a[i].s->text) == 0) return &run->layers[k];
        }
        return NULL;
    }
    return ar_run_layer(run, ar_to_int(a[i]));
}

FN(f_layer_get_id) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    return ar_real(l ? l->id : -1);
}

FN(f_layer_exists) {
    UNUSED;
    return ar_bool(layer_arg(vm, argc, a, 0) != NULL);
}

FN(f_layer_create) {
    UNUSED;
    ARRun *run = RUN;
    double depth = REAL(0, 0);
    if (run->layer_count == run->layer_capacity) {
        run->layer_capacity = run->layer_capacity ? run->layer_capacity * 2 : 32;
        run->layers = realloc(run->layers, sizeof(ARLayerState) * (size_t)run->layer_capacity);
    }
    ARLayerState *l = &run->layers[run->layer_count++];
    memset(l, 0, sizeof(*l));
    l->id = run->next_layer_id++;
    l->depth = depth;
    l->visible = 1;
    if (argc > 1) snprintf(l->name, sizeof(l->name), "%s", ar_to_str(HEAP, a[1])->text);
    else snprintf(l->name, sizeof(l->name), "_layer_%d", l->id);
    return ar_real(l->id);
}

FN(f_layer_destroy) {
    UNUSED;
    ARRun *run = RUN;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    if (!l) return ar_undefined();
    int id = l->id;
    // The instances on the layer go too.
    int count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (inst->layer == id) ar_run_instance_destroy(run, inst, 0);
    }
    while (l->element_count > 0) ar_run_element_remove(run, l->elements[l->element_count - 1]);
    free(l->elements);
    int index = (int)(l - run->layers);
    memmove(&run->layers[index], &run->layers[index + 1], sizeof(ARLayerState) * (size_t)(run->layer_count - index - 1));
    run->layer_count--;
    return ar_undefined();
}

FN(f_layer_depth) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    if (!l) return ar_undefined();
    l->depth = REAL(1, 0);
    int count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (inst->layer == l->id) inst->depth = l->depth;
    }
    return ar_undefined();
}

FN(f_layer_get_depth) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    return ar_real(l ? l->depth : -1);
}

FN(f_layer_get_name) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    return STR(l ? l->name : "");
}

#define LAYER_FIELD(get, set, field)                                    \
    FN(get) {                                                           \
        UNUSED;                                                         \
        ARLayerState *l = layer_arg(vm, argc, a, 0);                    \
        return ar_real(l ? l->field : 0);                               \
    }                                                                   \
    FN(set) {                                                           \
        UNUSED;                                                         \
        ARLayerState *l = layer_arg(vm, argc, a, 0);                    \
        if (l) l->field = REAL(1, 0);                                   \
        return ar_undefined();                                          \
    }

LAYER_FIELD(f_layer_get_x, f_layer_x, x)
LAYER_FIELD(f_layer_get_y, f_layer_y, y)
LAYER_FIELD(f_layer_get_hspeed, f_layer_hspeed, hspeed)
LAYER_FIELD(f_layer_get_vspeed, f_layer_vspeed, vspeed)

FN(f_layer_get_visible) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    return ar_bool(l && l->visible);
}

FN(f_layer_set_visible) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    if (l) l->visible = argc > 1 && ar_truthy(a[1]);
    return ar_undefined();
}

FN(f_layer_get_all) {
    UNUSED;
    ARRun *run = RUN;
    ARArray *arr = ar_array_new(HEAP, run->layer_count);
    for (int i = 0; i < run->layer_count; i++) arr->items[i] = ar_real(run->layers[i].id);
    return ar_arrayv(arr);
}

FN(f_layer_get_all_elements) {
    UNUSED;
    ARRun *run = RUN;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    ARArray *arr = ar_array_new(HEAP, 0);
    if (!l) return ar_arrayv(arr);
    for (int i = 0; i < l->element_count; i++) ar_array_set(arr, arr->length, ar_real(l->elements[i]));
    // The instances of the layer are elements too.
    int count = ar_vm_instance_count(vm);
    for (int i = 0; i < count; i++) {
        ARInstance *inst = ar_vm_instance_at(vm, i);
        if (inst->layer == l->id && !inst->destroyed) ar_array_set(arr, arr->length, ar_real(inst->id));
    }
    (void)run;
    return ar_arrayv(arr);
}

FN(f_layer_get_element_type) {
    UNUSED;
    int id = INT(0, -1);
    if (id >= AR_FIRST_INSTANCE_ID) return ar_real(AR_ELEMENT_INSTANCE);
    ARElement *e = ar_run_element(RUN, id);
    return ar_real(e ? e->type : -1);
}

static ARElement *element_arg(ARVM *vm, int argc, ARValue *a, int i) {
    if (i >= argc) return NULL;
    ARElement *e = ar_run_element(RUN, ar_to_int(a[i]));
    return e && e->type ? e : NULL;
}

FN(f_layer_background_get_id) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    if (!l) return ar_real(-1);
    for (int i = 0; i < l->element_count; i++) {
        ARElement *e = ar_run_element(RUN, l->elements[i]);
        if (e && e->type == AR_ELEMENT_BACKGROUND) return ar_real(e->id);
    }
    return ar_real(-1);
}

FN(f_layer_background_exists) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 1);
    return ar_bool(e && e->type == AR_ELEMENT_BACKGROUND);
}

FN(f_layer_background_create) {
    UNUSED;
    ARLayerState *l = layer_arg(vm, argc, a, 0);
    if (!l) return ar_real(-1);
    int id = ar_run_element_new(RUN, l->id, AR_ELEMENT_BACKGROUND);
    ar_run_element(RUN, id)->sprite = INT(1, -1);
    return ar_real(id);
}

#define ELEMENT_FIELD(get, set, field, kind)                            \
    FN(get) {                                                           \
        UNUSED;                                                         \
        ARElement *e = element_arg(vm, argc, a, 0);                     \
        return kind(e ? e->field : 0);                                  \
    }                                                                   \
    FN(set) {                                                           \
        UNUSED;                                                         \
        ARElement *e = element_arg(vm, argc, a, 0);                     \
        if (e) e->field = REAL(1, 0);                                   \
        return ar_undefined();                                          \
    }

ELEMENT_FIELD(f_layer_background_get_alpha, f_layer_background_alpha, alpha, ar_real)
ELEMENT_FIELD(f_layer_background_get_xscale, f_layer_background_xscale, xscale, ar_real)
ELEMENT_FIELD(f_layer_background_get_yscale, f_layer_background_yscale, yscale, ar_real)
ELEMENT_FIELD(f_layer_background_get_index, f_layer_background_index, index, ar_real)
ELEMENT_FIELD(f_layer_background_get_speed, f_layer_background_speed, speed, ar_real)
ELEMENT_FIELD(f_layer_tile_get_x, f_layer_tile_x, x, ar_real)
ELEMENT_FIELD(f_layer_tile_get_y, f_layer_tile_y, y, ar_real)

#define ELEMENT_FLAG(get, set, field)                                   \
    FN(get) {                                                           \
        UNUSED;                                                         \
        ARElement *e = element_arg(vm, argc, a, 0);                     \
        return ar_bool(e && e->field);                                  \
    }                                                                   \
    FN(set) {                                                           \
        UNUSED;                                                         \
        ARElement *e = element_arg(vm, argc, a, 0);                     \
        if (e) e->field = argc > 1 && ar_truthy(a[1]);                  \
        return ar_undefined();                                          \
    }

ELEMENT_FLAG(f_layer_background_get_htiled, f_layer_background_htiled, htiled)
ELEMENT_FLAG(f_layer_background_get_vtiled, f_layer_background_vtiled, vtiled)
ELEMENT_FLAG(f_layer_background_get_stretch, f_layer_background_stretch, stretch)
ELEMENT_FLAG(f_layer_background_get_visible, f_layer_background_visible, visible)
ELEMENT_FLAG(f_layer_tile_get_visible, f_layer_tile_visible, visible)

FN(f_layer_background_get_sprite) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    return ar_real(e ? e->sprite : -1);
}

FN(f_layer_background_change) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    if (e) e->sprite = INT(1, -1);
    return ar_undefined();
}

FN(f_layer_background_get_blend) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    return ar_real(e ? e->blend : 0xFFFFFF);
}

FN(f_layer_background_blend) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    if (e) e->blend = (uint32_t)INT(1, 0xFFFFFF) & 0xFFFFFF;
    return ar_undefined();
}

FN(f_layer_tile_alpha) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    if (e) e->alpha = REAL(1, 1);
    return ar_undefined();
}

FN(f_layer_sprite_destroy) {
    UNUSED;
    ar_run_element_remove(RUN, INT(0, -1));
    return ar_undefined();
}

FN(f_layer_sprite_get_sprite) {
    UNUSED;
    ARElement *e = element_arg(vm, argc, a, 0);
    return ar_real(e ? e->sprite : -1);
}

// --------------------------------------------------------- cameras, views

static ARCamera *camera_arg(ARVM *vm, int argc, ARValue *a) {
    int c = INT(0, -1);
    if (c < 0 || c >= AR_MAX_CAMERAS || !RUN->cameras[c].used) return NULL;
    return &RUN->cameras[c];
}

FN(f_camera_create) {
    UNUSED;
    ARRun *run = RUN;
    for (int i = AR_MAX_VIEWS; i < AR_MAX_CAMERAS; i++) {
        if (!run->cameras[i].used) {
            memset(&run->cameras[i], 0, sizeof(ARCamera));
            run->cameras[i].used = 1;
            run->cameras[i].target = -1;
            run->cameras[i].w = run->window_w;
            run->cameras[i].h = run->window_h;
            return ar_real(i);
        }
    }
    return ar_real(-1);
}

#define CAMERA_GET(name, expr)                                          \
    FN(name) {                                                          \
        UNUSED;                                                         \
        ARCamera *c = camera_arg(vm, argc, a);                          \
        return ar_real(c ? (expr) : 0);                                 \
    }

CAMERA_GET(f_camera_get_view_x, c->x)
CAMERA_GET(f_camera_get_view_y, c->y)
CAMERA_GET(f_camera_get_view_width, c->w)
CAMERA_GET(f_camera_get_view_height, c->h)
CAMERA_GET(f_camera_get_view_angle, c->angle)
CAMERA_GET(f_camera_get_view_border_x, c->border_x)
CAMERA_GET(f_camera_get_view_border_y, c->border_y)
CAMERA_GET(f_camera_get_view_speed_x, c->speed_x)
CAMERA_GET(f_camera_get_view_speed_y, c->speed_y)
CAMERA_GET(f_camera_get_view_target, c->target)

FN(f_camera_set_view_pos) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) {
        c->x = REAL(1, 0);
        c->y = REAL(2, 0);
    }
    return ar_undefined();
}

FN(f_camera_set_view_size) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) {
        c->w = REAL(1, 0);
        c->h = REAL(2, 0);
    }
    return ar_undefined();
}

FN(f_camera_set_view_angle) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) c->angle = REAL(1, 0);
    return ar_undefined();
}

FN(f_camera_set_view_border) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) {
        c->border_x = REAL(1, 0);
        c->border_y = REAL(2, 0);
    }
    return ar_undefined();
}

FN(f_camera_set_view_speed) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) {
        c->speed_x = REAL(1, -1);
        c->speed_y = REAL(2, -1);
    }
    return ar_undefined();
}

FN(f_camera_set_view_target) {
    UNUSED;
    ARCamera *c = camera_arg(vm, argc, a);
    if (c) c->target = INT(1, -1);
    return ar_undefined();
}

static ARView *view_arg(ARVM *vm, int argc, ARValue *a) {
    int v = INT(0, 0);
    if (v < 0 || v >= AR_MAX_VIEWS) return NULL;
    return &RUN->views[v];
}

#define VIEW_FIELD(get, set, field)                                     \
    FN(get) {                                                           \
        UNUSED;                                                         \
        ARView *v = view_arg(vm, argc, a);                              \
        return ar_real(v ? v->field : 0);                               \
    }                                                                   \
    FN(set) {                                                           \
        UNUSED;                                                         \
        ARView *v = view_arg(vm, argc, a);                              \
        if (v) v->field = INT(1, 0);                                    \
        return ar_undefined();                                          \
    }

VIEW_FIELD(f_view_get_camera, f_view_set_camera, camera)
VIEW_FIELD(f_view_get_xport, f_view_set_xport, xport)
VIEW_FIELD(f_view_get_yport, f_view_set_yport, yport)
VIEW_FIELD(f_view_get_wport, f_view_set_wport, wport)
VIEW_FIELD(f_view_get_hport, f_view_set_hport, hport)
VIEW_FIELD(f_view_get_surface_id, f_view_set_surface_id, surface)

FN(f_view_get_visible) {
    UNUSED;
    ARView *v = view_arg(vm, argc, a);
    return ar_bool(v && v->visible);
}

FN(f_view_set_visible) {
    UNUSED;
    ARView *v = view_arg(vm, argc, a);
    if (v) v->visible = argc > 1 && ar_truthy(a[1]);
    return ar_undefined();
}

// ------------------------------------------------------------------- draw

FN(f_draw_set_color) {
    UNUSED;
    RUN->draw_color = (uint32_t)INT(0, 0) & 0xFFFFFF;
    return ar_undefined();
}

FN(f_draw_get_color) {
    UNUSED;
    return ar_real(RUN->draw_color);
}

FN(f_draw_set_alpha) {
    UNUSED;
    RUN->draw_alpha = REAL(0, 1);
    return ar_undefined();
}

FN(f_draw_get_alpha) {
    UNUSED;
    return ar_real(RUN->draw_alpha);
}

FN(f_draw_set_font) {
    UNUSED;
    RUN->draw_font = INT(0, -1);
    return ar_undefined();
}

FN(f_draw_get_font) {
    UNUSED;
    return ar_real(RUN->draw_font);
}

FN(f_draw_set_halign) {
    UNUSED;
    RUN->halign = INT(0, 0);
    return ar_undefined();
}

FN(f_draw_set_valign) {
    UNUSED;
    RUN->valign = INT(0, 0);
    return ar_undefined();
}

FN(f_draw_self) {
    UNUSED;
    ARInstance *s = self_of(vm);
    if (!s) return ar_undefined();
    ar_draw_sprite(RUN, s->sprite_index, s->image_index, s->x, s->y, s->image_xscale, s->image_yscale,
                   s->image_angle, (uint32_t)s->image_blend, s->image_alpha);
    return ar_undefined();
}

static double sub_image(ARVM *vm, int argc, ARValue *a, int i) {
    double sub = REAL(i, 0);
    if (sub < 0) {
        ARInstance *s = self_of(vm);
        sub = s ? s->image_index : 0;
    }
    return sub;
}

FN(f_draw_sprite) {
    UNUSED;
    ar_draw_sprite(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), 1, 1, 0, 0xFFFFFF,
                   RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_sprite_ext) {
    UNUSED;
    ar_draw_sprite(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 1), REAL(5, 1),
                   REAL(6, 0), (uint32_t)INT(7, 0xFFFFFF), REAL(8, 1));
    return ar_undefined();
}

FN(f_draw_sprite_part) {
    UNUSED;
    ar_draw_sprite_part(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 0), REAL(5, 0),
                        REAL(6, 0), REAL(7, 0), 1, 1, 0xFFFFFF, RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_sprite_part_ext) {
    UNUSED;
    ar_draw_sprite_part(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 0), REAL(5, 0),
                        REAL(6, 0), REAL(7, 0), REAL(8, 1), REAL(9, 1), (uint32_t)INT(10, 0xFFFFFF), REAL(11, 1));
    return ar_undefined();
}

FN(f_draw_sprite_stretched) {
    UNUSED;
    ar_draw_sprite_stretched(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 0),
                             REAL(5, 0), 0xFFFFFF, RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_sprite_stretched_ext) {
    UNUSED;
    ar_draw_sprite_stretched(RUN, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 0),
                             REAL(5, 0), (uint32_t)INT(6, 0xFFFFFF), REAL(7, 1));
    return ar_undefined();
}

FN(f_draw_sprite_tiled_ext) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_sprite_tiled(run, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), REAL(4, 1), REAL(5, 1), 1,
                         1, 0, 0, run->room_w, run->room_h, (uint32_t)INT(6, 0xFFFFFF), REAL(7, 1));
    return ar_undefined();
}

FN(f_draw_sprite_tiled) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_sprite_tiled(run, INT(0, -1), sub_image(vm, argc, a, 1), REAL(2, 0), REAL(3, 0), 1, 1, 1, 1, 0, 0,
                         run->room_w, run->room_h, 0xFFFFFF, run->draw_alpha);
    return ar_undefined();
}

static const char *text_arg(ARVM *vm, int argc, ARValue *a, int i) {
    return ARG_STR(i)->text;
}

FN(f_draw_text) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_text(run, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), -1, -1, 1, 1, 0, c, c, c, c, run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_text_ext) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_text(run, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), REAL(3, -1), REAL(4, -1), 1, 1, 0, c, c, c, c,
                 run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_text_transformed) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_text(run, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), -1, -1, REAL(3, 1), REAL(4, 1), REAL(5, 0), c,
                 c, c, c, run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_text_ext_transformed) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_text(run, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), REAL(3, -1), REAL(4, -1), REAL(5, 1),
                 REAL(6, 1), REAL(7, 0), c, c, c, c, run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_text_color) {
    UNUSED;
    ar_draw_text(RUN, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), -1, -1, 1, 1, 0, (uint32_t)INT(3, 0),
                 (uint32_t)INT(4, 0), (uint32_t)INT(5, 0), (uint32_t)INT(6, 0), REAL(7, 1));
    return ar_undefined();
}

FN(f_draw_text_ext_color) {
    UNUSED;
    ar_draw_text(RUN, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), REAL(3, -1), REAL(4, -1), 1, 1, 0,
                 (uint32_t)INT(5, 0), (uint32_t)INT(6, 0), (uint32_t)INT(7, 0), (uint32_t)INT(8, 0), REAL(9, 1));
    return ar_undefined();
}

FN(f_draw_text_transformed_color) {
    UNUSED;
    ar_draw_text(RUN, REAL(0, 0), REAL(1, 0), text_arg(vm, argc, a, 2), -1, -1, REAL(3, 1), REAL(4, 1), REAL(5, 0),
                 (uint32_t)INT(6, 0), (uint32_t)INT(7, 0), (uint32_t)INT(8, 0), (uint32_t)INT(9, 0), REAL(10, 1));
    return ar_undefined();
}

FN(f_string_width) {
    UNUSED;
    return ar_real(ar_draw_text_width(RUN, text_arg(vm, argc, a, 0), -1, -1));
}

FN(f_string_height) {
    UNUSED;
    return ar_real(ar_draw_text_height(RUN, text_arg(vm, argc, a, 0), -1, -1));
}

FN(f_string_width_ext) {
    UNUSED;
    return ar_real(ar_draw_text_width(RUN, text_arg(vm, argc, a, 0), REAL(1, -1), REAL(2, -1)));
}

FN(f_string_height_ext) {
    UNUSED;
    return ar_real(ar_draw_text_height(RUN, text_arg(vm, argc, a, 0), REAL(1, -1), REAL(2, -1)));
}

FN(f_font_add_sprite_ext) {
    UNUSED;
    ARRun *run = RUN;
    run->sprite_fonts = realloc(run->sprite_fonts, sizeof(ARSpriteFont) * (size_t)(run->sprite_font_count + 1));
    ARSpriteFont *f = &run->sprite_fonts[run->sprite_font_count];
    f->sprite = INT(0, -1);
    ARStr *map = ARG_STR(1);
    map->h.fixed = 1;  // The font keeps the text.
    f->map = map->text;
    f->prop = argc > 2 && ar_truthy(a[2]);
    f->sep = INT(3, 0);
    return ar_real(ar_vm_data(vm)->font_count + run->sprite_font_count++);
}

FN(f_font_add_sprite) {
    UNUSED;
    ARRun *run = RUN;
    run->sprite_fonts = realloc(run->sprite_fonts, sizeof(ARSpriteFont) * (size_t)(run->sprite_font_count + 1));
    ARSpriteFont *f = &run->sprite_fonts[run->sprite_font_count];
    f->sprite = INT(0, -1);
    // The characters start at the first argument and follow in order.
    int first = INT(1, 32);
    char *map = malloc(512);
    int n = 0;
    const ARData *d = ar_vm_data(vm);
    int frames = f->sprite >= 0 && f->sprite < d->sprite_count ? d->sprites[f->sprite].frame_count : 0;
    for (int i = 0; i < frames && n < 500; i++) {
        int c = first + i;
        if (c < 0x80) map[n++] = (char)c;
    }
    map[n] = 0;
    f->map = map;
    f->prop = argc > 2 && ar_truthy(a[2]);
    f->sep = INT(3, 0);
    return ar_real(d->font_count + run->sprite_font_count++);
}

static uint32_t color_arg(ARVM *vm, int argc, ARValue *a, int i) {
    return i < argc ? (uint32_t)ar_to_int(a[i]) & 0xFFFFFF : RUN->draw_color;
}

FN(f_draw_rectangle) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c, c, c, c, run->draw_alpha,
                 argc > 4 && ar_truthy(a[4]));
    return ar_undefined();
}

FN(f_draw_rectangle_color) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), color_arg(vm, argc, a, 4),
                 color_arg(vm, argc, a, 5), color_arg(vm, argc, a, 6), color_arg(vm, argc, a, 7), run->draw_alpha,
                 argc > 8 && ar_truthy(a[8]));
    return ar_undefined();
}

FN(f_draw_roundrect) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c, c, c, c, run->draw_alpha,
                 argc > 4 && ar_truthy(a[4]));
    return ar_undefined();
}

FN(f_draw_roundrect_color) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c1 = color_arg(vm, argc, a, 4), c2 = color_arg(vm, argc, a, 5);
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c1, c1, c2, c2, run->draw_alpha,
                 argc > 6 && ar_truthy(a[6]));
    return ar_undefined();
}

FN(f_draw_roundrect_ext) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c, c, c, c, run->draw_alpha,
                 argc > 6 && ar_truthy(a[6]));
    return ar_undefined();
}

FN(f_draw_roundrect_color_ext) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c1 = color_arg(vm, argc, a, 6), c2 = color_arg(vm, argc, a, 7);
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c1, c1, c2, c2, run->draw_alpha,
                 argc > 8 && ar_truthy(a[8]));
    return ar_undefined();
}

FN(f_draw_line) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_line(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), 1, run->draw_color, run->draw_color,
                 run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_line_width) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_line(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), REAL(4, 1), run->draw_color, run->draw_color,
                 run->draw_alpha);
    return ar_undefined();
}

FN(f_draw_line_color) {
    UNUSED;
    ar_draw_line(RUN, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), 1, color_arg(vm, argc, a, 4),
                 color_arg(vm, argc, a, 5), RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_line_width_color) {
    UNUSED;
    ar_draw_line(RUN, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), REAL(4, 1), color_arg(vm, argc, a, 5),
                 color_arg(vm, argc, a, 6), RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_arrow) {
    UNUSED;
    ARRun *run = RUN;
    double x1 = REAL(0, 0), y1 = REAL(1, 0), x2 = REAL(2, 0), y2 = REAL(3, 0), size = REAL(4, 8);
    uint32_t c = run->draw_color;
    ar_draw_line(run, x1, y1, x2, y2, 1, c, c, run->draw_alpha);
    double d = atan2(y2 - y1, x2 - x1);
    ar_draw_triangle(run, x2, y2, x2 - cos(d - 0.4) * size, y2 - sin(d - 0.4) * size, x2 - cos(d + 0.4) * size,
                     y2 - sin(d + 0.4) * size, c, c, c, run->draw_alpha, 0);
    return ar_undefined();
}

FN(f_draw_point) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_rect(run, REAL(0, 0), REAL(1, 0), REAL(0, 0), REAL(1, 0), c, c, c, c, run->draw_alpha, 0);
    return ar_undefined();
}

FN(f_draw_point_color) {
    UNUSED;
    uint32_t c = color_arg(vm, argc, a, 2);
    ar_draw_rect(RUN, REAL(0, 0), REAL(1, 0), REAL(0, 0), REAL(1, 0), c, c, c, c, RUN->draw_alpha, 0);
    return ar_undefined();
}

FN(f_draw_triangle) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_triangle(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), REAL(4, 0), REAL(5, 0), c, c, c,
                     run->draw_alpha, argc > 6 && ar_truthy(a[6]));
    return ar_undefined();
}

FN(f_draw_triangle_color) {
    UNUSED;
    ar_draw_triangle(RUN, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), REAL(4, 0), REAL(5, 0),
                     color_arg(vm, argc, a, 6), color_arg(vm, argc, a, 7), color_arg(vm, argc, a, 8), RUN->draw_alpha,
                     argc > 9 && ar_truthy(a[9]));
    return ar_undefined();
}

FN(f_draw_circle) {
    UNUSED;
    ARRun *run = RUN;
    double x = REAL(0, 0), y = REAL(1, 0), r = REAL(2, 0);
    uint32_t c = run->draw_color;
    ar_draw_ellipse(run, x - r, y - r, x + r, y + r, c, c, run->draw_alpha, argc > 3 && ar_truthy(a[3]));
    return ar_undefined();
}

FN(f_draw_circle_color) {
    UNUSED;
    double x = REAL(0, 0), y = REAL(1, 0), r = REAL(2, 0);
    ar_draw_ellipse(RUN, x - r, y - r, x + r, y + r, color_arg(vm, argc, a, 3), color_arg(vm, argc, a, 4),
                    RUN->draw_alpha, argc > 5 && ar_truthy(a[5]));
    return ar_undefined();
}

FN(f_draw_ellipse) {
    UNUSED;
    ARRun *run = RUN;
    uint32_t c = run->draw_color;
    ar_draw_ellipse(run, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), c, c, run->draw_alpha,
                    argc > 4 && ar_truthy(a[4]));
    return ar_undefined();
}

FN(f_draw_ellipse_color) {
    UNUSED;
    ar_draw_ellipse(RUN, REAL(0, 0), REAL(1, 0), REAL(2, 0), REAL(3, 0), color_arg(vm, argc, a, 4),
                    color_arg(vm, argc, a, 5), RUN->draw_alpha, argc > 6 && ar_truthy(a[6]));
    return ar_undefined();
}

FN(f_draw_clear) {
    UNUSED;
    ar_draw_clear(RUN, (uint32_t)INT(0, 0), 1);
    return ar_undefined();
}

FN(f_draw_clear_alpha) {
    UNUSED;
    ar_draw_clear(RUN, (uint32_t)INT(0, 0), REAL(1, 1));
    return ar_undefined();
}

// ----------------------------------------------------------------- colors

FN(f_make_color_rgb) {
    UNUSED;
    int r = INT(0, 0) & 255, g = INT(1, 0) & 255, b = INT(2, 0) & 255;
    return ar_real(r | g << 8 | b << 16);
}

FN(f_make_color_hsv) {
    UNUSED;
    double h = REAL(0, 0) / 255.0 * 360.0, s = REAL(1, 0) / 255.0, v = REAL(2, 0) / 255.0;
    double c = v * s, x = c * (1 - fabs(fmod(h / 60.0, 2) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    int ri = (int)lround((r + m) * 255), gi = (int)lround((g + m) * 255), bi = (int)lround((b + m) * 255);
    return ar_real(ri | gi << 8 | bi << 16);
}

FN(f_merge_color) {
    UNUSED;
    uint32_t x = (uint32_t)INT(0, 0), y = (uint32_t)INT(1, 0);
    double t = REAL(2, 0);
    int r = (int)((x & 255) + ((int)(y & 255) - (int)(x & 255)) * t);
    int g = (int)(((x >> 8) & 255) + ((int)((y >> 8) & 255) - (int)((x >> 8) & 255)) * t);
    int b = (int)(((x >> 16) & 255) + ((int)((y >> 16) & 255) - (int)((x >> 16) & 255)) * t);
    return ar_real((r & 255) | (g & 255) << 8 | (b & 255) << 16);
}

FN(f_color_get_red) { UNUSED; return ar_real(INT(0, 0) & 255); }
FN(f_color_get_green) { UNUSED; return ar_real((INT(0, 0) >> 8) & 255); }
FN(f_color_get_blue) { UNUSED; return ar_real((INT(0, 0) >> 16) & 255); }

// ----------------------------------------------------------------- sprites

static const ARSprite *sprite_arg(ARVM *vm, int argc, ARValue *a) {
    int s = INT(0, -1);
    if (s < 0 || s >= ar_vm_data(vm)->sprite_count) return NULL;
    return &ar_vm_data(vm)->sprites[s];
}

FN(f_sprite_exists) { UNUSED; return ar_bool(sprite_arg(vm, argc, a) != NULL); }
FN(f_sprite_get_width) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->width : 0);
}
FN(f_sprite_get_height) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->height : 0);
}
FN(f_sprite_get_number) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->frame_count : 0);
}
FN(f_sprite_get_xoffset) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->origin_x : 0);
}
FN(f_sprite_get_yoffset) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->origin_y : 0);
}
FN(f_sprite_get_name) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return STR(s ? s->name : "<undefined>");
}
FN(f_sprite_set_offset) {
    UNUSED;
    ARSprite *s = (ARSprite *)sprite_arg(vm, argc, a);
    if (s) {
        s->origin_x = INT(1, 0);
        s->origin_y = INT(2, 0);
    }
    return ar_undefined();
}
FN(f_sprite_get_speed) {
    UNUSED;
    const ARSprite *s = sprite_arg(vm, argc, a);
    return ar_real(s ? s->speed : 0);
}

// --------------------------------------------------------------- surfaces

static int surface_ok(ARRun *run, int s) {
    return s >= 0 && s < run->surface_count && run->surfaces[s].used;
}

FN(f_surface_create) {
    UNUSED;
    return ar_real(ar_draw_surface_new(RUN, INT(0, 1), INT(1, 1)));
}

FN(f_surface_free) {
    UNUSED;
    ar_draw_surface_free(RUN, INT(0, -1));
    return ar_undefined();
}

FN(f_surface_exists) {
    UNUSED;
    return ar_bool(surface_ok(RUN, INT(0, -1)));
}

FN(f_surface_set_target) {
    UNUSED;
    return ar_bool(ar_draw_push_target(RUN, INT(0, -1)));
}

FN(f_surface_reset_target) {
    UNUSED;
    return ar_bool(ar_draw_pop_target(RUN));
}

FN(f_surface_get_width) {
    UNUSED;
    int s = INT(0, -1);
    return ar_real(surface_ok(RUN, s) ? RUN->surfaces[s].width : -1);
}

FN(f_surface_get_height) {
    UNUSED;
    int s = INT(0, -1);
    return ar_real(surface_ok(RUN, s) ? RUN->surfaces[s].height : -1);
}

FN(f_surface_resize) {
    UNUSED;
    ARRun *run = RUN;
    int s = INT(0, -1);
    if (!surface_ok(run, s)) return ar_undefined();
    int w = INT(1, 1), h = INT(2, 1);
    ARSurface *sf = &run->surfaces[s];
    if (sf->width == w && sf->height == h) return ar_undefined();
    ar_draw_flush(run);
    if (run->gfx.texture_free && sf->handle) run->gfx.texture_free(run->gfx.context, sf->handle);
    sf->width = w;
    sf->height = h;
    sf->handle = run->gfx.target_new ? run->gfx.target_new(run->gfx.context, w, h) : 0;
    return ar_undefined();
}

FN(f_draw_surface) {
    UNUSED;
    ar_draw_surface(RUN, INT(0, -1), REAL(1, 0), REAL(2, 0), 1, 1, 0, 0xFFFFFF, RUN->draw_alpha);
    return ar_undefined();
}

FN(f_draw_surface_ext) {
    UNUSED;
    ar_draw_surface(RUN, INT(0, -1), REAL(1, 0), REAL(2, 0), REAL(3, 1), REAL(4, 1), REAL(5, 0),
                    (uint32_t)INT(6, 0xFFFFFF), REAL(7, 1));
    return ar_undefined();
}

FN(f_draw_surface_stretched) {
    UNUSED;
    ARRun *run = RUN;
    int s = INT(0, -1);
    if (!surface_ok(run, s)) return ar_undefined();
    ar_draw_surface(run, s, REAL(1, 0), REAL(2, 0), REAL(3, 0) / run->surfaces[s].width,
                    REAL(4, 0) / run->surfaces[s].height, 0, 0xFFFFFF, run->draw_alpha);
    return ar_undefined();
}

FN(f_application_surface_draw_enable) {
    UNUSED;
    RUN->app_surface_draw = argc > 0 && ar_truthy(a[0]);
    return ar_undefined();
}

// -------------------------------------------------------------------- gpu

FN(f_gpu_set_blendmode) {
    UNUSED;
    // bm_normal 0, bm_add 1, bm_max 2, bm_subtract 3.
    int mode = INT(0, 0);
    ARRun *run = RUN;
    switch (mode) {
    case 1: ar_draw_blend(run, run->blend_enabled, AR_BLEND_SRC_ALPHA, AR_BLEND_ONE); break;
    case 2: ar_draw_blend(run, run->blend_enabled, AR_BLEND_SRC_ALPHA, AR_BLEND_INV_SRC_COLOR); break;
    case 3: ar_draw_blend(run, run->blend_enabled, AR_BLEND_ZERO, AR_BLEND_INV_SRC_COLOR); break;
    default: ar_draw_blend(run, run->blend_enabled, AR_BLEND_SRC_ALPHA, AR_BLEND_INV_SRC_ALPHA); break;
    }
    run->blend_src = mode;
    return ar_undefined();
}

FN(f_gpu_set_blendmode_ext) {
    UNUSED;
    ar_draw_blend(RUN, RUN->blend_enabled, INT(0, AR_BLEND_SRC_ALPHA), INT(1, AR_BLEND_INV_SRC_ALPHA));
    return ar_undefined();
}

FN(f_gpu_set_blendenable) {
    UNUSED;
    ARRun *run = RUN;
    run->blend_enabled = argc > 0 && ar_truthy(a[0]);
    ar_draw_blend(run, run->blend_enabled, 0, 0);
    return ar_undefined();
}

FN(f_gpu_set_fog) {
    UNUSED;
    ARRun *run = RUN;
    ar_draw_flush(run);
    run->fog = argc > 0 && ar_truthy(a[0]);
    run->fog_color = (uint32_t)INT(1, 0) & 0xFFFFFF;
    return ar_undefined();
}

FN(f_gpu_set_texfilter) {
    UNUSED;
    RUN->tex_filter = argc > 0 && ar_truthy(a[0]);
    return ar_undefined();
}

// --------------------------------------------------------------- keyboard

FN(f_keyboard_check) {
    UNUSED;
    ARRun *run = RUN;
    int k = INT(0, 0);
    if (k == AR_VK_ANYKEY) {
        for (int i = 2; i < 256; i++) {
            if (run->keys[i]) return ar_bool(1);
        }
        return ar_bool(0);
    }
    if (k == AR_VK_NOKEY) {
        for (int i = 2; i < 256; i++) {
            if (run->keys[i]) return ar_bool(0);
        }
        return ar_bool(1);
    }
    return ar_bool(k > 0 && k < 256 && run->keys[k]);
}

FN(f_keyboard_check_pressed) {
    UNUSED;
    ARRun *run = RUN;
    int k = INT(0, 0);
    if (k == AR_VK_ANYKEY) {
        for (int i = 2; i < 256; i++) {
            if (run->pressed[i]) return ar_bool(1);
        }
        return ar_bool(0);
    }
    return ar_bool(k > 0 && k < 256 && run->pressed[k]);
}

FN(f_keyboard_check_released) {
    UNUSED;
    int k = INT(0, 0);
    return ar_bool(k > 0 && k < 256 && RUN->released[k]);
}

FN(f_keyboard_clear) {
    UNUSED;
    int k = INT(0, 0);
    if (k > 0 && k < 256) {
        RUN->keys[k] = 0;
        RUN->pressed[k] = 0;
        RUN->released[k] = 0;
    }
    return ar_undefined();
}

FN(f_io_clear) {
    UNUSED;
    memset(RUN->keys, 0, sizeof(RUN->keys));
    memset(RUN->pressed, 0, sizeof(RUN->pressed));
    memset(RUN->released, 0, sizeof(RUN->released));
    return ar_undefined();
}

static const ARBuiltin kGame[] = {
    {"instance_create_depth", f_instance_create_depth}, {"instance_create_layer", f_instance_create_layer},
    {"instance_destroy", f_instance_destroy}, {"instance_exists", f_instance_exists},
    {"instance_number", f_instance_number}, {"instance_find", f_instance_find},
    {"instance_activate_all", f_instance_activate_all}, {"instance_deactivate_all", f_instance_deactivate_all},
    {"instance_activate_object", f_instance_activate_object},
    {"instance_deactivate_object", f_instance_deactivate_object}, {"object_get_sprite", f_object_get_sprite},
    {"object_get_name", f_object_get_name}, {"object_exists", f_object_exists},
    {"object_is_ancestor", f_object_is_ancestor}, {"place_meeting", f_place_meeting},
    {"instance_place", f_instance_place}, {"place_free", f_place_free}, {"place_empty", f_place_empty},
    {"position_meeting", f_position_meeting}, {"instance_position", f_instance_position},
    {"collision_point", f_collision_point}, {"collision_rectangle", f_collision_rectangle},
    {"collision_line", f_collision_line}, {"collision_circle", f_collision_circle},
    {"distance_to_object", f_distance_to_object}, {"distance_to_point", f_distance_to_point},
    {"instance_nearest", f_instance_nearest}, {"move_towards_point", f_move_towards_point},
    {"motion_set", f_motion_set}, {"motion_add", f_motion_add}, {"path_start", f_nothing}, {"path_end", f_nothing},
    {"draw_path", f_nothing}, {"room_goto", f_room_goto}, {"room_goto_next", f_room_goto_next},
    {"room_goto_previous", f_room_goto_previous}, {"room_next", f_room_next}, {"room_previous", f_room_previous},
    {"room_restart", f_room_restart}, {"room_exists", f_room_exists}, {"room_get_name", f_room_get_name},
    {"layer_get_id", f_layer_get_id}, {"layer_exists", f_layer_exists}, {"layer_create", f_layer_create},
    {"layer_destroy", f_layer_destroy}, {"layer_depth", f_layer_depth}, {"layer_get_depth", f_layer_get_depth},
    {"layer_get_name", f_layer_get_name}, {"layer_get_x", f_layer_get_x}, {"layer_x", f_layer_x},
    {"layer_get_y", f_layer_get_y}, {"layer_y", f_layer_y}, {"layer_get_hspeed", f_layer_get_hspeed},
    {"layer_hspeed", f_layer_hspeed}, {"layer_get_vspeed", f_layer_get_vspeed}, {"layer_vspeed", f_layer_vspeed},
    {"layer_get_visible", f_layer_get_visible}, {"layer_set_visible", f_layer_set_visible},
    {"layer_get_all", f_layer_get_all}, {"layer_get_all_elements", f_layer_get_all_elements},
    {"layer_get_element_type", f_layer_get_element_type}, {"layer_force_draw_depth", f_nothing},
    {"layer_background_get_id", f_layer_background_get_id}, {"layer_background_exists", f_layer_background_exists},
    {"layer_background_create", f_layer_background_create},
    {"layer_background_get_alpha", f_layer_background_get_alpha}, {"layer_background_alpha", f_layer_background_alpha},
    {"layer_background_get_xscale", f_layer_background_get_xscale},
    {"layer_background_xscale", f_layer_background_xscale},
    {"layer_background_get_yscale", f_layer_background_get_yscale},
    {"layer_background_yscale", f_layer_background_yscale},
    {"layer_background_get_index", f_layer_background_get_index}, {"layer_background_index", f_layer_background_index},
    {"layer_background_get_speed", f_layer_background_get_speed}, {"layer_background_speed", f_layer_background_speed},
    {"layer_background_get_htiled", f_layer_background_get_htiled},
    {"layer_background_htiled", f_layer_background_htiled},
    {"layer_background_get_vtiled", f_layer_background_get_vtiled},
    {"layer_background_vtiled", f_layer_background_vtiled},
    {"layer_background_get_stretch", f_layer_background_get_stretch},
    {"layer_background_stretch", f_layer_background_stretch},
    {"layer_background_get_visible", f_layer_background_get_visible},
    {"layer_background_visible", f_layer_background_visible},
    {"layer_background_get_sprite", f_layer_background_get_sprite},
    {"layer_background_change", f_layer_background_change},
    {"layer_background_sprite", f_layer_background_change},
    {"layer_background_get_blend", f_layer_background_get_blend},
    {"layer_background_blend", f_layer_background_blend}, {"layer_tile_get_x", f_layer_tile_get_x},
    {"layer_tile_x", f_layer_tile_x}, {"layer_tile_get_y", f_layer_tile_get_y}, {"layer_tile_y", f_layer_tile_y},
    {"layer_tile_get_visible", f_layer_tile_get_visible}, {"layer_tile_visible", f_layer_tile_visible},
    {"layer_tile_alpha", f_layer_tile_alpha}, {"layer_sprite_destroy", f_layer_sprite_destroy},
    {"layer_sprite_get_sprite", f_layer_sprite_get_sprite}, {"camera_create", f_camera_create},
    {"camera_get_view_x", f_camera_get_view_x}, {"camera_get_view_y", f_camera_get_view_y},
    {"camera_get_view_width", f_camera_get_view_width}, {"camera_get_view_height", f_camera_get_view_height},
    {"camera_get_view_angle", f_camera_get_view_angle}, {"camera_get_view_border_x", f_camera_get_view_border_x},
    {"camera_get_view_border_y", f_camera_get_view_border_y}, {"camera_get_view_speed_x", f_camera_get_view_speed_x},
    {"camera_get_view_speed_y", f_camera_get_view_speed_y}, {"camera_get_view_target", f_camera_get_view_target},
    {"camera_set_view_pos", f_camera_set_view_pos}, {"camera_set_view_size", f_camera_set_view_size},
    {"camera_set_view_angle", f_camera_set_view_angle}, {"camera_set_view_border", f_camera_set_view_border},
    {"camera_set_view_speed", f_camera_set_view_speed}, {"camera_set_view_target", f_camera_set_view_target},
    {"view_get_camera", f_view_get_camera}, {"view_set_camera", f_view_set_camera},
    {"view_get_xport", f_view_get_xport}, {"view_set_xport", f_view_set_xport}, {"view_get_yport", f_view_get_yport},
    {"view_set_yport", f_view_set_yport}, {"view_get_wport", f_view_get_wport}, {"view_set_wport", f_view_set_wport},
    {"view_get_hport", f_view_get_hport}, {"view_set_hport", f_view_set_hport},
    {"view_get_surface_id", f_view_get_surface_id}, {"view_set_surface_id", f_view_set_surface_id},
    {"view_get_visible", f_view_get_visible}, {"view_set_visible", f_view_set_visible},
    {"draw_set_color", f_draw_set_color}, {"draw_set_colour", f_draw_set_color}, {"draw_get_color", f_draw_get_color},
    {"draw_get_colour", f_draw_get_color}, {"draw_set_alpha", f_draw_set_alpha}, {"draw_get_alpha", f_draw_get_alpha},
    {"draw_set_font", f_draw_set_font}, {"draw_get_font", f_draw_get_font}, {"draw_set_halign", f_draw_set_halign},
    {"draw_set_valign", f_draw_set_valign}, {"draw_self", f_draw_self}, {"draw_sprite", f_draw_sprite},
    {"draw_sprite_ext", f_draw_sprite_ext}, {"draw_sprite_part", f_draw_sprite_part},
    {"draw_sprite_part_ext", f_draw_sprite_part_ext}, {"draw_sprite_stretched", f_draw_sprite_stretched},
    {"draw_sprite_stretched_ext", f_draw_sprite_stretched_ext}, {"draw_sprite_tiled", f_draw_sprite_tiled},
    {"draw_sprite_tiled_ext", f_draw_sprite_tiled_ext}, {"draw_text", f_draw_text}, {"draw_text_ext", f_draw_text_ext},
    {"draw_text_transformed", f_draw_text_transformed}, {"draw_text_ext_transformed", f_draw_text_ext_transformed},
    {"draw_text_color", f_draw_text_color}, {"draw_text_colour", f_draw_text_color},
    {"draw_text_ext_color", f_draw_text_ext_color}, {"draw_text_ext_colour", f_draw_text_ext_color},
    {"draw_text_transformed_color", f_draw_text_transformed_color},
    {"draw_text_transformed_colour", f_draw_text_transformed_color}, {"string_width", f_string_width},
    {"string_height", f_string_height}, {"string_width_ext", f_string_width_ext},
    {"string_height_ext", f_string_height_ext}, {"font_add_sprite_ext", f_font_add_sprite_ext},
    {"font_add_sprite", f_font_add_sprite}, {"draw_rectangle", f_draw_rectangle},
    {"draw_rectangle_color", f_draw_rectangle_color}, {"draw_rectangle_colour", f_draw_rectangle_color},
    {"draw_roundrect", f_draw_roundrect}, {"draw_roundrect_color", f_draw_roundrect_color},
    {"draw_roundrect_colour", f_draw_roundrect_color}, {"draw_roundrect_ext", f_draw_roundrect_ext},
    {"draw_roundrect_color_ext", f_draw_roundrect_color_ext}, {"draw_roundrect_colour_ext", f_draw_roundrect_color_ext},
    {"draw_line", f_draw_line}, {"draw_line_width", f_draw_line_width}, {"draw_line_color", f_draw_line_color},
    {"draw_line_colour", f_draw_line_color}, {"draw_line_width_color", f_draw_line_width_color},
    {"draw_line_width_colour", f_draw_line_width_color}, {"draw_arrow", f_draw_arrow}, {"draw_point", f_draw_point},
    {"draw_point_color", f_draw_point_color}, {"draw_point_colour", f_draw_point_color},
    {"draw_triangle", f_draw_triangle}, {"draw_triangle_color", f_draw_triangle_color},
    {"draw_triangle_colour", f_draw_triangle_color}, {"draw_circle", f_draw_circle},
    {"draw_circle_color", f_draw_circle_color}, {"draw_circle_colour", f_draw_circle_color},
    {"draw_ellipse", f_draw_ellipse}, {"draw_ellipse_color", f_draw_ellipse_color},
    {"draw_ellipse_colour", f_draw_ellipse_color}, {"draw_clear", f_draw_clear}, {"draw_clear_alpha", f_draw_clear_alpha},
    {"make_color_rgb", f_make_color_rgb}, {"make_colour_rgb", f_make_color_rgb}, {"make_color_hsv", f_make_color_hsv},
    {"make_colour_hsv", f_make_color_hsv}, {"merge_color", f_merge_color}, {"merge_colour", f_merge_color},
    {"color_get_red", f_color_get_red}, {"color_get_green", f_color_get_green}, {"color_get_blue", f_color_get_blue},
    {"colour_get_red", f_color_get_red}, {"colour_get_green", f_color_get_green},
    {"colour_get_blue", f_color_get_blue}, {"sprite_exists", f_sprite_exists}, {"sprite_get_width", f_sprite_get_width},
    {"sprite_get_height", f_sprite_get_height}, {"sprite_get_number", f_sprite_get_number},
    {"sprite_get_xoffset", f_sprite_get_xoffset}, {"sprite_get_yoffset", f_sprite_get_yoffset},
    {"sprite_get_name", f_sprite_get_name}, {"sprite_set_offset", f_sprite_set_offset},
    {"sprite_get_speed", f_sprite_get_speed}, {"sprite_create_from_surface", f_minus_one},
    {"sprite_delete", f_nothing}, {"surface_create", f_surface_create}, {"surface_free", f_surface_free},
    {"surface_exists", f_surface_exists}, {"surface_set_target", f_surface_set_target},
    {"surface_reset_target", f_surface_reset_target}, {"surface_get_width", f_surface_get_width},
    {"surface_get_height", f_surface_get_height}, {"surface_resize", f_surface_resize},
    {"draw_surface", f_draw_surface}, {"draw_surface_ext", f_draw_surface_ext},
    {"draw_surface_stretched", f_draw_surface_stretched}, {"application_surface_enable", f_nothing},
    {"application_surface_draw_enable", f_application_surface_draw_enable}, {"gpu_set_blendmode", f_gpu_set_blendmode},
    {"gpu_set_blendmode_ext", f_gpu_set_blendmode_ext}, {"gpu_set_blendenable", f_gpu_set_blendenable},
    {"gpu_set_fog", f_gpu_set_fog}, {"gpu_set_texfilter", f_gpu_set_texfilter}, {"texture_prefetch", f_nothing},
    {"texture_flush", f_nothing}, {"texture_is_ready", f_true}, {"texturegroup_get_textures", f_nothing},
    {"vertex_format_begin", f_nothing}, {"vertex_format_end", f_zero}, {"vertex_format_add_position_3d", f_nothing},
    {"vertex_format_add_colour", f_nothing}, {"vertex_format_add_normal", f_nothing},
    {"vertex_format_add_textcoord", f_nothing}, {"vertex_create_buffer", f_zero}, {"keyboard_check", f_keyboard_check},
    {"keyboard_check_direct", f_keyboard_check}, {"keyboard_check_pressed", f_keyboard_check_pressed},
    {"keyboard_check_released", f_keyboard_check_released}, {"keyboard_clear", f_keyboard_clear},
    {"io_clear", f_io_clear}, {"@@false@@", f_false},
};

void ar_lib_register_game(void) {
    ar_vm_register(kGame, (int)(sizeof(kGame) / sizeof(kGame[0])), NULL, 0);
}
