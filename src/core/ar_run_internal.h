// The state of the runtime, for the files of the runtime only.

#ifndef AR_RUN_INTERNAL_H
#define AR_RUN_INTERNAL_H

#include <stdio.h>
#include "ar_run.h"
#include "ar_vm.h"

#define AR_MAX_VIEWS 8
#define AR_MAX_CAMERAS 32

// The events that a frame runs, as type and subtype.
enum {
    AR_STEP_NORMAL = 0,
    AR_STEP_BEGIN = 1,
    AR_STEP_END = 2,
    AR_OTHER_OUTSIDE = 0,
    AR_OTHER_GAME_START = 2,
    AR_OTHER_GAME_END = 3,
    AR_OTHER_ROOM_START = 4,
    AR_OTHER_ROOM_END = 5,
    AR_OTHER_ANIMATION_END = 7,
    AR_OTHER_USER0 = 10,
    AR_OTHER_ASYNC_SAVE_LOAD = 72,
    AR_DRAW_NORMAL = 0,
    AR_DRAW_GUI = 64,
    AR_DRAW_BEGIN = 72,
    AR_DRAW_END = 73,
    AR_DRAW_GUI_BEGIN = 74,
    AR_DRAW_GUI_END = 75,
    AR_DRAW_PRE = 76,
    AR_DRAW_POST = 77
};

// An element of a layer.
enum { AR_ELEMENT_BACKGROUND = 1, AR_ELEMENT_INSTANCE = 2, AR_ELEMENT_SPRITE = 4, AR_ELEMENT_TILEMAP = 5,
       AR_ELEMENT_TILE = 7 };

typedef struct {
    int id;
    int layer;  // The id of the layer.
    int type;
    int instance;  // AR_ELEMENT_INSTANCE: the instance id.
    // AR_ELEMENT_BACKGROUND, AR_ELEMENT_TILE, AR_ELEMENT_SPRITE.
    int sprite;
    int visible;
    int htiled, vtiled, stretch;
    uint32_t blend;
    double alpha;
    double xscale, yscale;
    double index, speed;
    int speed_type;
    // AR_ELEMENT_TILE: the part of the sprite, and the position.
    double x, y;
    int source_x, source_y, width, height;
    // AR_ELEMENT_TILEMAP.
    int tileset, grid_w, grid_h;
    const uint32_t *grid;
} ARElement;

typedef struct {
    int id;
    char name[64];
    double depth;
    double x, y, hspeed, vspeed;
    int visible;
    int managed;  // A layer that the runtime made for a depth.
    int *elements;  // The ids of the elements.
    int element_count, element_capacity;
} ARLayerState;

typedef struct {
    int used;
    double x, y, w, h, angle;
    int target;  // An object or an instance, or -1.
    double border_x, border_y, speed_x, speed_y;
} ARCamera;

typedef struct {
    int visible;
    int camera;
    int xport, yport, wport, hport;
    int surface;
} ARView;

typedef struct {
    int used;
    int handle;  // The handle of the gfx target.
    int width, height;
} ARSurface;

// The structures of the ds functions.
enum { AR_DS_LIST = 1, AR_DS_MAP, AR_DS_GRID, AR_DS_STACK, AR_DS_QUEUE, AR_DS_PRIORITY };

typedef struct {
    ARValue key, value;
} ARMapEntry;

typedef struct {
    int type;
    // A list, a stack, a queue, or a grid: the items.
    ARValue *items;
    int count, capacity;
    int width, height;  // A grid.
    int head;           // A queue: the first item.
    // A map: the entries in the order of insertion, and an open hash of
    // their indices. A removed entry has the key undefined.
    ARMapEntry *entries;
    int entry_count, entry_capacity, live;
    int *index;
    int index_capacity;
} ARDs;

typedef struct {
    FILE *file;
    int write;
    int eof;
} ARTextFile;

typedef struct ARIniKey {
    char *section, *key, *value;
} ARIniKey;

typedef struct {
    int open;
    char path[1024];  // "" for an ini from a string.
    ARIniKey *keys;
    int count, capacity;
    int changed;
} ARIni;

typedef struct {
    int used;
    uint8_t *bytes;
    size_t size, capacity;
    size_t pos;
    int type;  // 0 fixed, 1 grow, 2 wrap, 3 fast
} ARBuffer;

// A sprite that the game makes at run time, or a sprite font.
typedef struct {
    int sprite;
    const char *map;  // A sprite font: the characters of the frames.
    int prop, sep;
} ARSpriteFont;

typedef struct {
    int object;  // The object whose code runs.
    int type, subtype;
    ARInstance *self;
} AREventContext;

// The audio voices.
typedef struct ARMixer ARMixer;

struct ARRun {
    const ARData *data;
    ARVM *vm;
    ARGfx gfx;
    ARAudioHost audio_host;
    char game_dir[1024];
    char save_dir[1024];
    // The cache of the decoded texture pages; empty for no cache.
    char cache_dir[1024];
    char music_dir[1024];
    const char *const *parameters;
    int parameter_count;

    long frame;
    double start_time;  // In milliseconds of the host clock.
    int game_end;
    int restart;
    char change_dir[512];
    char change_params[512];
    int change;

    int room;           // The index of the room.
    int next_room;      // -1 when no change waits.
    double room_speed;
    int room_w, room_h;
    int room_persistent;
    // For each room: 1 when the room keeps its instances, and the instances
    // that it keeps while another room runs.
    uint8_t *room_kept;
    struct {
        ARInstance **items;
        int count;
        int saved;
    } *room_saves;
    uint32_t background_color;
    int show_background_color;

    ARLayerState *layers;
    int layer_count, layer_capacity;
    int next_layer_id;
    ARElement *elements;
    int element_count, element_capacity;
    int next_element_id;

    ARCamera cameras[AR_MAX_CAMERAS];
    ARView views[AR_MAX_VIEWS];
    int views_enabled;
    int view_current;

    // The images of the texture pages, on the host.
    int *page_handles;
    int window_handle;
    int window_w, window_h;
    ARSurface *surfaces;
    int surface_count;
    int app_surface_draw;  // application_surface_draw_enable

    // The draw state.
    uint32_t draw_color;
    double draw_alpha;
    int draw_font;
    int halign, valign;
    int blend_enabled;
    int blend_src, blend_dst;
    int tex_filter;
    int fog;
    uint32_t fog_color;
    int target_depth;
    // The transform from the coordinates of the game to the pixels of the
    // target: x' = a x + c y + e, y' = b x + d y + f.
    double ma, mb, mc, md, me, mf;
    int target_w, target_h;
    int target_handle_now;
    // The batch of triangles.
    ARVertex *batch;
    int batch_count, batch_capacity;
    int batch_texture;
    int batch_filter;
    int draw_calls;

    ARSpriteFont *sprite_fonts;
    int sprite_font_count;

    // The input.
    uint8_t keys[256];
    uint8_t pressed[256];
    uint8_t released[256];
    uint8_t queued_down[256];
    uint8_t queued_up[256];
    int last_key;

    ARDs **ds;
    int ds_count, ds_capacity;
    ARTextFile files[32];
    ARIni ini;
    ARBuffer buffers[64];

    AREventContext event_stack[64];
    int event_depth;

    uint64_t random_state;
    ARMixer *mixer;

    // The slot numbers of some variables.
    int32_t slot_async_load;
};

// ar_run.c
ARRun *ar_run_of(ARVM *vm);
void ar_run_event(ARRun *run, ARInstance *inst, int type, int subtype);
// Runs the event with the parent of the object of the current event.
void ar_run_event_inherited(ARRun *run);
int ar_run_has_event(ARRun *run, int object, int type, int subtype);
ARInstance *ar_run_instance_create(ARRun *run, double x, double y, int object, double depth, int layer);
void ar_run_instance_destroy(ARRun *run, ARInstance *inst, int run_event);
void ar_run_goto_room(ARRun *run, int room);
int ar_run_layer_find(ARRun *run, int id);
int ar_run_layer_for_depth(ARRun *run, double depth);
ARLayerState *ar_run_layer(ARRun *run, int id);
ARElement *ar_run_element(ARRun *run, int id);
int ar_run_element_new(ARRun *run, int layer, int type);
void ar_run_element_remove(ARRun *run, int id);
double ar_run_time_ms(void);
double ar_run_random(ARRun *run);
// The box of the mask of an instance, in the coordinates of the room.
int ar_run_bbox(ARRun *run, ARInstance *inst, double *left, double *top, double *right, double *bottom);
// 1 when the instance at (x, y) touches the other instance.
int ar_run_collide(ARRun *run, ARInstance *a, double x, double y, ARInstance *b);
int ar_run_collide_rect(ARRun *run, ARInstance *inst, double x1, double y1, double x2, double y2, int precise);
int ar_run_collide_point(ARRun *run, ARInstance *inst, double px, double py, int precise);
// The instance of a value of GML: an id, an object, self, or other.
ARInstance *ar_run_instance(ARRun *run, ARValue v);
int ar_run_image_number(ARRun *run, ARInstance *inst);
// A path of the game in the save directory or in the game directory.
void ar_run_path(ARRun *run, const char *name, int write, char *out, size_t size);
// Finds the file in dir whose name is name in any case, as on Windows where
// GameMaker made the game. Writes its path with the real name to out, and
// returns 1; returns 0 when there is no such file.
int ar_run_find_any_case(const char *dir, const char *name, char *out, size_t size);
void ar_run_update_camera(ARRun *run, int camera);

// ar_draw.c
void ar_draw_init(ARRun *run);
void ar_draw_free(ARRun *run);
void ar_draw_flush(ARRun *run);
void ar_draw_set_target(ARRun *run, int handle, int width, int height);
void ar_draw_set_view(ARRun *run, double vx, double vy, double vw, double vh, double angle, double px, double py,
                      double pw, double ph);
void ar_draw_identity(ARRun *run);
int ar_draw_page(ARRun *run, int page);
void ar_draw_item(ARRun *run, int item, double x, double y, double xscale, double yscale, double angle,
                  double ox, double oy, uint32_t color, double alpha);
// Draws a part of a texture item, in the pixels of the image before the
// trim of the item.
void ar_draw_item_part(ARRun *run, int item, double left, double top, double width, double height, double x,
                       double y, double xscale, double yscale, uint32_t color, double alpha);
void ar_draw_sprite(ARRun *run, int sprite, double frame, double x, double y, double xscale, double yscale,
                    double angle, uint32_t color, double alpha);
void ar_draw_sprite_part(ARRun *run, int sprite, double frame, double left, double top, double width,
                         double height, double x, double y, double xscale, double yscale, uint32_t color,
                         double alpha);
void ar_draw_sprite_stretched(ARRun *run, int sprite, double frame, double x, double y, double w, double h,
                              uint32_t color, double alpha);
void ar_draw_sprite_tiled(ARRun *run, int sprite, double frame, double x, double y, double xscale,
                          double yscale, int htiled, int vtiled, double area_x, double area_y, double area_w,
                          double area_h, uint32_t color, double alpha);
void ar_draw_rect(ARRun *run, double x1, double y1, double x2, double y2, uint32_t c1, uint32_t c2, uint32_t c3,
                  uint32_t c4, double alpha, int outline);
void ar_draw_line(ARRun *run, double x1, double y1, double x2, double y2, double width, uint32_t c1, uint32_t c2,
                  double alpha);
void ar_draw_triangle(ARRun *run, double x1, double y1, double x2, double y2, double x3, double y3, uint32_t c1,
                      uint32_t c2, uint32_t c3, double alpha, int outline);
void ar_draw_ellipse(ARRun *run, double x1, double y1, double x2, double y2, uint32_t c1, uint32_t c2,
                     double alpha, int outline);
void ar_draw_text(ARRun *run, double x, double y, const char *text, double sep, double width, double xscale,
                  double yscale, double angle, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double alpha);
double ar_draw_text_width(ARRun *run, const char *text, double sep, double width);
double ar_draw_text_height(ARRun *run, const char *text, double sep, double width);
void ar_draw_surface(ARRun *run, int surface, double x, double y, double xscale, double yscale, double angle,
                     uint32_t color, double alpha);
void ar_draw_blend(ARRun *run, int enabled, int src, int dst);
int ar_draw_surface_new(ARRun *run, int width, int height);
void ar_draw_surface_free(ARRun *run, int surface);
void ar_draw_clear(ARRun *run, uint32_t rgb, double alpha);
// surface_set_target and surface_reset_target. They return 0 on a fault.
int ar_draw_push_target(ARRun *run, int surface);
int ar_draw_pop_target(ARRun *run);

// ar_audio.c
void ar_audio_init(ARRun *run);
void ar_audio_free(ARRun *run);
void ar_audio_mark(ARRun *run);
// Moves the sounds by the time of a frame when the host has no output.
void ar_audio_advance(ARRun *run, double seconds);

// The builtin tables of each part of the library.
void ar_lib_register_all(void);

// Arguments.
static inline double ar_arg_real(int argc, ARValue *args, int i, double fallback) {
    return i < argc ? ar_to_real(args[i]) : fallback;
}

static inline int ar_arg_int(int argc, ARValue *args, int i, int fallback) {
    return i < argc ? ar_to_int(args[i]) : fallback;
}

// The color of GML (0xBBGGRR) as a vertex color with an alpha.
static inline uint32_t ar_rgba(uint32_t bgr, double alpha) {
    int a = alpha <= 0 ? 0 : alpha >= 1 ? 255 : (int)(alpha * 255 + 0.5);
    return (bgr & 0xFFFFFF) | (uint32_t)a << 24;
}

#endif
