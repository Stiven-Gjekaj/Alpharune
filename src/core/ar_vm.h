// ar_vm: runs the GML bytecode of a data file (bytecode version 17).
//
// ar_vm_new links the program: it decodes each code blob into a compact
// form, sets the variable and function of each reference, and makes the
// constant strings. The interpreter then runs a code entry with a self
// and an other.
//
// The runtime (ar_run) adds rooms, events, and drawing. The VM keeps the
// instances, because the bytecode reads and writes their variables.

#ifndef AR_VM_H
#define AR_VM_H

#include "ar_data.h"
#include "ar_value.h"

typedef struct ARVM ARVM;
typedef struct ARInstance ARInstance;

// The special instance values of GML.
enum {
    AR_SELF = -1,
    AR_OTHER = -2,
    AR_ALL = -3,
    AR_NOONE = -4,
    AR_GLOBAL = -5,
    AR_BUILTIN = -6,
    AR_LOCAL = -7,
    AR_STACKTOP = -9,
    AR_ARGUMENT = -15,
    AR_STATIC = -16
};

// The first id of an instance. A smaller number is an object index.
#define AR_FIRST_INSTANCE_ID 100000

struct ARInstance {
    ARStruct *vars;  // The variables. vars->instance points back here.
    int32_t id;
    int32_t object;
    int32_t order;   // The order of creation, for "with" and the events.
    double x, y, xprevious, yprevious, xstart, ystart;
    double hspeed, vspeed, speed, direction;
    double friction, gravity, gravity_direction;
    int32_t sprite_index, mask_index;
    double image_index, image_speed, image_xscale, image_yscale, image_angle, image_alpha;
    int32_t image_blend;
    double depth;
    int32_t layer;
    uint8_t visible, solid, persistent, active;
    uint8_t destroyed;   // instance_destroy ran. The runtime removes it later.
    uint8_t created;     // The create event ran.
    uint8_t pad[2];
    int32_t alarm[12];
    // The runtime keeps its own data here.
    void *user;
};

// A function of the runtime. args[0] is the first argument.
typedef ARValue (*ARBuiltinFn)(ARVM *vm, int argc, ARValue *args);

// A variable of the runtime. index is -1 when the code does not give an
// array index. self is NULL when the self is not an instance.
typedef ARValue (*ARVarGet)(ARVM *vm, ARInstance *self, int index);
typedef void (*ARVarSet)(ARVM *vm, ARInstance *self, int index, ARValue value);

typedef struct {
    const char *name;
    ARBuiltinFn fn;
} ARBuiltin;

typedef struct {
    const char *name;
    ARVarGet get;
    ARVarSet set;  // NULL for a variable that the code cannot set.
    int instance;  // 1 for a variable of an instance, 0 for a global one.
} ARBuiltinVar;

// Adds functions and variables of the runtime. Call before ar_vm_new.
// The tables must stay in memory.
void ar_vm_register(const ARBuiltin *functions, int function_count, const ARBuiltinVar *variables,
                    int variable_count);

ARVM *ar_vm_new(const ARData *data);
void ar_vm_free(ARVM *vm);
const char *ar_vm_error(const ARVM *vm);

ARHeap *ar_vm_heap(ARVM *vm);
const ARData *ar_vm_data(ARVM *vm);
ARStruct *ar_vm_global(ARVM *vm);
// The self and the other of the code that runs now, or of the last code.
ARStruct *ar_vm_self(ARVM *vm);
ARStruct *ar_vm_other(ARVM *vm);
ARInstance *ar_vm_self_instance(ARVM *vm);
ARInstance *ar_vm_other_instance(ARVM *vm);
// The count of the arguments and the arguments of the current script.
int ar_vm_argument_count(ARVM *vm);
ARValue ar_vm_argument(ARVM *vm, int index);

// Runs a code entry. Returns the value of its "return", or undefined.
ARValue ar_vm_run(ARVM *vm, int32_t code, ARStruct *self, ARStruct *other, int argc, ARValue *args);
// Calls a function value: a method, a script index, or a builtin id.
ARValue ar_vm_call(ARVM *vm, ARValue function, ARStruct *self, ARStruct *other, int argc, ARValue *args);
// Calls a script by its index in the SCPT chunk.
ARValue ar_vm_call_script(ARVM *vm, int32_t script, ARStruct *self, ARStruct *other, int argc, ARValue *args);
// Runs each global script, in the order of the GLOB chunk.
void ar_vm_run_global_scripts(ARVM *vm);

// The slot of a variable name, for ar_vars_find and ar_vars_slot. Adds
// the name when it is new.
int32_t ar_vm_slot(ARVM *vm, const char *name);
const char *ar_vm_slot_name(ARVM *vm, int32_t slot);
// The id of a builtin function by name, or -1.
int32_t ar_vm_builtin(ARVM *vm, const char *name);
// The index of a script by name ("scr_x" or "gml_Script_scr_x"), or -1.
int32_t ar_vm_script(ARVM *vm, const char *name);
const char *ar_vm_function_name(ARVM *vm, int32_t function);

// The instances, in the order of creation.
ARInstance *ar_vm_instance_new(ARVM *vm, int32_t object, int32_t id);
// Takes out each destroyed instance and frees it.
void ar_vm_remove_destroyed(ARVM *vm);
ARInstance *ar_vm_find_instance(ARVM *vm, int32_t id);
int ar_vm_instance_count(ARVM *vm);
ARInstance *ar_vm_instance_at(ARVM *vm, int index);
// 1 when the object is the object or one of its parents.
int ar_vm_is_object(ARVM *vm, int32_t object, int32_t ancestor);
// The next id of a new instance.
int32_t ar_vm_next_id(ARVM *vm);
// The instances that a value names: an instance id, an object index, all,
// self, or other. Fills at most max and returns the count.
int ar_vm_targets(ARVM *vm, ARValue target, ARStruct **out, int max);

// Marks the roots of the VM for the collector, then sweeps. The runtime
// calls this between two frames, when no code runs.
void ar_vm_collect(ARVM *vm, void (*mark_more)(ARVM *vm, void *context), void *context);

// A message for the log of the runtime.
void ar_vm_log(ARVM *vm, const char *format, ...) __attribute__((format(printf, 2, 3)));
// The name of the code that runs now, for messages.
const char *ar_vm_current_code(ARVM *vm);

// The runtime can keep one pointer in the VM.
void ar_vm_set_user(ARVM *vm, void *user);
void *ar_vm_user(ARVM *vm);

#endif
