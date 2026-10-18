// Tests of the interpreter with bytecode that the tests assemble.

#import "ARTest.h"
#include "ARAsm.h"
#include "ar_vm.h"

// The builtins of these tests have names that the runtime does not use.
static ARValue gLogged[16];
static int gLogCount;

static ARValue t_log(ARVM *vm, int argc, ARValue *args) {
    (void)vm;
    if (gLogCount < 16 && argc > 0) gLogged[gLogCount++] = args[0];
    return ar_undefined();
}

static ARValue t_args(ARVM *vm, int argc, ARValue *args) {
    (void)args;
    // The self of a builtin is the self of the caller.
    ARInstance *self = ar_vm_self_instance(vm);
    return ar_real(argc * 1000 + (self ? self->id - AR_FIRST_INSTANCE_ID : -1));
}

static double gRoom = 7;

static ARValue t_x_get(ARVM *vm, ARInstance *self, int index) {
    (void)vm;
    (void)index;
    return ar_real(self->x);
}

static void t_x_set(ARVM *vm, ARInstance *self, int index, ARValue v) {
    (void)vm;
    (void)index;
    self->x = ar_to_real(v);
}

static ARValue t_room_get(ARVM *vm, ARInstance *self, int index) {
    (void)vm;
    (void)self;
    (void)index;
    return ar_real(gRoom);
}

static void t_room_set(ARVM *vm, ARInstance *self, int index, ARValue v) {
    (void)vm;
    (void)self;
    (void)index;
    gRoom = ar_to_real(v);
}

static ARValue t_alarm_get(ARVM *vm, ARInstance *self, int index) {
    (void)vm;
    return ar_real(index >= 0 && index < 12 ? self->alarm[index] : -1);
}

static void t_alarm_set(ARVM *vm, ARInstance *self, int index, ARValue v) {
    (void)vm;
    if (index >= 0 && index < 12) self->alarm[index] = ar_to_int(v);
}

static const ARBuiltin kTestFunctions[] = {{"t_log", t_log}, {"t_args", t_args}};
static const ARBuiltinVar kTestVars[] = {
    {"t_x", t_x_get, t_x_set, 1},
    {"t_room", t_room_get, t_room_set, 0},
    {"t_alarm", t_alarm_get, t_alarm_set, 1},
};

typedef struct {
    ARAsm *a;
    ARData data;
    ARVM *vm;
} Program;

static int link_program(Program *p) {
    ar_vm_register(kTestFunctions, 2, kTestVars, 3);
    size_t size;
    const uint8_t *bytes = ar_asm_finish(p->a, &size);
    if (!ar_data_read(&p->data, bytes, size)) return 0;
    p->vm = ar_vm_new(&p->data);
    gLogCount = 0;
    return p->vm != NULL;
}

static void unlink_program(Program *p) {
    ar_vm_free(p->vm);
    ar_data_free(&p->data);
    ar_asm_free(p->a);
}

// Assembles one code entry and runs it with no self. Returns its result.
static ARValue run1(const char *source, int argc, ARValue *args) {
    Program p = {ar_asm_new()};
    int code = ar_asm_code(p.a, "gml_Script_test", argc, source);
    if (code < 0) {
        printf("asm: %s\n", ar_asm_error(p.a));
        ar_asm_free(p.a);
        return ar_real(-12345);
    }
    if (!link_program(&p)) {
        unlink_program(&p);
        return ar_real(-54321);
    }
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, argc, args);
    // A value on the heap does not stay after the program goes.
    if (r.type >= AR_STRING && r.type <= AR_METHOD) r = ar_real(-99999);
    unlink_program(&p);
    return r;
}

AR_TEST(test_vm_adds_two_arguments) {
    ARValue args[2] = {ar_real(40), ar_real(2)};
    ARValue r = run1("push.v -15.argument0\n"
                     "push.v -15.argument1\n"
                     "add.v.v\n"
                     "ret.v\n",
                     2, args);
    AR_EQUAL_INT(r.type, AR_REAL);
    AR_EQUAL_INT(r.r, 42);
}

AR_TEST(test_vm_sums_a_loop_with_a_local) {
    ARValue r = run1("pushi 0\n"
                     "pop.v.i -7.sum\n"
                     "pushi 1\n"
                     "pop.v.i -7.i\n"
                     "loop:\n"
                     "pushloc.v -7.i\n"
                     "pushi 10\n"
                     "cmp.i.v le\n"
                     "bf end\n"
                     "pushloc.v -7.sum\n"
                     "pushloc.v -7.i\n"
                     "add.v.v\n"
                     "pop.v.v -7.sum\n"
                     "pushloc.v -7.i\n"
                     "push.e 1\n"
                     "add.i.v\n"
                     "pop.v.v -7.i\n"
                     "b loop\n"
                     "end:\n"
                     "pushloc.v -7.sum\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.r, 55);
}

AR_TEST(test_vm_joins_strings_and_compares_them) {
    Program p = {ar_asm_new()};
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "push.s \"Kris \"\n"
                           "conv.s.v\n"
                           "push.s \"and Susie\"\n"
                           "conv.s.v\n"
                           "add.v.v\n"
                           "pop.v.v -7.s\n"
                           "pushloc.v -7.s\n"
                           "call t_log 1\n"
                           "popz.v\n"
                           "pushloc.v -7.s\n"
                           "push.s \"Kris and Susie\"\n"
                           "cmp.s.v eq\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, 0, NULL);
    AR_EQUAL_INT(r.type, AR_BOOL);
    AR_EQUAL_INT(r.r, 1);
    AR_EQUAL_INT(gLogCount, 1);
    AR_EQUAL(@(gLogged[0].s->text), @"Kris and Susie");
    unlink_program(&p);
}

AR_TEST(test_vm_calls_a_script_and_recurses) {
    Program p = {ar_asm_new()};
    // fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 2)
    int fib = ar_asm_code(p.a, "gml_Script_fib", 1,
                          "push.v -15.argument0\n"
                          "pushi 2\n"
                          "cmp.i.v lt\n"
                          "bf more\n"
                          "push.v -15.argument0\n"
                          "ret.v\n"
                          "more:\n"
                          "push.v -15.argument0\n"
                          "pushi 1\n"
                          "sub.i.v\n"
                          "call gml_Script_fib 1\n"
                          "push.v -15.argument0\n"
                          "pushi 2\n"
                          "sub.i.v\n"
                          "call gml_Script_fib 1\n"
                          "add.v.v\n"
                          "ret.v\n");
    AR_EXPECT(fib >= 0);
    AR_EXPECT(link_program(&p));
    ARValue n = ar_real(15);
    ARValue r = ar_vm_run(p.vm, fib, NULL, NULL, 1, &n);
    AR_EQUAL_INT(r.r, 610);
    unlink_program(&p);
}

AR_TEST(test_vm_reads_and_writes_global_and_instance_variables) {
    Program p = {ar_asm_new()};
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 3\n"
                           "pop.v.i -5.lives\n"
                           "pushi 20\n"
                           "pop.v.i -1.hp\n"
                           "pushglb.v -5.lives\n"
                           "push.v -1.hp\n"
                           "mul.v.v\n"
                           "ret.v\n");
    AR_EXPECT(link_program(&p));
    ARInstance *kris = ar_vm_instance_new(p.vm, -1, 0);
    ARValue r = ar_vm_run(p.vm, code, kris->vars, kris->vars, 0, NULL);
    AR_EQUAL_INT(r.r, 60);
    AR_EQUAL_INT(ar_vars_find(&kris->vars->vars, ar_vm_slot(p.vm, "hp"))->r, 20);
    AR_EQUAL_INT(ar_vars_find(&ar_vm_global(p.vm)->vars, ar_vm_slot(p.vm, "lives"))->r, 3);
    unlink_program(&p);
}

AR_TEST(test_vm_uses_the_builtin_variables) {
    Program p = {ar_asm_new()};
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 12\n"
                           "pop.v.i -1.t_x\n"
                           "pushi -1\n"       // t_alarm[2] = 30
                           "pushi 2\n"
                           "pushi 30\n"
                           "pop.i.v -1.t_alarm array\n"
                           "push.v -1.t_room\n"
                           "pushi 1\n"
                           "add.i.v\n"
                           "pop.v.v -1.t_room\n"
                           "push.v -1.t_x\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    gRoom = 7;
    ARInstance *kris = ar_vm_instance_new(p.vm, -1, 0);
    ARValue r = ar_vm_run(p.vm, code, kris->vars, kris->vars, 0, NULL);
    AR_EQUAL_INT(r.r, 12);
    AR_EQUAL_INT(kris->x, 12);
    AR_EQUAL_INT(kris->alarm[2], 30);
    AR_EQUAL_INT(gRoom, 8);
    unlink_program(&p);
}

AR_TEST(test_vm_writes_an_array_with_the_value_under_or_over_the_index) {
    Program p = {ar_asm_new()};
    // global.a[3] = 5, with the value under the instance and the index.
    // Then global.a[3]++, as the compiler gives it, with a dup swap and
    // the value on the top.
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 5\n"
                           "pushi -5\n"
                           "pushi 3\n"
                           "pop.v.i 0.a array\n"
                           "pushi -5\n"
                           "pushi 3\n"
                           "dup.i 1\n"
                           "push.v 0.a array\n"
                           "dup.v 0\n"
                           "dup.i 4 swap176\n"
                           "push.e 1\n"
                           "add.i.v\n"
                           "pop.i.v 0.a array\n"
                           "pop.v.v -7.old\n"
                           "pushi -5\n"
                           "pushi 3\n"
                           "push.v 0.a array\n"
                           "pushloc.v -7.old\n"
                           "mul.v.v\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, 0, NULL);
    AR_EQUAL_INT(r.r, 30);  // a[3] is 6, and the old value is 5.
    unlink_program(&p);
}

AR_TEST(test_vm_reads_and_writes_two_indices) {
    Program p = {ar_asm_new()};
    // grid[1][2] = 9; return grid[1][2] + array_length(grid)
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 9\n"
                           "pushi -1\n"
                           "pushi 1\n"
                           "push.v 0.grid popaf\n"
                           "pushi 2\n"
                           "break -3\n"
                           "pushi -1\n"
                           "pushi 1\n"
                           "push.v 0.grid pushaf\n"
                           "pushi 2\n"
                           "break -2\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARInstance *kris = ar_vm_instance_new(p.vm, -1, 0);
    ARValue r = ar_vm_run(p.vm, code, kris->vars, kris->vars, 0, NULL);
    AR_EQUAL_INT(r.r, 9);
    ARValue *grid = ar_vars_find(&kris->vars->vars, ar_vm_slot(p.vm, "grid"));
    AR_EXPECT(grid && grid->type == AR_ARRAY);
    AR_EQUAL_INT(grid->a->length, 2);
    unlink_program(&p);
}

AR_TEST(test_vm_runs_with_over_the_instances_of_an_object_and_its_children) {
    Program p = {ar_asm_new()};
    int enemy = ar_asm_object(p.a, "obj_enemy", -1);
    int boss = ar_asm_object(p.a, "obj_boss", enemy);
    (void)boss;
    // with (obj_enemy) { hp = other.power; t_log(id) }
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 0\n"
                           "pushenv end\n"
                           "body:\n"
                           "push.v -2.power\n"
                           "pop.v.v -1.hp\n"
                           "pushi 0\n"
                           "call t_args 1\n"
                           "call t_log 1\n"
                           "popz.v\n"
                           "end:\n"
                           "popenv body\n"
                           "pushi 1\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARInstance *player = ar_vm_instance_new(p.vm, -1, 0);
    *ar_vars_slot(&player->vars->vars, ar_vm_slot(p.vm, "power")) = ar_real(4);
    ARInstance *a = ar_vm_instance_new(p.vm, 0, 0);
    ARInstance *b = ar_vm_instance_new(p.vm, 1, 0);
    ar_vm_run(p.vm, code, player->vars, player->vars, 0, NULL);
    int hp = ar_vm_slot(p.vm, "hp");
    AR_EQUAL_INT(ar_vars_find(&a->vars->vars, hp)->r, 4);
    AR_EQUAL_INT(ar_vars_find(&b->vars->vars, hp)->r, 4);
    AR_EXPECT(ar_vars_find(&player->vars->vars, hp) == NULL);
    AR_EQUAL_INT(gLogCount, 2);
    AR_EQUAL_INT(gLogged[0].r, 1001);  // one argument, instance 1
    AR_EQUAL_INT(gLogged[1].r, 1002);
    unlink_program(&p);
}

AR_TEST(test_vm_skips_a_with_that_has_no_instance) {
    ARValue r = run1("pushi 7\n"
                     "pop.v.i -7.n\n"
                     "pushi -4\n"
                     "pushenv end\n"
                     "body:\n"
                     "pushi 99\n"
                     "pop.v.i -7.n\n"
                     "end:\n"
                     "popenv body\n"
                     "pushloc.v -7.n\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.r, 7);
}

AR_TEST(test_vm_leaves_a_with_with_a_drop) {
    Program p = {ar_asm_new()};
    ar_asm_object(p.a, "obj_a", -1);
    // with (obj_a) { count += 1; break }
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 0\n"
                           "pop.v.i -5.count\n"
                           "pushi 0\n"
                           "pushenv end\n"
                           "body:\n"
                           "pushglb.v -5.count\n"
                           "pushi 1\n"
                           "add.i.v\n"
                           "pop.v.v -5.count\n"
                           "popenv drop\n"
                           "b after\n"
                           "end:\n"
                           "popenv body\n"
                           "after:\n"
                           "pushglb.v -5.count\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ar_vm_instance_new(p.vm, 0, 0);
    ar_vm_instance_new(p.vm, 0, 0);
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, 0, NULL);
    AR_EQUAL_INT(r.r, 1);
    unlink_program(&p);
}

AR_TEST(test_vm_global_script_makes_a_function_that_callv_calls) {
    Program p = {ar_asm_new()};
    // function double_it(n) { return n * 2 }, as a global script makes it.
    int global = ar_asm_code(p.a, "gml_GlobalScript_double_it", 0,
                             "b define\n"
                             "body:\n"
                             "push.v -15.argument0\n"
                             "pushi 2\n"
                             "mul.i.v\n"
                             "ret.v\n"
                             "define:\n"
                             "push.i func:gml_Script_double_it\n"
                             "conv.i.v\n"
                             "pushi -1\n"
                             "conv.i.v\n"
                             "call method 2\n"
                             "dup.v 0\n"
                             "pushi -1\n"
                             "pop.v.v 0.double_it stacktop\n"
                             "popz.v\n");
    AR_EXPECT(global >= 0);
    int child = ar_asm_child(p.a, "gml_Script_double_it", 1, "body");
    AR_EXPECT(child >= 0);
    ar_asm_global_script(p.a, global);
    // double_it(21) through the global variable, with callv.
    int caller = ar_asm_code(p.a, "gml_Object_caller", 0,
                             "pushi 21\n"
                             "call @@This@@ 0\n"
                             "pushglb.v -5.double_it\n"
                             "callv 1\n"
                             "ret.v\n");
    AR_EXPECT(caller >= 0);
    AR_EXPECT(link_program(&p));
    ar_vm_run_global_scripts(p.vm);
    ARValue *f = ar_vars_find(&ar_vm_global(p.vm)->vars, ar_vm_slot(p.vm, "double_it"));
    AR_EXPECT(f && f->type == AR_METHOD);
    AR_EQUAL_INT(f ? f->m->self.type : -1, AR_UNDEFINED);
    ARInstance *kris = ar_vm_instance_new(p.vm, -1, 0);
    ARValue r = ar_vm_run(p.vm, caller, kris->vars, kris->vars, 0, NULL);
    AR_EQUAL_INT(r.r, 42);
    unlink_program(&p);
}

AR_TEST(test_vm_new_runs_the_constructor_with_the_struct_as_self) {
    Program p = {ar_asm_new()};
    int ctor = ar_asm_code(p.a, "gml_Script_Point", 2,
                           "push.v -15.argument0\n"
                           "pop.v.v -1.px\n"
                           "push.v -15.argument1\n"
                           "pop.v.v -1.py\n"
                           "exit.i\n");
    AR_EXPECT(ctor >= 0);
    // var p = new Point(3, 4); return p.px * 10 + p.py
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "pushi 4\n"
                           "conv.i.v\n"
                           "pushi 3\n"
                           "conv.i.v\n"
                           "push.i func:gml_Script_Point\n"
                           "conv.i.v\n"
                           "call @@NewGMLObject@@ 3\n"
                           "pop.v.v -7.pt\n"
                           "pushloc.v -7.pt\n"
                           "pushi -9\n"
                           "push.v -9.px\n"
                           "pushi 10\n"
                           "mul.i.v\n"
                           "pushloc.v -7.pt\n"
                           "push.v -9.py\n"
                           "add.v.v\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, 0, NULL);
    AR_EQUAL_INT(r.r, 34);
    unlink_program(&p);
}

AR_TEST(test_vm_writes_through_the_stacktop_marker) {
    Program p = {ar_asm_new()};
    // other_inst.score = -1000, as "pushi -9" marks the instance below it.
    int code = ar_asm_code(p.a, "gml_Script_test", 1,
                           "pushi -1000\n"
                           "push.v -15.argument0\n"
                           "pushi -9\n"
                           "pop.v.i 0.score stacktop\n"
                           "exit.i\n");
    AR_EXPECT(link_program(&p));
    ARInstance *target = ar_vm_instance_new(p.vm, -1, 0);
    ARValue arg = ar_real(target->id);
    ar_vm_run(p.vm, code, NULL, NULL, 1, &arg);
    AR_EQUAL_INT(ar_vars_find(&target->vars->vars, ar_vm_slot(p.vm, "score"))->r, -1000);
    unlink_program(&p);
}

AR_TEST(test_vm_moves_the_instance_over_the_arguments_with_a_dup_swap) {
    Program p = {ar_asm_new()};
    // inst.f(a, b): the compiler pushes inst, then b and a, then moves inst
    // to the top with "dup.v 2 swap136".
    int f = ar_asm_code(p.a, "gml_Script_f", 2,
                        "push.v -15.argument0\n"
                        "pushi 10\n"
                        "mul.i.v\n"
                        "push.v -15.argument1\n"
                        "add.v.v\n"
                        "push.v -1.base\n"
                        "add.v.v\n"
                        "ret.v\n");
    AR_EXPECT(f >= 0);
    int code = ar_asm_code(p.a, "gml_Script_test", 1,
                           "push.v -15.argument0\n"
                           "pushi 2\n"     // b
                           "conv.i.v\n"
                           "pushi 1\n"     // a
                           "conv.i.v\n"
                           "dup.v 2 swap136\n"
                           "dup.v 0\n"
                           "push.v -9.f\n"
                           "callv 2\n"
                           "ret.v\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARInstance *target = ar_vm_instance_new(p.vm, -1, 0);
    *ar_vars_slot(&target->vars->vars, ar_vm_slot(p.vm, "base")) = ar_real(100);
    ARValue method;
    method.type = AR_METHOD;
    method.m = ar_method_new(ar_vm_heap(p.vm), ar_vm_script(p.vm, "gml_Script_f"), ar_undefined());
    *ar_vars_slot(&target->vars->vars, ar_vm_slot(p.vm, "f")) = method;
    ARValue arg = ar_real(target->id);
    ARValue r = ar_vm_run(p.vm, code, NULL, NULL, 1, &arg);
    AR_EQUAL_INT(r.r, 112);
    unlink_program(&p);
}

AR_TEST(test_vm_gives_undefined_for_an_unknown_function) {
    ARValue r = run1("pushi 1\n"
                     "call no_such_function 1\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.type, AR_UNDEFINED);
}

AR_TEST(test_vm_keeps_64_bit_integers) {
    ARValue r = run1("push.l 4000000000\n"
                     "push.l 3\n"
                     "mul.l.l\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.type, AR_INT64);
    AR_EQUAL_INT(r.l, 12000000000LL);
}

AR_TEST(test_vm_truncates_a_conversion_to_int) {
    ARValue r = run1("push.d 2.75\n"
                     "conv.d.i\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.r * 100, 200);
}

AR_TEST(test_vm_survives_an_empty_stack) {
    ARValue r = run1("popz.v\n"
                     "pushi 5\n"
                     "ret.v\n",
                     0, NULL);
    AR_EQUAL_INT(r.type, AR_UNDEFINED);
}

AR_TEST(test_vm_collector_keeps_the_variables_of_instances) {
    Program p = {ar_asm_new()};
    int code = ar_asm_code(p.a, "gml_Script_test", 0,
                           "push.s \"kept\"\n"
                           "push.s \" text\"\n"
                           "add.s.s\n"
                           "pop.v.v -1.name\n"
                           "pushi 1\n"
                           "pushi 2\n"
                           "call @@NewGMLArray@@ 2\n"
                           "popz.v\n"
                           "exit.i\n");
    AR_EXPECT(code >= 0);
    AR_EXPECT(link_program(&p));
    ARInstance *kris = ar_vm_instance_new(p.vm, -1, 0);
    ar_vm_run(p.vm, code, kris->vars, kris->vars, 0, NULL);
    ar_vm_collect(p.vm, NULL, NULL);
    ar_vm_collect(p.vm, NULL, NULL);
    AR_EQUAL(@(ar_vars_find(&kris->vars->vars, ar_vm_slot(p.vm, "name"))->s->text), @"kept text");
    unlink_program(&p);
}
