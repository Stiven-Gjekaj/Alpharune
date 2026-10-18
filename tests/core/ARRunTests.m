// Tests of the runtime: rooms, events, alarms, collisions, and the library.

#import "ARTest.h"
#include "ARAsm.h"
#include "ar_audio.h"
#include "ar_run.h"
#include "ar_vm.h"

typedef struct {
    ARAsm *a;
    ARData data;
    ARRun *run;
    ARVM *vm;
} Game;

// A run of the runtime has no renderer here, so the draw calls do nothing.
static int start_game(Game *g) {
    size_t size;
    const uint8_t *bytes = ar_asm_finish(g->a, &size);
    if (!ar_data_read(&g->data, bytes, size)) return 0;
    NSString *dir = ARTestTemporaryDirectory();
    ARRunConfig config;
    memset(&config, 0, sizeof(config));
    config.game_dir = dir.fileSystemRepresentation;
    config.save_dir = dir.fileSystemRepresentation;
    char error[128];
    g->run = ar_run_new(&g->data, &config, error, sizeof(error));
    if (!g->run) return 0;
    return 1;
}

static void end_game(Game *g) {
    ar_run_free(g->run);
    ar_data_free(&g->data);
    ar_asm_free(g->a);
}

// The code "global.order = global.order * 10 + k".
static int order_code(ARAsm *a, const char *name, int k) {
    char source[256];
    snprintf(source, sizeof(source),
             "pushglb.v -5.order\n"
             "pushi 10\n"
             "mul.i.v\n"
             "pushi %d\n"
             "add.i.v\n"
             "pop.v.v -5.order\n",
             k);
    return ar_asm_code(a, name, 0, source);
}

static ARVM *vm_of(Game *g);

AR_TEST(test_run_starts_a_room_in_the_order_of_gamemaker) {
    Game g = {ar_asm_new()};
    // The global script sets order to 0; then create (1), creation code (2),
    // game start (3), room creation code (4), and room start (5).
    int init = ar_asm_code(g.a, "gml_GlobalScript_init", 0, "call t_capture_vm 0\npopz.v\npushi 0\npop.v.i -5.order\n");
    ar_asm_global_script(g.a, init);
    int obj = ar_asm_object(g.a, "obj_a", -1);
    ar_asm_event(g.a, obj, AR_EV_CREATE, 0, order_code(g.a, "gml_Object_obj_a_Create_0", 1));
    ar_asm_event(g.a, obj, AR_EV_OTHER, 2, order_code(g.a, "gml_Object_obj_a_Other_2", 3));
    ar_asm_event(g.a, obj, AR_EV_OTHER, 4, order_code(g.a, "gml_Object_obj_a_Other_4", 5));
    int cc = order_code(g.a, "gml_RoomCC_room_a_0", 2);
    int rc = order_code(g.a, "gml_Room_room_a_Create", 4);
    ar_asm_room(g.a, "room_a", 320, 240, rc);
    ar_asm_instance(g.a, obj, 10, 20, cc);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ARVM *vm = vm_of(&g);
    ARValue *order = ar_vars_find(&ar_vm_global(vm)->vars, ar_vm_slot(vm, "order"));
    AR_EXPECT(order != NULL);
    AR_EQUAL_INT(order ? order->r : 0, 12345);
    end_game(&g);
}

// The VM of a run: the tests reach it with a debug command of its own.
static ARVM *gVM;

static ARValue t_capture_vm(ARVM *vm, int argc, ARValue *args) {
    (void)argc;
    (void)args;
    gVM = vm;
    return ar_undefined();
}

static const ARBuiltin kRunTest[] = {{"t_capture_vm", t_capture_vm}};

__attribute__((constructor)) static void register_run_test(void) {
    ar_vm_register(kRunTest, 1, NULL, 0);
}

static ARVM *vm_of(Game *g) {
    (void)g;
    return gVM;
}

// Each test game has a global script that captures the VM.
static void add_capture(ARAsm *a) {
    int c = ar_asm_code(a, "gml_GlobalScript_capture", 0, "call t_capture_vm 0\npopz.v\n");
    ar_asm_global_script(a, c);
}

static ARValue call(ARVM *vm, const char *name, int argc, ARValue *args) {
    int32_t b = ar_vm_builtin(vm, name);
    if (b < 0) return ar_real(-99999);
    return ar_vm_call(vm, ar_real(AR_BUILTIN_BASE + b), ar_vm_global(vm), ar_vm_global(vm), argc, args);
}

static ARValue str(ARVM *vm, const char *text) {
    return ar_strv(ar_str_cstr(ar_vm_heap(vm), text));
}

static double global_of(ARVM *vm, const char *name) {
    ARValue *v = ar_vars_find(&ar_vm_global(vm)->vars, ar_vm_slot(vm, name));
    return v ? ar_to_real(*v) : -1;
}

AR_TEST(test_run_counts_down_an_alarm_and_runs_its_event) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int obj = ar_asm_object(g.a, "obj_timer", -1);
    // Create: alarm[0] = 3. Alarm 0: global.fired = 1.
    ar_asm_event(g.a, obj, AR_EV_CREATE, 0,
                 ar_asm_code(g.a, "gml_Object_obj_timer_Create_0", 0,
                             "pushi -1\npushi 0\npushi 3\npop.i.v -1.alarm array\n"
                             "pushi 0\npop.v.i -5.fired\n"));
    ar_asm_event(g.a, obj, AR_EV_ALARM, 0,
                 ar_asm_code(g.a, "gml_Object_obj_timer_Alarm_0", 0, "pushi 1\npop.v.i -5.fired\n"));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, obj, 0, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ARVM *vm = vm_of(&g);
    ar_run_frame(g.run);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "fired"), 0);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "fired"), 1);
    end_game(&g);
}

AR_TEST(test_run_changes_the_room_and_keeps_a_persistent_instance) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int keeper = ar_asm_object(g.a, "obj_keeper", -1);
    ar_asm_object_sprite(g.a, keeper, -1, 1);
    int temp = ar_asm_object(g.a, "obj_temp", -1);
    // obj_keeper: step goes to room 1 in the first frame. Room end counts.
    ar_asm_event(g.a, keeper, AR_EV_STEP, 0,
                 ar_asm_code(g.a, "gml_Object_obj_keeper_Step_0", 0,
                             "push.v -1.room\npushi 0\ncmp.i.v eq\nbf end\npushi 1\npop.v.i -1.room\nend:\n"));
    ar_asm_event(g.a, keeper, AR_EV_OTHER, 5,
                 ar_asm_code(g.a, "gml_Object_obj_keeper_Other_5", 0,
                             "pushglb.v -5.ends\npushi 1\nadd.i.v\npop.v.v -5.ends\n"));
    ar_asm_event(g.a, keeper, AR_EV_CREATE, 0,
                 ar_asm_code(g.a, "gml_Object_obj_keeper_Create_0", 0, "pushi 0\npop.v.i -5.ends\n"));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, keeper, 0, 0, -1);
    ar_asm_instance(g.a, temp, 0, 0, -1);
    ar_asm_room(g.a, "room_b", 640, 480, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    AR_EQUAL(@(ar_run_room_name(g.run)), @"room_a");
    ar_run_frame(g.run);
    AR_EQUAL(@(ar_run_room_name(g.run)), @"room_b");
    ARVM *vm = vm_of(&g);
    AR_EQUAL_INT(ar_vm_instance_count(vm), 1);
    AR_EQUAL_INT(ar_vm_instance_at(vm, 0)->object, keeper);
    AR_EQUAL_INT(global_of(vm, "ends"), 1);
    end_game(&g);
}

AR_TEST(test_run_steps_a_new_room_before_it_draws_it) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int go = ar_asm_object(g.a, "obj_go", -1);
    int probe = ar_asm_object(g.a, "obj_probe", -1);
    // obj_go goes to room 1. In room 1, obj_probe adds 1 in its step and 2
    // in its draw, so the order of the first frame there is 12.
    ar_asm_event(g.a, go, AR_EV_STEP, 0,
                 ar_asm_code(g.a, "gml_Object_obj_go_Step_0", 0, "pushi 1\npop.v.i -1.room\n"));
    ar_asm_event(g.a, probe, AR_EV_CREATE, 0,
                 ar_asm_code(g.a, "gml_Object_obj_probe_Create_0", 0, "pushi 0\npop.v.i -5.order\n"));
    ar_asm_event(g.a, probe, AR_EV_STEP, 0, order_code(g.a, "gml_Object_obj_probe_Step_0", 1));
    ar_asm_event(g.a, probe, AR_EV_DRAW, 0, order_code(g.a, "gml_Object_obj_probe_Draw_0", 2));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, go, 0, 0, -1);
    ar_asm_room(g.a, "room_b", 320, 240, -1);
    ar_asm_instance(g.a, probe, 0, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ar_run_frame(g.run);
    AR_EQUAL(@(ar_run_room_name(g.run)), @"room_b");
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm_of(&g), "order"), 12);
    end_game(&g);
}

AR_TEST(test_run_draws_the_newer_of_two_instances_of_one_depth_first) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int first = ar_asm_object(g.a, "obj_first", -1);
    int second = ar_asm_object(g.a, "obj_second", -1);
    // Both have the depth 0. obj_first is made first, so obj_second draws
    // first, and the order of the draw is 21.
    int init = ar_asm_code(g.a, "gml_Room_room_a_Create", 0, "pushi 0\npop.v.i -5.order\n");
    ar_asm_event(g.a, first, AR_EV_DRAW, 0, order_code(g.a, "gml_Object_obj_first_Draw_0", 1));
    ar_asm_event(g.a, second, AR_EV_DRAW, 0, order_code(g.a, "gml_Object_obj_second_Draw_0", 2));
    ar_asm_room(g.a, "room_a", 320, 240, init);
    ar_asm_instance(g.a, first, 0, 0, -1);
    ar_asm_instance(g.a, second, 0, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm_of(&g), "order"), 21);
    end_game(&g);
}

AR_TEST(test_run_gives_new_instances_ids_after_the_ids_of_the_rooms) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int maker = ar_asm_object(g.a, "obj_maker", -1);
    int other = ar_asm_object(g.a, "obj_other", -1);
    // The create event of the first instance makes an instance before the
    // runtime makes the second instance of the room.
    char source[256];
    snprintf(source, sizeof(source),
             "pushi %d\nconv.i.v\npushi 0\nconv.i.v\npushi 0\nconv.i.v\npushi 0\nconv.i.v\n"
             "call instance_create_depth 4\npop.v.v -5.made\n",
             other);
    ar_asm_event(g.a, maker, AR_EV_CREATE, 0, ar_asm_code(g.a, "gml_Object_obj_maker_Create_0", 0, source));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    int first = ar_asm_instance(g.a, maker, 0, 0, -1);
    int second = ar_asm_instance(g.a, other, 0, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ARVM *vm = vm_of(&g);
    AR_EQUAL_INT(ar_vm_instance_count(vm), 3);
    AR_EXPECT(ar_vm_find_instance(vm, first) != NULL);
    AR_EXPECT(ar_vm_find_instance(vm, second) != NULL);
    AR_EXPECT(global_of(vm, "made") > second);
    end_game(&g);
}

AR_TEST(test_run_makes_all_instances_of_a_room_before_their_create_events) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int reader = ar_asm_object(g.a, "obj_reader", -1);
    int marker = ar_asm_object(g.a, "obj_marker", -1);
    // The create event of the first instance reads obj_marker.x, as the
    // player of Deltarune reads the marker of its entrance.
    char source[128];
    snprintf(source, sizeof(source), "push.v %d.x\npop.v.v -5.seen\n", marker);
    ar_asm_event(g.a, reader, AR_EV_CREATE, 0, ar_asm_code(g.a, "gml_Object_obj_reader_Create_0", 0, source));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, reader, 0, 0, -1);
    ar_asm_instance(g.a, marker, 77, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    AR_EQUAL_INT(global_of(vm_of(&g), "seen"), 77);
    end_game(&g);
}

AR_TEST(test_run_finds_a_collision_with_place_meeting) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int box = ar_asm_sprite(g.a, "spr_box", 16, 16, 0, 0);
    int player = ar_asm_object(g.a, "obj_player", -1);
    int wall = ar_asm_object(g.a, "obj_wall", -1);
    ar_asm_object_sprite(g.a, player, box, 0);
    ar_asm_object_sprite(g.a, wall, box, 0);
    char source[512];
    // global.hit = place_meeting(x + 4, y, obj_wall); global.miss = place_meeting(x - 1, y, obj_wall)
    snprintf(source, sizeof(source),
             "pushi %d\nconv.i.v\npush.v -1.y\npush.v -1.x\npushi 4\nadd.i.v\ncall place_meeting 3\npop.v.v -5.hit\n"
             "pushi %d\nconv.i.v\npush.v -1.y\npush.v -1.x\npushi 1\nsub.i.v\ncall place_meeting 3\npop.v.v -5.miss\n",
             wall, wall);
    ar_asm_event(g.a, player, AR_EV_STEP, 0, ar_asm_code(g.a, "gml_Object_obj_player_Step_0", 0, source));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, player, 0, 0, -1);
    ar_asm_instance(g.a, wall, 19, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ar_run_frame(g.run);
    ARVM *vm = vm_of(&g);
    AR_EQUAL_INT(global_of(vm, "hit"), 1);
    AR_EQUAL_INT(global_of(vm, "miss"), 0);
    end_game(&g);
}

AR_TEST(test_run_sees_a_key_press_for_one_frame) {
    Game g = {ar_asm_new()};
    add_capture(g.a);
    int obj = ar_asm_object(g.a, "obj_input", -1);
    ar_asm_event(g.a, obj, AR_EV_STEP, 0,
                 ar_asm_code(g.a, "gml_Object_obj_input_Step_0", 0,
                             "pushi 90\nconv.i.v\ncall keyboard_check_pressed 1\npop.v.v -5.pressed\n"
                             "pushi 90\nconv.i.v\ncall keyboard_check 1\npop.v.v -5.held\n"));
    ar_asm_room(g.a, "room_a", 320, 240, -1);
    ar_asm_instance(g.a, obj, 0, 0, -1);
    AR_EXPECT(start_game(&g));
    ar_run_start(g.run);
    ARVM *vm = vm_of(&g);
    ar_run_key(g.run, 'Z', 1);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "pressed"), 1);
    AR_EQUAL_INT(global_of(vm, "held"), 1);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "pressed"), 0);
    AR_EQUAL_INT(global_of(vm, "held"), 1);
    ar_run_key(g.run, 'Z', 0);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "held"), 0);
    // A press and a release in one frame: the key is down for one frame.
    ar_run_key(g.run, 'Z', 1);
    ar_run_key(g.run, 'Z', 0);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "pressed"), 1);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "held"), 0);
    // A release and then a press in one frame: the key stays down.
    ar_run_key(g.run, 'Z', 1);
    ar_run_frame(g.run);
    ar_run_key(g.run, 'Z', 0);
    ar_run_key(g.run, 'Z', 1);
    ar_run_frame(g.run);
    ar_run_frame(g.run);
    ar_run_frame(g.run);
    AR_EQUAL_INT(global_of(vm, "held"), 1);
    end_game(&g);
}

// The library, called on a small game.
static Game gLib;

static ARVM *lib(void) {
    if (!gLib.run) {
        gLib.a = ar_asm_new();
        add_capture(gLib.a);
        ar_asm_room(gLib.a, "room_a", 320, 240, -1);
        start_game(&gLib);
        ar_run_start(gLib.run);
    }
    return vm_of(&gLib);
}

AR_TEST(test_lib_writes_a_list_in_the_hex_form_of_gamemaker) {
    ARVM *vm = lib();
    ARValue list = call(vm, "ds_list_create", 0, NULL);
    ARValue add[3] = {list, ar_real(1), str(vm, "ab")};
    call(vm, "ds_list_add", 3, add);
    ARValue text = call(vm, "ds_list_write", 1, &list);
    AR_EQUAL(@(text.s->text), @"2E01000002000000"
                              "00000000000000000000F03F"
                              "01000000020000006162");
    ARValue copy = call(vm, "ds_list_create", 0, NULL);
    ARValue read[2] = {copy, text};
    call(vm, "ds_list_read", 2, read);
    AR_EQUAL_INT(call(vm, "ds_list_size", 1, &copy).r, 2);
    ARValue find[2] = {copy, ar_real(1)};
    AR_EQUAL(@(call(vm, "ds_list_find_value", 2, find).s->text), @"ab");
}

AR_TEST(test_lib_keeps_map_keys_of_strings_and_numbers) {
    ARVM *vm = lib();
    ARValue map = call(vm, "ds_map_create", 0, NULL);
    for (int i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key%d", i);
        ARValue set[3] = {map, str(vm, key), ar_real(i)};
        call(vm, "ds_map_set", 3, set);
    }
    ARValue num[3] = {map, ar_real(5), str(vm, "five")};
    call(vm, "ds_map_add", 3, num);
    AR_EQUAL_INT(call(vm, "ds_map_size", 1, &map).r, 101);
    ARValue get[2] = {map, str(vm, "key42")};
    AR_EQUAL_INT(call(vm, "ds_map_find_value", 2, get).r, 42);
    ARValue get5[2] = {map, ar_real(5)};
    AR_EQUAL(@(call(vm, "ds_map_find_value", 2, get5).s->text), @"five");
    ARValue del[2] = {map, str(vm, "key42")};
    call(vm, "ds_map_delete", 2, del);
    AR_EQUAL_INT(call(vm, "ds_map_exists", 2, get).r, 0);
    AR_EQUAL_INT(call(vm, "ds_map_size", 1, &map).r, 100);
}

AR_TEST(test_lib_decodes_json_into_maps_and_lists) {
    ARVM *vm = lib();
    ARValue text = str(vm, "{ \"name\": \"Kris\", \"hp\": 90, \"items\": [1, \"a\\nb\"] }");
    ARValue map = call(vm, "json_decode", 1, &text);
    ARValue name[2] = {map, str(vm, "name")};
    AR_EQUAL(@(call(vm, "ds_map_find_value", 2, name).s->text), @"Kris");
    ARValue hp[2] = {map, str(vm, "hp")};
    AR_EQUAL_INT(call(vm, "ds_map_find_value", 2, hp).r, 90);
    ARValue items[2] = {map, str(vm, "items")};
    ARValue list = call(vm, "ds_map_find_value", 2, items);
    AR_EQUAL_INT(call(vm, "ds_list_size", 1, &list).r, 2);
    ARValue second[2] = {list, ar_real(1)};
    AR_EQUAL(@(call(vm, "ds_list_find_value", 2, second).s->text), @"a\nb");
}

AR_TEST(test_lib_string_functions_count_from_one) {
    ARVM *vm = lib();
    ARValue s = str(vm, "DELTARUNE");
    ARValue copy[3] = {s, ar_real(2), ar_real(4)};
    AR_EQUAL(@(call(vm, "string_copy", 3, copy).s->text), @"ELTA");
    ARValue at[2] = {s, ar_real(1)};
    AR_EQUAL(@(call(vm, "string_char_at", 2, at).s->text), @"D");
    ARValue pos[2] = {str(vm, "RUNE"), s};
    AR_EQUAL_INT(call(vm, "string_pos", 2, pos).r, 6);
    ARValue hash = str(vm, "a#b\\#c");
    AR_EQUAL(@(call(vm, "string_hash_to_newline", 1, &hash).s->text), @"a\nb#c");
    ARValue split[2] = {str(vm, "a,b,,c"), str(vm, ",")};
    ARValue parts = call(vm, "string_split", 2, split);
    AR_EQUAL_INT(parts.a->length, 4);
    ARValue round = ar_real(2.5);
    AR_EQUAL_INT(call(vm, "round", 1, &round).r, 2);
}

AR_TEST(test_lib_reads_and_writes_an_ini_file) {
    ARVM *vm = lib();
    ARValue name = str(vm, "test_settings.ini");
    call(vm, "ini_open", 1, &name);
    ARValue w1[3] = {str(vm, "VOLUME"), str(vm, "MUSIC"), ar_real(0.5)};
    call(vm, "ini_write_real", 3, w1);
    ARValue w2[3] = {str(vm, "LANG"), str(vm, "NAME"), str(vm, "en")};
    call(vm, "ini_write_string", 3, w2);
    call(vm, "ini_close", 0, NULL);
    call(vm, "ini_open", 1, &name);
    ARValue r1[3] = {str(vm, "VOLUME"), str(vm, "MUSIC"), ar_real(1)};
    AR_EQUAL_INT(call(vm, "ini_read_real", 3, r1).r * 10, 5);
    ARValue r2[3] = {str(vm, "LANG"), str(vm, "NAME"), str(vm, "?")};
    AR_EQUAL(@(call(vm, "ini_read_string", 3, r2).s->text), @"en");
    ARValue r3[3] = {str(vm, "LANG"), str(vm, "NONE"), str(vm, "?")};
    AR_EQUAL(@(call(vm, "ini_read_string", 3, r3).s->text), @"?");
    call(vm, "ini_close", 0, NULL);
}

AR_TEST(test_lib_round_uses_the_nearest_even_number) {
    ARVM *vm = lib();
    ARValue a = ar_real(0.5), b = ar_real(1.5), c = ar_real(-2.5);
    AR_EQUAL_INT(call(vm, "round", 1, &a).r, 0);
    AR_EQUAL_INT(call(vm, "round", 1, &b).r, 2);
    AR_EQUAL_INT(call(vm, "round", 1, &c).r, -2);
}

// A WAV of 16 bits, mono, 44100 frames each second, with frames of the
// value 1000.
static NSData *make_wav(int frames) {
    NSMutableData *d = [NSMutableData data];
    uint32_t data_size = (uint32_t)frames * 2;
    uint32_t riff = 36 + data_size;
    uint8_t head[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                        1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'};
    memcpy(head + 4, &riff, 4);
    [d appendBytes:head length:40];
    [d appendBytes:&data_size length:4];
    for (int i = 0; i < frames; i++) {
        int16_t v = 1000;
        [d appendBytes:&v length:2];
    }
    return d;
}

static Game gSound;

static ARVM *sound_game(void) {
    if (!gSound.run) {
        gSound.a = ar_asm_new();
        add_capture(gSound.a);
        NSData *wav = make_wav(44100 / 2);  // half a second
        ar_asm_sound(gSound.a, "snd_test", wav.bytes, wav.length);
        ar_asm_room(gSound.a, "room_a", 320, 240, -1);
        start_game(&gSound);
        ar_run_start(gSound.run);
    }
    return vm_of(&gSound);
}

AR_TEST(test_audio_plays_a_sound_and_mixes_its_samples) {
    ARVM *vm = sound_game();
    ar_audio_set_output(1);
    ARValue args[3] = {ar_real(0), ar_real(10), ar_bool(0)};
    ARValue voice = call(vm, "audio_play_sound", 3, args);
    AR_EXPECT(voice.r >= 200000);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 1);
    int16_t out[64];
    ar_audio_render(out, 32);
    AR_EQUAL_INT(out[0], 1000);
    AR_EQUAL_INT(out[1], 1000);
    // Half the gain gives half the value.
    ARValue gain[3] = {voice, ar_real(0.5), ar_real(0)};
    call(vm, "audio_sound_gain", 3, gain);
    ar_audio_render(out, 32);
    AR_EQUAL_INT(out[0], 500);
    call(vm, "audio_stop_sound", 1, &voice);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 0);
    ar_audio_set_output(0);
}

AR_TEST(test_audio_ends_a_sound_with_no_output_after_its_time) {
    ARVM *vm = sound_game();
    ar_audio_set_output(0);
    ARValue args[3] = {ar_real(0), ar_real(10), ar_bool(0)};
    ARValue voice = call(vm, "audio_play_sound", 3, args);
    // Half a second is 15 frames at 30 frames each second.
    for (int i = 0; i < 14; i++) ar_run_frame(gSound.run);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 1);
    ar_run_frame(gSound.run);
    ar_run_frame(gSound.run);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 0);
    ARValue sound = ar_real(0);
    AR_EQUAL_INT(call(vm, "audio_sound_length", 1, &sound).r * 100, 50);
}

AR_TEST(test_audio_loops_a_sound) {
    ARVM *vm = sound_game();
    ar_audio_set_output(0);
    ARValue args[3] = {ar_real(0), ar_real(10), ar_bool(1)};
    ARValue voice = call(vm, "audio_play_sound", 3, args);
    for (int i = 0; i < 40; i++) ar_run_frame(gSound.run);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 1);
    ARValue sound = ar_real(0);
    call(vm, "audio_stop_sound", 1, &sound);
    AR_EQUAL_INT(call(vm, "audio_is_playing", 1, &voice).r, 0);
}
