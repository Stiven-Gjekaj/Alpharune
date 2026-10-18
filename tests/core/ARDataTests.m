// Tests of ar_data with small data files that the tests build.

#import "ARTest.h"
#include "ARForm.h"
#include "ar_data.h"

// The GEN8 chunk of bytecode 17, with a window of 640 x 480, two rooms in
// the room order, and 30 frames each second.
static void put_gen8(ARForm *f) {
    ar_form_chunk(f, "GEN8");
    ar_form_u32(f, 1 | 17 << 8);  // debug off, bytecode 17
    ar_form_str(f, "GAME");      // file name
    ar_form_str(f, "Default");   // config
    ar_form_u32(f, 100000);      // last object
    ar_form_u32(f, 10000000);    // last tile
    ar_form_u32(f, 0);           // game id
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // guid
    ar_form_str(f, "GAME");      // name
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // version
    ar_form_u32(f, 640);
    ar_form_u32(f, 480);
    ar_form_u32(f, 0);           // info
    ar_form_u32(f, 0);           // licence crc
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // licence md5
    ar_form_u32(f, 0);           // timestamp
    ar_form_u32(f, 0);
    ar_form_str(f, "The Game");  // display name
    for (int i = 0; i < 4; i++) ar_form_u32(f, 0);  // targets, classes
    ar_form_u32(f, 0);           // steam
    ar_form_u32(f, 0);           // debugger port
    ar_form_u32(f, 2);           // room order
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    for (int i = 0; i < 10; i++) ar_form_u32(f, 0);  // five random numbers
    ar_form_f32(f, 30);
}

static void put_list(ARForm *f, int count, const char *prefix) {
    char label[48];
    ar_form_u32(f, (uint32_t)count);
    for (int i = 0; i < count; i++) {
        snprintf(label, sizeof(label), "%s%d", prefix, i);
        ar_form_ptr(f, label);
    }
}

static void label(ARForm *f, const char *prefix, int i) {
    char text[48];
    snprintf(text, sizeof(text), "%s%d", prefix, i);
    ar_form_label(f, text);
}

AR_TEST(test_data_refuses_a_file_that_is_not_a_form) {
    ARData data;
    const uint8_t bytes[] = "RIFF\4\0\0\0WAVE";
    AR_EXPECT(!ar_data_read(&data, bytes, sizeof(bytes)));
    AR_EXPECT(strlen(data.error) > 0);
}

AR_TEST(test_data_refuses_a_file_with_an_old_bytecode) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    size_t size;
    uint8_t *bytes = (uint8_t *)ar_form_finish(f, &size);
    bytes[17] = 14;  // The second byte of the GEN8 data.
    ARData data;
    AR_EXPECT(!ar_data_read(&data, bytes, size));
    AR_EXPECT(strstr(data.error, "14") != NULL);
    ar_form_free(f);
}

AR_TEST(test_data_reads_the_general_info) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.bytecode, 17);
    AR_EQUAL(@(data.name), @"GAME");
    AR_EQUAL(@(data.display_name), @"The Game");
    AR_EQUAL_INT(data.window_w, 640);
    AR_EQUAL_INT(data.window_h, 480);
    AR_EQUAL_INT(data.fps, 30);
    AR_EQUAL_INT(data.room_order_count, 2);
    AR_EQUAL_INT(data.room_order[0], 1);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_refuses_a_damaged_list) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "SCPT");
    ar_form_u32(f, 1000000);  // A count that is larger than the file.
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(!ar_data_read(&data, bytes, size));
    ar_form_free(f);
}

// Two texture items on one page, and a sprite of two frames.
static void put_sprite(ARForm *f) {
    ar_form_chunk(f, "TPAG");
    put_list(f, 2, "item");
    for (int i = 0; i < 2; i++) {
        label(f, "item", i);
        uint16_t fields[11] = {(uint16_t)(16 * i), 0, 16, 8, 0, 0, 16, 8, 16, 8, 0};
        for (int k = 0; k < 11; k++) ar_form_u16(f, fields[k]);
    }
    ar_form_chunk(f, "TXTR");
    put_list(f, 1, "page");
    label(f, "page", 0);
    uint32_t page[7] = {0, 0, 4, 32, 8, 0, 0};
    for (int k = 0; k < 6; k++) ar_form_u32(f, page[k]);
    ar_form_ptr(f, "pagebytes");
    ar_form_label(f, "pagebytes");
    ar_form_bytes(f, "2zoq", 4);

    ar_form_chunk(f, "SPRT");
    put_list(f, 1, "sprite");
    label(f, "sprite", 0);
    ar_form_str(f, "spr_kris");
    int32_t head[13] = {16, 8, 1, 14, 7, 2, 0, 0, 0, 0, 1, 8, 4};
    for (int k = 0; k < 13; k++) ar_form_u32(f, (uint32_t)head[k]);
    ar_form_u32(f, 0xFFFFFFFF);  // GameMaker 2
    ar_form_u32(f, 3);           // version
    ar_form_u32(f, 0);           // type
    ar_form_f32(f, 0.25f);       // speed
    ar_form_u32(f, 1);           // frames each game frame
    ar_form_u32(f, 0);           // sequence
    ar_form_u32(f, 0);           // nine slice
    ar_form_u32(f, 2);
    ar_form_ptr(f, "item1");
    ar_form_ptr(f, "item0");
    ar_form_u32(f, 1);  // one mask of 2 x 8 bytes
    for (int k = 0; k < 16; k++) ar_form_bytes(f, "\xff", 1);
}

AR_TEST(test_data_reads_a_sprite_and_its_texture_items) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    put_sprite(f);
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.item_count, 2);
    AR_EQUAL_INT(data.items[1].source_x, 16);
    AR_EQUAL_INT(data.items[1].target_w, 16);
    AR_EQUAL_INT(data.page_count, 1);
    AR_EQUAL_INT(data.pages[0].width, 32);
    AR_EQUAL_INT(data.pages[0].size, 4);
    AR_EXPECT(memcmp(data.pages[0].bytes, "2zoq", 4) == 0);
    AR_EQUAL_INT(data.sprite_count, 1);
    ARSprite *s = &data.sprites[0];
    AR_EQUAL(@(s->name), @"spr_kris");
    AR_EQUAL_INT(s->width, 16);
    AR_EQUAL_INT(s->margin_right, 14);
    AR_EQUAL_INT(s->sep_masks, 1);
    AR_EQUAL_INT(s->origin_x, 8);
    AR_EQUAL_INT(s->origin_y, 4);
    AR_EQUAL_INT(s->speed * 100, 25);
    AR_EQUAL_INT(s->frame_count, 2);
    AR_EQUAL_INT(s->frames[0], 1);
    AR_EQUAL_INT(s->frames[1], 0);
    AR_EQUAL_INT(s->mask_count, 1);
    AR_EQUAL_INT(s->masks[15], 0xFF);
    AR_EQUAL_INT(ar_data_find_sprite(&data, "spr_kris"), 0);
    AR_EQUAL_INT(ar_data_find_sprite(&data, "spr_susie"), -1);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_an_object_with_a_parent_and_events) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "OBJT");
    put_list(f, 1, "object");
    label(f, "object", 0);
    ar_form_str(f, "obj_door");
    int32_t head[20] = {3, 1, 0, 1, -10, 0, 7, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0};
    for (int k = 0; k < 20; k++) ar_form_u32(f, (uint32_t)head[k]);
    // The vertex count (at word 17) is 1, so two floats come here.
    ar_form_f32(f, 1);
    ar_form_f32(f, 2);
    put_list(f, AR_EV_COUNT, "evtype");
    for (int t = 0; t < AR_EV_COUNT; t++) {
        label(f, "evtype", t);
        if (t == AR_EV_ALARM) {
            ar_form_u32(f, 2);
            ar_form_ptr(f, "alarm0");
            ar_form_ptr(f, "alarm3");
            int subs[2] = {0, 3};
            for (int k = 0; k < 2; k++) {
                char name[16];
                snprintf(name, sizeof(name), "alarm%d", subs[k]);
                ar_form_label(f, name);
                ar_form_u32(f, (uint32_t)subs[k]);
                ar_form_u32(f, 1);
                snprintf(name, sizeof(name), "action%d", k);
                ar_form_ptr(f, name);
                ar_form_label(f, name);
                uint32_t action[14] = {1, 603, 7, 0, 0, 0, 2, 0, (uint32_t)(40 + k), 0, 0xFFFFFFFF, 0, 0, 0};
                for (int w = 0; w < 14; w++) ar_form_u32(f, action[w]);
            }
        } else {
            ar_form_u32(f, 0);
        }
    }
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.object_count, 1);
    ARObject *o = &data.objects[0];
    AR_EQUAL(@(o->name), @"obj_door");
    AR_EQUAL_INT(o->sprite, 3);
    AR_EQUAL_INT(o->depth, -10);
    AR_EQUAL_INT(o->parent, 7);
    AR_EQUAL_INT(o->event_count[AR_EV_CREATE], 0);
    AR_EQUAL_INT(o->event_count[AR_EV_ALARM], 2);
    AR_EQUAL_INT(o->events[AR_EV_ALARM][1].subtype, 3);
    AR_EQUAL_INT(o->events[AR_EV_ALARM][1].code, 41);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_gives_minus_one_for_no_parent) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "OBJT");
    put_list(f, 1, "object");
    label(f, "object", 0);
    ar_form_str(f, "obj_root");
    int32_t head[20] = {-1, 1, 0, 0, 0, 0, -100, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 20; k++) ar_form_u32(f, (uint32_t)head[k]);
    ar_form_u32(f, 0);  // no event types
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.objects[0].parent, -1);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_a_room_with_views_instances_and_layers) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "ROOM");
    put_list(f, 1, "room");
    label(f, "room", 0);
    ar_form_str(f, "room_town");
    ar_form_u32(f, 0);      // caption
    ar_form_u32(f, 1280);
    ar_form_u32(f, 480);
    ar_form_u32(f, 0);      // speed
    ar_form_u32(f, 1);      // persistent
    ar_form_u32(f, 0xFF000000);
    ar_form_u32(f, 1);
    ar_form_u32(f, 12);     // creation code
    ar_form_u32(f, 0x30002);
    ar_form_ptr(f, "backgrounds");
    ar_form_ptr(f, "views");
    ar_form_ptr(f, "objects");
    ar_form_ptr(f, "tiles");
    for (int k = 0; k < 6; k++) ar_form_u32(f, 0);
    ar_form_f32(f, 0);
    ar_form_f32(f, 0.1f);
    ar_form_ptr(f, "layers");
    ar_form_ptr(f, "sequences");

    ar_form_label(f, "backgrounds");
    ar_form_u32(f, 0);
    ar_form_label(f, "tiles");
    ar_form_u32(f, 0);
    ar_form_label(f, "sequences");
    ar_form_u32(f, 0);
    ar_form_label(f, "views");
    put_list(f, 1, "view");
    label(f, "view", 0);
    int32_t view[14] = {1, 10, 20, 320, 240, 0, 0, 640, 480, 160, 120, -1, -1, 5};
    for (int k = 0; k < 14; k++) ar_form_u32(f, (uint32_t)view[k]);
    ar_form_label(f, "objects");
    put_list(f, 1, "inst");
    label(f, "inst", 0);
    ar_form_u32(f, 100);
    ar_form_u32(f, (uint32_t)-50);
    ar_form_u32(f, 4);
    ar_form_u32(f, 100007);
    ar_form_u32(f, 33);
    ar_form_f32(f, 2);
    ar_form_f32(f, -1);
    ar_form_f32(f, 1);
    ar_form_u32(f, 3);
    ar_form_u32(f, 0xFFFFFFFF);
    ar_form_f32(f, 90);
    ar_form_u32(f, 0xFFFFFFFF);

    ar_form_label(f, "layers");
    put_list(f, 3, "layer");
    // An instance layer.
    label(f, "layer", 0);
    ar_form_str(f, "Instances");
    ar_form_u32(f, 1);
    ar_form_u32(f, AR_LAYER_INSTANCES);
    ar_form_u32(f, 100);
    for (int k = 0; k < 4; k++) ar_form_f32(f, 0);
    ar_form_u32(f, 1);
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    ar_form_u32(f, 1);
    ar_form_u32(f, 100007);
    // A background layer with a sprite that repeats.
    label(f, "layer", 1);
    ar_form_str(f, "Background");
    ar_form_u32(f, 2);
    ar_form_u32(f, AR_LAYER_BACKGROUND);
    ar_form_u32(f, 1000);
    ar_form_f32(f, 0);
    ar_form_f32(f, 0);
    ar_form_f32(f, -2);
    ar_form_f32(f, 0);
    ar_form_u32(f, 1);
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    uint32_t bg[10] = {1, 0, 9, 1, 0, 0, 0x80FFFFFF, 0, 0, 0};
    for (int k = 0; k < 10; k++) ar_form_u32(f, bg[k]);
    // An asset layer with one legacy tile.
    label(f, "layer", 2);
    ar_form_str(f, "Tiles");
    ar_form_u32(f, 3);
    ar_form_u32(f, AR_LAYER_ASSETS);
    ar_form_u32(f, 10000);
    for (int k = 0; k < 4; k++) ar_form_f32(f, 0);
    ar_form_u32(f, 1);
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    ar_form_ptr(f, "legacy");
    ar_form_ptr(f, "empty");
    ar_form_ptr(f, "empty");
    ar_form_label(f, "empty");
    ar_form_u32(f, 0);
    ar_form_label(f, "legacy");
    put_list(f, 1, "tile");
    label(f, "tile", 0);
    uint32_t tile[9] = {40, 160, 12, 20, 0, 20, 20, 10000, 10000001};
    for (int k = 0; k < 9; k++) ar_form_u32(f, tile[k]);
    ar_form_f32(f, 1);
    ar_form_f32(f, 1);
    ar_form_u32(f, 0xFFFFFFFF);

    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.room_count, 1);
    ARRoom *r = &data.rooms[0];
    AR_EQUAL(@(r->name), @"room_town");
    AR_EQUAL_INT(r->width, 1280);
    AR_EQUAL_INT(r->persistent, 1);
    AR_EQUAL_INT(r->creation_code, 12);
    AR_EQUAL_INT(r->view_count, 1);
    AR_EQUAL_INT(r->views[0].view_y, 20);
    AR_EQUAL_INT(r->views[0].port_w, 640);
    AR_EQUAL_INT(r->views[0].object, 5);
    AR_EQUAL_INT(r->instance_count, 1);
    AR_EQUAL_INT(r->instances[0].y, -50);
    AR_EQUAL_INT(r->instances[0].id, 100007);
    AR_EQUAL_INT(r->instances[0].creation_code, 33);
    AR_EQUAL_INT(r->instances[0].scale_y, -1);
    AR_EQUAL_INT(r->instances[0].rotation, 90);
    AR_EQUAL_INT(r->layer_count, 3);
    AR_EQUAL(@(r->layers[0].name), @"Instances");
    AR_EQUAL_INT(r->layers[0].instance_count, 1);
    AR_EQUAL_INT(r->layers[0].instances[0], 100007);
    AR_EQUAL_INT(r->layers[1].bg_sprite, 9);
    AR_EQUAL_INT(r->layers[1].bg_htiled, 1);
    AR_EQUAL_INT(r->layers[1].hspeed, -2);
    AR_EQUAL_INT(r->layers[1].bg_alpha * 255 + 0.5f, 128);
    AR_EQUAL_INT(r->layers[2].tile_count, 1);
    AR_EQUAL_INT(r->layers[2].tiles[0].y, 160);
    AR_EQUAL_INT(r->layers[2].tiles[0].sprite, 12);
    AR_EQUAL_INT(r->layers[2].tiles[0].source_x, 20);
    AR_EQUAL_INT(r->layers[2].tiles[0].depth, 10000);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_code_variables_functions_and_scripts) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "CODE");
    put_list(f, 2, "code");
    ar_form_label(f, "blob");
    ar_form_u32(f, 0xB6000002);  // b +8
    ar_form_u32(f, 0x9D020000);  // exit.i
    ar_form_u32(f, 0x9D020000);  // exit.i
    for (int i = 0; i < 2; i++) {
        label(f, "code", i);
        ar_form_str(f, i == 0 ? "gml_GlobalScript_scr_a" : "gml_Script_scr_a");
        ar_form_u32(f, 12);
        ar_form_u16(f, (uint16_t)(1 + i));
        ar_form_u16(f, (uint16_t)(i == 1 ? 0x8002 : 0));
        // The address of the blob, relative to this field. The test sets
        // it after ar_form_finish.
        ar_form_u32(f, 0);
        ar_form_u32(f, (uint32_t)(i * 8));
    }
    size_t size;
    uint8_t *bytes = (uint8_t *)ar_form_finish(f, &size);
    // Set each relative address now that the offset of the blob is known.
    uint32_t blob = 0;
    for (size_t p = 8; p + 4 <= size; p += 4) {
        if (memcmp(bytes + p, "\x02\x00\x00\xb6", 4) == 0) {
            blob = (uint32_t)p;
            break;
        }
    }
    AR_EXPECT(blob > 0);
    uint32_t o, n;
    AR_EXPECT(ar_data_chunk(bytes, size, "CODE", &o, &n));
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t entry = (uint32_t)bytes[o + 4 + 4 * i] | (uint32_t)bytes[o + 5 + 4 * i] << 8 |
                         (uint32_t)bytes[o + 6 + 4 * i] << 16 | (uint32_t)bytes[o + 7 + 4 * i] << 24;
        int32_t rel = (int32_t)blob - (int32_t)(entry + 12);
        memcpy(bytes + entry + 12, &rel, 4);
    }
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.code_count, 2);
    AR_EQUAL(@(data.codes[1].name), @"gml_Script_scr_a");
    AR_EQUAL_INT(data.codes[1].offset, 8);
    AR_EQUAL_INT(data.codes[1].arguments, 2);
    AR_EQUAL_INT(data.codes[1].locals, 2);
    AR_EXPECT(data.codes[0].blob == bytes + blob);
    AR_EQUAL_INT(ar_data_find_code(&data, "gml_Script_scr_a"), 1);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_the_variable_and_function_tables) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    ar_form_chunk(f, "VARI");
    ar_form_u32(f, 2);
    ar_form_u32(f, 2);
    ar_form_u32(f, 1);
    ar_form_str(f, "hp");
    ar_form_u32(f, (uint32_t)-5);
    ar_form_u32(f, 0);
    ar_form_u32(f, 3);
    ar_form_u32(f, 1234);
    ar_form_str(f, "x");
    ar_form_u32(f, (uint32_t)-1);
    ar_form_u32(f, 1);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0xFFFFFFFF);
    ar_form_chunk(f, "FUNC");
    ar_form_u32(f, 1);
    ar_form_str(f, "instance_create");
    ar_form_u32(f, 2);
    ar_form_u32(f, 504);
    ar_form_u32(f, 0);  // no code locals
    ar_form_chunk(f, "SCPT");
    put_list(f, 2, "script");
    label(f, "script", 0);
    ar_form_str(f, "scr_a");
    ar_form_u32(f, 7);
    label(f, "script", 1);
    ar_form_str(f, "gml_Script_new_struct");
    ar_form_u32(f, 0x80000009);
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.variable_count, 2);
    AR_EQUAL(@(data.variables[0].name), @"hp");
    AR_EQUAL_INT(data.variables[0].instance_type, -5);
    AR_EQUAL_INT(data.variables[0].occurrences, 3);
    AR_EQUAL_INT(data.variables[0].first, 1234);
    AR_EQUAL_INT(data.variables[1].instance_type, -1);
    AR_EQUAL_INT(data.function_count, 1);
    AR_EQUAL(@(data.functions[0].name), @"instance_create");
    AR_EQUAL_INT(data.functions[0].first, 500);
    AR_EQUAL_INT(data.script_count, 2);
    AR_EQUAL_INT(data.scripts[0].code, 7);
    AR_EQUAL_INT(data.scripts[0].constructor, 0);
    AR_EQUAL_INT(data.scripts[1].code, 9);
    AR_EQUAL_INT(data.scripts[1].constructor, 1);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_a_font_a_sound_and_the_audio) {
    ARForm *f = ar_form_new();
    put_gen8(f);
    put_sprite(f);
    ar_form_chunk(f, "FONT");
    put_list(f, 1, "font");
    label(f, "font", 0);
    ar_form_str(f, "fnt_main");
    ar_form_str(f, "Determination");
    ar_form_f32(f, -16);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    ar_form_u16(f, 32);
    ar_form_u16(f, 0x0100);
    ar_form_u32(f, 127);
    ar_form_ptr(f, "item1");
    ar_form_f32(f, 1);
    ar_form_f32(f, 2);
    ar_form_u32(f, 3);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    put_list(f, 2, "glyph");
    for (int i = 0; i < 2; i++) {
        label(f, "glyph", i);
        uint16_t g[8] = {(uint16_t)('A' + i), (uint16_t)(8 * i), 2, 7, 12, 8, (uint16_t)-1, 0};
        for (int k = 0; k < 8; k++) ar_form_u16(f, g[k]);
    }
    ar_form_chunk(f, "SOND");
    put_list(f, 1, "sound");
    label(f, "sound", 0);
    ar_form_str(f, "snd_select");
    ar_form_u32(f, 101);
    ar_form_str(f, ".wav");
    ar_form_str(f, "snd_select.wav");
    ar_form_u32(f, 0);
    ar_form_f32(f, 0.5f);
    ar_form_f32(f, 1);
    ar_form_u32(f, 0);
    ar_form_u32(f, 0);
    ar_form_chunk(f, "AUDO");
    put_list(f, 1, "audio");
    label(f, "audio", 0);
    ar_form_u32(f, 4);
    ar_form_bytes(f, "RIFF", 4);
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARData data;
    AR_EXPECT(ar_data_read(&data, bytes, size));
    AR_EQUAL_INT(data.font_count, 1);
    ARFont *font = &data.fonts[0];
    AR_EQUAL(@(font->font_name), @"Determination");
    AR_EQUAL_INT(font->size, -16);
    AR_EQUAL_INT(font->range_start, 32);
    AR_EQUAL_INT(font->range_end, 127);
    AR_EQUAL_INT(font->item, 1);
    AR_EQUAL_INT(font->scale_y, 2);
    AR_EQUAL_INT(font->ascender_offset, 3);
    AR_EQUAL_INT(font->glyph_count, 2);
    AR_EQUAL_INT(font->glyphs[1].character, 'B');
    AR_EQUAL_INT(font->glyphs[1].x, 8);
    AR_EQUAL_INT(font->glyphs[1].shift, 8);
    AR_EQUAL_INT(font->glyphs[1].offset, -1);
    AR_EQUAL_INT(data.sound_count, 1);
    AR_EQUAL(@(data.sounds[0].file), @"snd_select.wav");
    AR_EQUAL_INT(data.sounds[0].volume * 10, 5);
    AR_EQUAL_INT(data.audio_count, 1);
    AR_EQUAL_INT(data.audio[0].size, 4);
    AR_EXPECT(memcmp(data.audio[0].bytes, "RIFF", 4) == 0);
    ar_data_free(&data);
    ar_form_free(f);
}

AR_TEST(test_data_reads_the_audio_of_an_audio_group_file) {
    ARForm *f = ar_form_new();
    ar_form_chunk(f, "AUDO");
    put_list(f, 2, "audio");
    for (int i = 0; i < 2; i++) {
        label(f, "audio", i);
        ar_form_u32(f, 3);
        ar_form_bytes(f, i ? "Ogg" : "WAV", 3);
    }
    size_t size;
    const uint8_t *bytes = ar_form_finish(f, &size);
    ARAudio *audio;
    int32_t count;
    AR_EXPECT(ar_data_read_audio_group(bytes, size, &audio, &count));
    AR_EQUAL_INT(count, 2);
    AR_EXPECT(memcmp(audio[1].bytes, "Ogg", 3) == 0);
    free(audio);
    ar_form_free(f);
}
