// ar_data: reads a GameMaker data file (game.ios or data.win).
//
// The file is a FORM of chunks. Each chunk has a name of four letters and
// a size. ar_data reads the chunks that the runtime needs into tables.
// The tables point into the bytes of the file, so the bytes must stay in
// memory while the tables are in use.

#ifndef AR_DATA_H
#define AR_DATA_H

#include <stddef.h>
#include <stdint.h>

// The event types of an object, in the order of the OBJT chunk.
enum {
    AR_EV_CREATE = 0,
    AR_EV_DESTROY = 1,
    AR_EV_ALARM = 2,
    AR_EV_STEP = 3,
    AR_EV_COLLISION = 4,
    AR_EV_KEYBOARD = 5,
    AR_EV_MOUSE = 6,
    AR_EV_OTHER = 7,
    AR_EV_DRAW = 8,
    AR_EV_KEYPRESS = 9,
    AR_EV_KEYRELEASE = 10,
    AR_EV_TRIGGER = 11,
    AR_EV_CLEANUP = 12,
    AR_EV_GESTURE = 13,
    AR_EV_PRECREATE = 14,
    AR_EV_COUNT = 15
};

// A string of the STRG chunk. The bytes end with a zero.
typedef struct {
    const char *text;
    uint32_t length;
} ARString;

typedef struct {
    uint16_t source_x, source_y, source_w, source_h;
    uint16_t target_x, target_y, target_w, target_h;
    uint16_t bound_w, bound_h;
    uint16_t page;
} ARTexItem;

typedef struct {
    uint32_t width, height;
    // The compressed image: a "2zoq" QOI image in a bzip2 stream, or a PNG.
    const uint8_t *bytes;
    uint32_t size;
} ARTexPage;

typedef struct {
    const char *name;
    int32_t width, height;
    int32_t margin_left, margin_right, margin_bottom, margin_top;
    int32_t bbox_mode;
    int32_t sep_masks;
    int32_t origin_x, origin_y;
    float speed;
    int32_t speed_type;  // 0: frames each second, 1: frames each game frame
    int32_t frame_count;
    const int32_t *frames;  // An index into the texture items for each frame.
    int32_t mask_count;
    // One mask for each entry: a bit for each pixel, rows of whole bytes.
    const uint8_t *masks;
} ARSprite;

typedef struct {
    int32_t subtype;
    int32_t code;  // An index into the code entries, or -1.
} AREvent;

typedef struct {
    const char *name;
    int32_t sprite;
    int32_t visible;
    int32_t solid;
    int32_t depth;
    int32_t persistent;
    int32_t parent;  // -1 when the object has no parent.
    int32_t mask;
    int32_t event_count[AR_EV_COUNT];
    AREvent *events[AR_EV_COUNT];
} ARObject;

typedef struct {
    int32_t x, y;
    int32_t object;
    int32_t id;
    int32_t creation_code;
    float scale_x, scale_y;
    float image_speed;
    int32_t image_index;
    uint32_t color;
    float rotation;
    int32_t pre_create_code;
} ARRoomInstance;

typedef struct {
    int32_t enabled;
    int32_t view_x, view_y, view_w, view_h;
    int32_t port_x, port_y, port_w, port_h;
    int32_t border_x, border_y;
    int32_t speed_x, speed_y;
    int32_t object;
} ARRoomView;

typedef struct {
    int32_t x, y;
    int32_t sprite;  // A sprite in GameMaker 2. A background in GameMaker 1.
    int32_t source_x, source_y, width, height;
    int32_t depth;
    int32_t id;
    float scale_x, scale_y;
    uint32_t color;
} ARRoomTile;

enum {
    AR_LAYER_BACKGROUND = 1,
    AR_LAYER_INSTANCES = 2,
    AR_LAYER_ASSETS = 3,
    AR_LAYER_TILES = 4,
    AR_LAYER_EFFECT = 6
};

typedef struct {
    const char *name;
    int32_t id;
    int32_t type;
    int32_t depth;
    float x, y, hspeed, vspeed;
    int32_t visible;
    // AR_LAYER_INSTANCES: the room instance ids.
    int32_t instance_count;
    const uint32_t *instances;
    // AR_LAYER_BACKGROUND.
    int32_t bg_visible, bg_foreground, bg_sprite, bg_htiled, bg_vtiled, bg_stretch;
    uint32_t bg_color;
    float bg_alpha, bg_speed;
    int32_t bg_speed_type;
    // AR_LAYER_ASSETS: the legacy tiles.
    int32_t tile_count;
    ARRoomTile *tiles;
    // AR_LAYER_TILES: a grid of tile ids of one tile set.
    int32_t tileset;
    int32_t grid_w, grid_h;
    const uint32_t *grid;
} ARLayer;

typedef struct {
    const char *name;
    int32_t width, height;
    int32_t speed;
    int32_t persistent;
    uint32_t color;
    int32_t draw_color;
    int32_t creation_code;
    uint32_t flags;
    ARRoomView views[8];
    int32_t view_count;
    int32_t instance_count;
    ARRoomInstance *instances;
    int32_t layer_count;
    ARLayer *layers;
} ARRoom;

typedef struct {
    const char *name;
    const uint8_t *blob;  // The bytecode that this entry shares with others.
    uint32_t length;      // The length of the blob.
    uint32_t offset;      // The start of this entry in the blob.
    uint16_t locals;
    uint16_t arguments;
} ARCode;

typedef struct {
    const char *name;
    int32_t instance_type;
    int32_t id;
    uint32_t occurrences;
    uint32_t first;  // The file offset of the first instruction, or -1.
} ARVariable;

typedef struct {
    const char *name;
    uint32_t occurrences;
    uint32_t first;  // The file offset of the first instruction.
} ARFunction;

typedef struct {
    const char *name;
    int32_t code;
    int32_t constructor;
} ARScript;

typedef struct {
    uint16_t character;
    uint16_t x, y, w, h;
    int16_t shift, offset;
} ARGlyph;

typedef struct {
    const char *name;
    const char *font_name;
    float size;
    int32_t bold, italic;
    int32_t range_start, range_end;
    int32_t item;  // A texture item.
    float scale_x, scale_y;
    int32_t ascender_offset;
    int32_t glyph_count;
    ARGlyph *glyphs;  // In the order of the characters.
} ARFont;

typedef struct {
    const char *name;
    uint32_t flags;
    const char *type;
    const char *file;
    float volume, pitch;
    int32_t group;
    int32_t audio;  // An index into the audio of the group, or -1.
} ARSoundInfo;

typedef struct {
    const uint8_t *bytes;
    uint32_t size;
} ARAudio;

typedef struct {
    const char *name;
    int32_t item;
    int32_t tile_w, tile_h;
    int32_t border_x, border_y;
    int32_t columns;
    int32_t tile_count;
} ARTileset;

typedef struct ARData {
    const uint8_t *bytes;
    size_t size;

    int bytecode;
    // The largest id of an instance in the rooms. The ids of new instances
    // start after it.
    int32_t last_object;
    const char *name;
    const char *display_name;
    int32_t window_w, window_h;
    float fps;
    int32_t room_order_count;
    const uint32_t *room_order;

    int32_t string_count;
    ARString *strings;
    int32_t item_count;
    ARTexItem *items;
    int32_t page_count;
    ARTexPage *pages;
    int32_t sprite_count;
    ARSprite *sprites;
    int32_t object_count;
    ARObject *objects;
    int32_t room_count;
    ARRoom *rooms;
    int32_t code_count;
    ARCode *codes;
    int32_t variable_count;
    ARVariable *variables;
    int32_t function_count;
    ARFunction *functions;
    int32_t script_count;
    ARScript *scripts;
    int32_t global_init_count;
    const uint32_t *global_inits;
    int32_t font_count;
    ARFont *fonts;
    int32_t sound_count;
    ARSoundInfo *sounds;
    int32_t audio_count;
    ARAudio *audio;
    int32_t tileset_count;
    ARTileset *tilesets;
    int32_t audio_group_count;
    const char **audio_groups;

    // Each allocation of the reader, so that ar_data_free can free them.
    void **allocations;
    int allocation_count, allocation_capacity;
    char error[160];
} ARData;

// Reads the tables from the bytes of a data file. Returns 0 and sets
// data->error when the file is not a data file that ar_data can read.
int ar_data_read(ARData *data, const uint8_t *bytes, size_t size);

// Frees the tables. It does not free the bytes.
void ar_data_free(ARData *data);

// Finds the start and the size of a chunk. Returns 0 when it is not there.
int ar_data_chunk(const uint8_t *bytes, size_t size, const char *name, uint32_t *offset, uint32_t *length);

// Reads only the AUDO chunk of an audio group file (audiogroupN.dat).
int ar_data_read_audio_group(const uint8_t *bytes, size_t size, ARAudio **audio, int32_t *count);

// Finds an entry by name. Returns -1 when there is none.
int ar_data_find_sprite(const ARData *data, const char *name);
int ar_data_find_object(const ARData *data, const char *name);
int ar_data_find_room(const ARData *data, const char *name);
int ar_data_find_code(const ARData *data, const char *name);

#endif
