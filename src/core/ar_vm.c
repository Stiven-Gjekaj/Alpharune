#include "ar_vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The opcodes of bytecode version 17.
enum {
    OP_CONV = 0x07,
    OP_MUL = 0x08,
    OP_DIV = 0x09,
    OP_REM = 0x0A,
    OP_MOD = 0x0B,
    OP_ADD = 0x0C,
    OP_SUB = 0x0D,
    OP_AND = 0x0E,
    OP_OR = 0x0F,
    OP_XOR = 0x10,
    OP_NEG = 0x11,
    OP_NOT = 0x12,
    OP_SHL = 0x13,
    OP_SHR = 0x14,
    OP_CMP = 0x15,
    OP_POP = 0x45,
    OP_PUSHI = 0x84,
    OP_DUP = 0x86,
    OP_CALLV = 0x99,
    OP_RET = 0x9C,
    OP_EXIT = 0x9D,
    OP_POPZ = 0x9E,
    OP_B = 0xB6,
    OP_BT = 0xB7,
    OP_BF = 0xB8,
    OP_PUSHENV = 0xBA,
    OP_POPENV = 0xBB,
    OP_PUSH = 0xC0,
    OP_PUSHLOC = 0xC1,
    OP_PUSHGLB = 0xC2,
    OP_PUSHBLTN = 0xC3,
    OP_CALL = 0xD9,
    OP_BREAK = 0xFF
};

// The data types of an instruction.
enum { T_DOUBLE = 0, T_FLOAT, T_INT, T_LONG, T_BOOL, T_VAR, T_STRING, T_INSTANCE, T_DELETE, T_UNDEFINED,
       T_UINT, T_SHORT = 0xF };

// The kinds of a variable reference.
enum { VT_ARRAY = 0x00, VT_PUSHAF = 0x10, VT_STACKTOP = 0x80, VT_POPAF = 0x90, VT_NORMAL = 0xA0,
       VT_INSTANCE = 0xE0 };

// The decoded instructions. They have their own codes, so that the switch
// of the interpreter is dense.
enum {
    I_CONV,
    I_ARITH,
    I_NEG,
    I_NOT,
    I_CMP,
    I_POP,
    I_POP_SWAP,
    I_PUSH_CONST,
    I_PUSH_VAR,
    I_DUP,
    I_DUP_SWAP,
    I_RET,
    I_EXIT,
    I_POPZ,
    I_B,
    I_BT,
    I_BF,
    I_PUSHENV,
    I_POPENV,
    I_POPENV_DROP,
    I_CALL,
    I_CALLV,
    I_BREAK,
    I_END
};

typedef struct {
    uint8_t op;      // I_...
    uint8_t t1, t2;  // The data types.
    uint8_t sub;     // The GML opcode of I_ARITH, the compare kind, the
                     // variable kind, or the swap byte of a dup.
    int16_t inst;    // The instance of a variable.
    uint16_t extra;  // The argument count, or the count of a dup.
    int32_t arg;     // A constant, a variable, a function, or a target.
} Insn;

typedef struct {
    const uint8_t *start;  // The first byte of the blob in the file.
    uint32_t length;
    Insn *insns;
    int32_t count;
    int32_t *at;  // The instruction at each word of the blob, or -1.
} Blob;

// What a VARI entry names.
enum { V_PLAIN, V_INSTANCE_BUILTIN, V_GLOBAL_BUILTIN, V_ARGUMENT, V_ARGUMENTS, V_ARGUMENT_COUNT };

typedef struct {
    int32_t slot;
    uint8_t kind;
    int16_t builtin;   // An index into the builtin variables.
    int16_t argument;  // The index of argumentN.
} VarRef;

#define AR_UNKNOWN_BASE 2000000
#define STACK_SIZE 65536
#define MAX_DEPTH 400

typedef struct {
    int32_t key;
    ARValue value;
} Local;

typedef struct {
    ARStruct **targets;
    int count, index;
    ARStruct *self, *other;
} Env;

typedef struct Frame {
    int32_t code;
    ARStruct *self, *other;
    ARValue *args;
    int argc;
    Local *locals;
    int local_count, local_capacity;
    Local local_store[16];
    Env *envs;
    int env_count, env_capacity;
    int32_t pc;
    struct Frame *caller;
} Frame;

struct ARVM {
    const ARData *data;
    ARHeap *heap;
    ARStruct *global;
    char error[200];

    ARStr **strings;
    ARValue *consts;
    int32_t const_count, const_capacity;
    Blob *blobs;
    int32_t blob_count;
    int32_t *code_blob;   // The blob of each code entry.
    int32_t *code_start;  // The first instruction of each code entry.
    VarRef *vars;
    int32_t *function_ids;  // The function id of each FUNC entry.

    // The names of the variable slots.
    const char **slot_names;
    int32_t slot_count, slot_capacity;
    int32_t *slot_table;  // An open hash of slot numbers, by name.
    int32_t slot_table_capacity;

    ARValue *stack;
    uint8_t *sizes;
    int32_t sp;
    Frame *frame;
    int depth;

    ARInstance **instances;
    int32_t instance_count, instance_capacity;
    int32_t next_id;
    int32_t next_order;
    ARInstance **id_table;  // An open hash by id.
    int32_t id_table_capacity, id_table_used;
    // The parent of each object.
    const int32_t *parents;

    uint8_t *logged;  // One flag for each function, so that each message shows once.
    int log_count;
    int quiet;        // Above 0, fault() writes nothing.
    void *user;
};

static const ARBuiltin **gFunctionTables;
static int *gFunctionCounts;
static int gFunctionTableCount;
static const ARBuiltinVar **gVarTables;
static int *gVarCounts;
static int gVarTableCount;

// The flat lists of the registered builtins.
static const ARBuiltin **gFunctions;
static int gFunctionCount;
static const ARBuiltinVar **gVariables;
static int gVariableCount;

static void rebuild_builtins(void) {
    free(gFunctions);
    free(gVariables);
    gFunctionCount = 0;
    gVariableCount = 0;
    for (int t = 0; t < gFunctionTableCount; t++) gFunctionCount += gFunctionCounts[t];
    for (int t = 0; t < gVarTableCount; t++) gVariableCount += gVarCounts[t];
    gFunctions = malloc(sizeof(ARBuiltin *) * (size_t)(gFunctionCount + 1));
    gVariables = malloc(sizeof(ARBuiltinVar *) * (size_t)(gVariableCount + 1));
    int n = 0;
    for (int t = 0; t < gFunctionTableCount; t++) {
        for (int i = 0; i < gFunctionCounts[t]; i++) gFunctions[n++] = &gFunctionTables[t][i];
    }
    n = 0;
    for (int t = 0; t < gVarTableCount; t++) {
        for (int i = 0; i < gVarCounts[t]; i++) gVariables[n++] = &gVarTables[t][i];
    }
}

void ar_vm_register(const ARBuiltin *functions, int function_count, const ARBuiltinVar *variables,
                    int variable_count) {
    // A table that is already there is not added again.
    int known = 0;
    for (int t = 0; t < gFunctionTableCount; t++) {
        if (gFunctionTables[t] == functions) known = 1;
    }
    for (int t = 0; t < gVarTableCount; t++) {
        if (gVarTables[t] == variables && variables) known = 1;
    }
    if (known) return;
    if (functions && function_count > 0) {
        gFunctionTables = realloc(gFunctionTables, sizeof(*gFunctionTables) * (size_t)(gFunctionTableCount + 1));
        gFunctionCounts = realloc(gFunctionCounts, sizeof(int) * (size_t)(gFunctionTableCount + 1));
        gFunctionTables[gFunctionTableCount] = functions;
        gFunctionCounts[gFunctionTableCount] = function_count;
        gFunctionTableCount++;
    }
    if (variables && variable_count > 0) {
        gVarTables = realloc(gVarTables, sizeof(*gVarTables) * (size_t)(gVarTableCount + 1));
        gVarCounts = realloc(gVarCounts, sizeof(int) * (size_t)(gVarTableCount + 1));
        gVarTables[gVarTableCount] = variables;
        gVarCounts[gVarTableCount] = variable_count;
        gVarTableCount++;
    }
    rebuild_builtins();
}

// ---------------------------------------------------------------- messages

void ar_vm_log(ARVM *vm, const char *format, ...) {
    (void)vm;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "gml: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

const char *ar_vm_current_code(ARVM *vm) {
    if (!vm->frame || vm->frame->code < 0) return "(none)";
    return vm->data->codes[vm->frame->code].name;
}

static void fault(ARVM *vm, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void fault(ARVM *vm, const char *format, ...) {
    if (vm->log_count > 200 || vm->quiet) return;
    vm->log_count++;
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    fprintf(stderr, "gml: %s (in %s)\n", text, ar_vm_current_code(vm));
}

const char *ar_vm_error(const ARVM *vm) {
    return vm->error;
}

// ------------------------------------------------------------------ slots

static uint32_t hash_name(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static void slot_table_grow(ARVM *vm) {
    int32_t capacity = vm->slot_table_capacity ? vm->slot_table_capacity * 2 : 4096;
    int32_t *table = malloc(sizeof(int32_t) * (size_t)capacity);
    memset(table, 0xFF, sizeof(int32_t) * (size_t)capacity);
    for (int32_t s = 0; s < vm->slot_count; s++) {
        uint32_t i = hash_name(vm->slot_names[s]) & (uint32_t)(capacity - 1);
        while (table[i] >= 0) i = (i + 1) & (uint32_t)(capacity - 1);
        table[i] = s;
    }
    free(vm->slot_table);
    vm->slot_table = table;
    vm->slot_table_capacity = capacity;
}

int32_t ar_vm_slot(ARVM *vm, const char *name) {
    if (!name) name = "";
    if ((vm->slot_count + 1) * 2 > vm->slot_table_capacity) slot_table_grow(vm);
    uint32_t mask = (uint32_t)vm->slot_table_capacity - 1;
    uint32_t i = hash_name(name) & mask;
    while (vm->slot_table[i] >= 0) {
        if (strcmp(vm->slot_names[vm->slot_table[i]], name) == 0) return vm->slot_table[i];
        i = (i + 1) & mask;
    }
    if (vm->slot_count == vm->slot_capacity) {
        vm->slot_capacity = vm->slot_capacity ? vm->slot_capacity * 2 : 4096;
        vm->slot_names = realloc(vm->slot_names, sizeof(char *) * (size_t)vm->slot_capacity);
    }
    // A name that the data does not hold gets its own copy.
    const char *copy = name;
    if (!(name >= (const char *)vm->data->bytes && name < (const char *)vm->data->bytes + vm->data->size)) {
        copy = strdup(name);
    }
    vm->slot_names[vm->slot_count] = copy;
    vm->slot_table[i] = vm->slot_count;
    return vm->slot_count++;
}

const char *ar_vm_slot_name(ARVM *vm, int32_t slot) {
    if (slot < 0 || slot >= vm->slot_count) return "?";
    return vm->slot_names[slot];
}

int32_t ar_vm_builtin(ARVM *vm, const char *name) {
    (void)vm;
    for (int i = 0; i < gFunctionCount; i++) {
        if (strcmp(gFunctions[i]->name, name) == 0) return i;
    }
    return -1;
}

static int builtin_var(const char *name) {
    for (int i = 0; i < gVariableCount; i++) {
        if (strcmp(gVariables[i]->name, name) == 0) return i;
    }
    return -1;
}

int32_t ar_vm_script(ARVM *vm, const char *name) {
    const ARData *d = vm->data;
    for (int32_t i = 0; i < d->script_count; i++) {
        if (d->scripts[i].name && strcmp(d->scripts[i].name, name) == 0) return i;
    }
    if (strncmp(name, "gml_Script_", 11) != 0) {
        char full[256];
        snprintf(full, sizeof(full), "gml_Script_%s", name);
        for (int32_t i = 0; i < d->script_count; i++) {
            if (d->scripts[i].name && strcmp(d->scripts[i].name, full) == 0) return i;
        }
    }
    return -1;
}

const char *ar_vm_function_name(ARVM *vm, int32_t function) {
    if (function >= AR_UNKNOWN_BASE) {
        int32_t k = function - AR_UNKNOWN_BASE;
        return k < vm->data->function_count ? vm->data->functions[k].name : "?";
    }
    if (function >= AR_BUILTIN_BASE) {
        int32_t b = function - AR_BUILTIN_BASE;
        return b < gFunctionCount ? gFunctions[b]->name : "?";
    }
    if (function >= 0 && function < vm->data->script_count) return vm->data->scripts[function].name;
    return "?";
}

// ------------------------------------------------------------------- link

typedef struct {
    uint32_t address;
    int32_t value;  // A VARI index, or (1 << 30) | a FUNC index.
} Ref;

static int compare_ref(const void *a, const void *b) {
    uint32_t x = ((const Ref *)a)->address, y = ((const Ref *)b)->address;
    return x < y ? -1 : x > y;
}

static int32_t find_ref(const Ref *refs, int32_t count, uint32_t address) {
    int32_t lo = 0, hi = count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        if (refs[mid].address == address) return refs[mid].value;
        if (refs[mid].address < address) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Follows the chain of the occurrences of each variable and function.
static Ref *collect_refs(const ARData *d, int32_t *count) {
    size_t total = 0;
    for (int32_t i = 0; i < d->variable_count; i++) total += d->variables[i].occurrences;
    for (int32_t i = 0; i < d->function_count; i++) total += d->functions[i].occurrences;
    Ref *refs = malloc(sizeof(Ref) * (total + 1));
    int32_t n = 0;
    for (int kind = 0; kind < 2; kind++) {
        int32_t entries = kind ? d->function_count : d->variable_count;
        for (int32_t i = 0; i < entries; i++) {
            uint32_t address = kind ? d->functions[i].first : d->variables[i].first;
            uint32_t occurrences = kind ? d->functions[i].occurrences : d->variables[i].occurrences;
            for (uint32_t k = 0; k < occurrences; k++) {
                if ((size_t)address + 8 > d->size) break;
                refs[n].address = address;
                refs[n].value = kind ? (1 << 30) | i : i;
                n++;
                address += word(d->bytes + address + 4) & 0x07FFFFFF;
            }
        }
    }
    qsort(refs, (size_t)n, sizeof(Ref), compare_ref);
    *count = n;
    return refs;
}

static int32_t add_const(ARVM *vm, ARValue v) {
    if (vm->const_count == vm->const_capacity) {
        vm->const_capacity = vm->const_capacity ? vm->const_capacity * 2 : 1024;
        vm->consts = realloc(vm->consts, sizeof(ARValue) * (size_t)vm->const_capacity);
    }
    vm->consts[vm->const_count] = v;
    return vm->const_count++;
}

static int type_size(int t) {
    switch (t) {
    case T_DOUBLE:
    case T_LONG: return 8;
    case T_VAR:
    case T_STRING: return 16;
    default: return 4;
    }
}

static int32_t sign23(uint32_t w) {
    int32_t off = (int32_t)(w & 0x7FFFFF);
    if (off & 0x400000) off -= 0x800000;
    return off;
}

static int decode_blob(ARVM *vm, Blob *b, const Ref *refs, int32_t ref_count) {
    const uint8_t *base = vm->data->bytes;
    uint32_t words = b->length / 4;
    b->at = malloc(sizeof(int32_t) * (words + 1));
    for (uint32_t i = 0; i <= words; i++) b->at[i] = -1;
    // Each instruction is one word or more, so words + 1 is enough.
    b->insns = calloc(words + 2, sizeof(Insn));
    int32_t n = 0;
    uint32_t p = 0;
    // The branch targets are words for now. A second pass maps them.
    while (p + 4 <= b->length) {
        uint32_t w = word(b->start + p);
        uint32_t address = (uint32_t)(b->start + p - base);
        Insn *in = &b->insns[n];
        b->at[p / 4] = n;
        int op = (int)(w >> 24);
        int t1 = (int)((w >> 16) & 0xF), t2 = (int)((w >> 20) & 0xF);
        in->t1 = (uint8_t)t1;
        in->t2 = (uint8_t)t2;
        uint32_t size = 4;
        switch (op) {
        case OP_CONV: in->op = I_CONV; break;
        case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD: case OP_ADD: case OP_SUB:
        case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
            in->op = I_ARITH;
            in->sub = (uint8_t)op;
            break;
        case OP_NEG: in->op = I_NEG; break;
        case OP_NOT: in->op = I_NOT; break;
        case OP_CMP:
            in->op = I_CMP;
            in->sub = (uint8_t)((w >> 8) & 0xFF);
            break;
        case OP_POP:
            if (t1 == T_SHORT) {
                in->op = I_POP_SWAP;
                in->extra = (uint16_t)(w & 0xFFFF);
            } else {
                in->op = I_POP;
                in->inst = (int16_t)(w & 0xFFFF);
                uint32_t r = word(b->start + p + 4);
                in->sub = (uint8_t)((r >> 24) & 0xF8);
                in->arg = find_ref(refs, ref_count, address);
                if (in->arg < 0 || in->arg >= (1 << 30)) {
                    snprintf(vm->error, sizeof(vm->error), "a pop at %u has no variable", address);
                    return 0;
                }
                size = 8;
            }
            break;
        case OP_PUSHI:
            in->op = I_PUSH_CONST;
            in->t1 = T_INT;
            in->arg = add_const(vm, ar_real((int16_t)(w & 0xFFFF)));
            break;
        case OP_DUP:
            in->op = ((w >> 8) & 0xFF) ? I_DUP_SWAP : I_DUP;
            in->extra = (uint16_t)(w & 0xFF);
            in->sub = (uint8_t)((w >> 8) & 0xFF);
            break;
        case OP_RET: in->op = I_RET; break;
        case OP_EXIT: in->op = I_EXIT; break;
        case OP_POPZ: in->op = I_POPZ; break;
        case OP_B: case OP_BT: case OP_BF: case OP_PUSHENV:
            in->op = op == OP_B ? I_B : op == OP_BT ? I_BT : op == OP_BF ? I_BF : I_PUSHENV;
            in->arg = (int32_t)(p / 4) + sign23(w);
            break;
        case OP_POPENV:
            if ((w & 0xFFFFFF) == 0xF00000) {
                in->op = I_POPENV_DROP;
            } else {
                in->op = I_POPENV;
                in->arg = (int32_t)(p / 4) + sign23(w);
            }
            break;
        case OP_PUSH: case OP_PUSHLOC: case OP_PUSHGLB: case OP_PUSHBLTN: {
            if (t1 == T_VAR) {
                in->op = I_PUSH_VAR;
                in->inst = (int16_t)(w & 0xFFFF);
                uint32_t r = word(b->start + p + 4);
                in->sub = (uint8_t)((r >> 24) & 0xF8);
                in->arg = find_ref(refs, ref_count, address);
                if (in->arg < 0 || in->arg >= (1 << 30)) {
                    snprintf(vm->error, sizeof(vm->error), "a push at %u has no variable", address);
                    return 0;
                }
                size = 8;
                break;
            }
            in->op = I_PUSH_CONST;
            ARValue v = ar_undefined();
            if (t1 == T_SHORT) {
                v = ar_real((int16_t)(w & 0xFFFF));
                in->t1 = T_INT;
            } else if (t1 == T_INT || t1 == T_BOOL || t1 == T_FLOAT) {
                uint32_t raw = word(b->start + p + 4);
                int32_t ref = t1 == T_INT ? find_ref(refs, ref_count, address) : -1;
                if (ref >= (1 << 30)) {
                    // A function, as push.i gives it.
                    v = ar_real(vm->function_ids[ref & ((1 << 30) - 1)]);
                } else if (t1 == T_FLOAT) {
                    float f;
                    memcpy(&f, &raw, 4);
                    v = ar_real(f);
                } else if (t1 == T_BOOL) {
                    v = ar_bool(raw != 0);
                } else {
                    v = ar_real((int32_t)raw);
                }
                size = 8;
            } else if (t1 == T_DOUBLE) {
                double d;
                memcpy(&d, b->start + p + 4, 8);
                v = ar_real(d);
                size = 12;
            } else if (t1 == T_LONG) {
                int64_t l;
                memcpy(&l, b->start + p + 4, 8);
                v = ar_int64(l);
                size = 12;
            } else if (t1 == T_STRING) {
                uint32_t index = word(b->start + p + 4);
                if ((int32_t)index < vm->data->string_count) v = ar_strv(vm->strings[index]);
                size = 8;
            } else {
                size = 8;
            }
            in->arg = add_const(vm, v);
            break;
        }
        case OP_CALL: {
            in->op = I_CALL;
            in->extra = (uint16_t)(w & 0xFFFF);
            int32_t ref = find_ref(refs, ref_count, address);
            if (ref < (1 << 30)) {
                snprintf(vm->error, sizeof(vm->error), "a call at %u has no function", address);
                return 0;
            }
            in->arg = vm->function_ids[ref & ((1 << 30) - 1)];
            size = 8;
            break;
        }
        case OP_CALLV:
            in->op = I_CALLV;
            in->extra = (uint16_t)(w & 0xFFFF);
            break;
        case OP_BREAK:
            in->op = I_BREAK;
            in->arg = (int16_t)(w & 0xFFFF);
            if (in->arg == -11) size = 8;
            break;
        default:
            snprintf(vm->error, sizeof(vm->error), "unknown opcode 0x%02x at %u", op, address);
            return 0;
        }
        p += size;
        n++;
    }
    b->insns[n].op = I_END;
    b->at[b->length / 4] = n;
    b->count = n + 1;
    // Map the branch targets from words to instructions.
    for (int32_t i = 0; i < n; i++) {
        Insn *in = &b->insns[i];
        if (in->op == I_B || in->op == I_BT || in->op == I_BF || in->op == I_PUSHENV || in->op == I_POPENV) {
            int32_t target = in->arg;
            if (target < 0 || target > (int32_t)(b->length / 4) || b->at[target] < 0) {
                snprintf(vm->error, sizeof(vm->error), "a branch goes out of its code");
                return 0;
            }
            in->arg = b->at[target];
        }
    }
    return 1;
}

static int resolve_names(ARVM *vm) {
    const ARData *d = vm->data;
    vm->function_ids = malloc(sizeof(int32_t) * (size_t)(d->function_count + 1));
    for (int32_t i = 0; i < d->function_count; i++) {
        const char *name = d->functions[i].name ? d->functions[i].name : "";
        int32_t id = -1;
        if (strncmp(name, "gml_Script_", 11) == 0) {
            id = ar_vm_script(vm, name);
        } else {
            int32_t b = ar_vm_builtin(vm, name);
            if (b >= 0) id = AR_BUILTIN_BASE + b;
            else id = ar_vm_script(vm, name);
        }
        vm->function_ids[i] = id >= 0 ? id : AR_UNKNOWN_BASE + i;
    }
    vm->vars = calloc((size_t)d->variable_count + 1, sizeof(VarRef));
    for (int32_t i = 0; i < d->variable_count; i++) {
        const char *name = d->variables[i].name ? d->variables[i].name : "";
        VarRef *v = &vm->vars[i];
        v->slot = ar_vm_slot(vm, name);
        v->kind = V_PLAIN;
        int b = builtin_var(name);
        if (b >= 0) {
            v->kind = gVariables[b]->instance ? V_INSTANCE_BUILTIN : V_GLOBAL_BUILTIN;
            v->builtin = (int16_t)b;
        } else if (strcmp(name, "argument") == 0) {
            v->kind = V_ARGUMENTS;
        } else if (strcmp(name, "argument_count") == 0) {
            v->kind = V_ARGUMENT_COUNT;
        } else if (strncmp(name, "argument", 8) == 0 && name[8] >= '0' && name[8] <= '9') {
            v->kind = V_ARGUMENT;
            v->argument = (int16_t)atoi(name + 8);
        }
    }
    return 1;
}

ARVM *ar_vm_new(const ARData *data) {
    ARVM *vm = calloc(1, sizeof(ARVM));
    vm->data = data;
    vm->heap = ar_heap_new();
    vm->global = ar_struct_new(vm->heap);
    vm->global->h.fixed = 1;
    vm->stack = malloc(sizeof(ARValue) * STACK_SIZE);
    vm->sizes = malloc(STACK_SIZE);
    vm->next_id = data->last_object >= AR_FIRST_INSTANCE_ID ? data->last_object + 1 : AR_FIRST_INSTANCE_ID;
    vm->logged = calloc((size_t)data->function_count + 1, 1);

    vm->strings = malloc(sizeof(ARStr *) * (size_t)(data->string_count + 1));
    for (int32_t i = 0; i < data->string_count; i++) {
        vm->strings[i] = ar_str_new(vm->heap, data->strings[i].text, data->strings[i].length);
        vm->strings[i]->h.fixed = 1;
    }
    int32_t *parents = malloc(sizeof(int32_t) * (size_t)(data->object_count + 1));
    for (int32_t i = 0; i < data->object_count; i++) parents[i] = data->objects[i].parent;
    vm->parents = parents;

    resolve_names(vm);

    int32_t ref_count;
    Ref *refs = collect_refs(data, &ref_count);
    vm->code_blob = malloc(sizeof(int32_t) * (size_t)(data->code_count + 1));
    vm->code_start = malloc(sizeof(int32_t) * (size_t)(data->code_count + 1));
    vm->blobs = calloc((size_t)data->code_count + 1, sizeof(Blob));
    int ok = 1;
    for (int32_t c = 0; c < data->code_count && ok; c++) {
        const ARCode *code = &data->codes[c];
        int32_t b = -1;
        // A child entry shares the blob of the entry before it.
        for (int32_t k = vm->blob_count - 1; k >= 0 && k >= vm->blob_count - 64; k--) {
            if (vm->blobs[k].start == code->blob) {
                b = k;
                break;
            }
        }
        if (b < 0) {
            b = vm->blob_count++;
            vm->blobs[b].start = code->blob;
            vm->blobs[b].length = code->length;
            ok = decode_blob(vm, &vm->blobs[b], refs, ref_count);
        }
        vm->code_blob[c] = b;
        if (ok) {
            int32_t start = vm->blobs[b].at[code->offset / 4];
            if (start < 0) {
                snprintf(vm->error, sizeof(vm->error), "%s starts in an instruction", code->name);
                ok = 0;
            }
            vm->code_start[c] = start;
        }
    }
    free(refs);
    if (!ok) {
        fprintf(stderr, "ar_vm: %s\n", vm->error);
        ar_vm_free(vm);
        return NULL;
    }
    return vm;
}

void ar_vm_free(ARVM *vm) {
    if (!vm) return;
    for (int32_t i = 0; i < vm->blob_count; i++) {
        free(vm->blobs[i].insns);
        free(vm->blobs[i].at);
    }
    for (int32_t i = 0; i < vm->instance_count; i++) free(vm->instances[i]);
    for (int32_t s = 0; s < vm->slot_count; s++) {
        const char *name = vm->slot_names[s];
        if (!(name >= (const char *)vm->data->bytes && name < (const char *)vm->data->bytes + vm->data->size)) {
            free((void *)name);
        }
    }
    free(vm->blobs);
    free(vm->code_blob);
    free(vm->code_start);
    free(vm->vars);
    free(vm->function_ids);
    free(vm->strings);
    free(vm->consts);
    free(vm->slot_names);
    free(vm->slot_table);
    free(vm->stack);
    free(vm->sizes);
    free(vm->instances);
    free(vm->id_table);
    free((void *)vm->parents);
    free(vm->logged);
    ar_heap_free(vm->heap);
    free(vm);
}

ARHeap *ar_vm_heap(ARVM *vm) {
    return vm->heap;
}

const ARData *ar_vm_data(ARVM *vm) {
    return vm->data;
}

ARStruct *ar_vm_global(ARVM *vm) {
    return vm->global;
}

void ar_vm_set_user(ARVM *vm, void *user) {
    vm->user = user;
}

void *ar_vm_user(ARVM *vm) {
    return vm->user;
}

// -------------------------------------------------------------- instances

static void id_table_put(ARVM *vm, ARInstance *inst);

static void id_table_grow(ARVM *vm) {
    ARInstance **old = vm->id_table;
    int32_t old_capacity = vm->id_table_capacity;
    vm->id_table_capacity = old_capacity ? old_capacity * 2 : 1024;
    vm->id_table = calloc((size_t)vm->id_table_capacity, sizeof(ARInstance *));
    vm->id_table_used = 0;
    for (int32_t i = 0; i < old_capacity; i++) {
        if (old[i] && old[i] != (ARInstance *)1) id_table_put(vm, old[i]);
    }
    free(old);
}

static void id_table_put(ARVM *vm, ARInstance *inst) {
    if ((vm->id_table_used + 1) * 2 > vm->id_table_capacity) id_table_grow(vm);
    uint32_t mask = (uint32_t)vm->id_table_capacity - 1;
    uint32_t i = ((uint32_t)inst->id * 2654435761u) & mask;
    while (vm->id_table[i] && vm->id_table[i] != (ARInstance *)1) i = (i + 1) & mask;
    if (!vm->id_table[i]) vm->id_table_used++;
    vm->id_table[i] = inst;
}

ARInstance *ar_vm_find_instance(ARVM *vm, int32_t id) {
    if (!vm->id_table_capacity) return NULL;
    uint32_t mask = (uint32_t)vm->id_table_capacity - 1;
    for (uint32_t i = ((uint32_t)id * 2654435761u) & mask; vm->id_table[i]; i = (i + 1) & mask) {
        ARInstance *inst = vm->id_table[i];
        if (inst != (ARInstance *)1 && inst->id == id) return inst;
    }
    return NULL;
}

static void id_table_remove(ARVM *vm, ARInstance *inst) {
    uint32_t mask = (uint32_t)vm->id_table_capacity - 1;
    for (uint32_t i = ((uint32_t)inst->id * 2654435761u) & mask; vm->id_table[i]; i = (i + 1) & mask) {
        if (vm->id_table[i] == inst) {
            vm->id_table[i] = (ARInstance *)1;  // A tombstone.
            return;
        }
    }
}

int32_t ar_vm_next_id(ARVM *vm) {
    return vm->next_id;
}

ARInstance *ar_vm_instance_new(ARVM *vm, int32_t object, int32_t id) {
    ARInstance *inst = calloc(1, sizeof(ARInstance));
    inst->vars = ar_struct_new(vm->heap);
    inst->vars->h.fixed = 1;
    inst->vars->instance = inst;
    if (id <= 0) id = vm->next_id;
    if (id >= vm->next_id) vm->next_id = id + 1;
    inst->id = id;
    inst->object = object;
    inst->order = vm->next_order++;
    inst->sprite_index = -1;
    inst->mask_index = -1;
    inst->image_speed = 1;
    inst->image_xscale = 1;
    inst->image_yscale = 1;
    inst->image_alpha = 1;
    inst->image_blend = 0xFFFFFF;
    inst->gravity_direction = 270;
    inst->visible = 1;
    inst->active = 1;
    inst->layer = -1;
    for (int i = 0; i < 12; i++) inst->alarm[i] = -1;
    if (object >= 0 && object < vm->data->object_count) {
        const ARObject *o = &vm->data->objects[object];
        inst->sprite_index = o->sprite;
        inst->mask_index = o->mask;
        inst->visible = (uint8_t)(o->visible != 0);
        inst->solid = (uint8_t)(o->solid != 0);
        inst->persistent = (uint8_t)(o->persistent != 0);
        inst->depth = o->depth;
    }
    if (vm->instance_count == vm->instance_capacity) {
        vm->instance_capacity = vm->instance_capacity ? vm->instance_capacity * 2 : 256;
        vm->instances = realloc(vm->instances, sizeof(ARInstance *) * (size_t)vm->instance_capacity);
    }
    vm->instances[vm->instance_count++] = inst;
    id_table_put(vm, inst);
    return inst;
}

void ar_vm_remove_destroyed(ARVM *vm) {
    int32_t n = 0;
    for (int32_t i = 0; i < vm->instance_count; i++) {
        ARInstance *inst = vm->instances[i];
        if (inst->destroyed) {
            id_table_remove(vm, inst);
            // The struct of the variables goes to the collector.
            inst->vars->h.fixed = 0;
            inst->vars->instance = NULL;
            free(inst);
        } else {
            vm->instances[n++] = inst;
        }
    }
    vm->instance_count = n;
}

int ar_vm_instance_count(ARVM *vm) {
    return vm->instance_count;
}

ARInstance *ar_vm_instance_at(ARVM *vm, int index) {
    return vm->instances[index];
}

int ar_vm_is_object(ARVM *vm, int32_t object, int32_t ancestor) {
    for (int guard = 0; object >= 0 && object < vm->data->object_count && guard < 64; guard++) {
        if (object == ancestor) return 1;
        object = vm->parents[object];
    }
    return 0;
}

ARStruct *ar_vm_self(ARVM *vm) {
    return vm->frame ? vm->frame->self : vm->global;
}

ARStruct *ar_vm_other(ARVM *vm) {
    return vm->frame ? vm->frame->other : vm->global;
}

ARInstance *ar_vm_self_instance(ARVM *vm) {
    ARStruct *s = ar_vm_self(vm);
    return s ? s->instance : NULL;
}

ARInstance *ar_vm_other_instance(ARVM *vm) {
    ARStruct *s = ar_vm_other(vm);
    return s ? s->instance : NULL;
}

int ar_vm_argument_count(ARVM *vm) {
    return vm->frame ? vm->frame->argc : 0;
}

ARValue ar_vm_argument(ARVM *vm, int index) {
    if (!vm->frame || index < 0 || index >= vm->frame->argc) return ar_undefined();
    return vm->frame->args[index];
}

static ARStruct *struct_of(ARVM *vm, ARValue target) {
    if (target.type == AR_STRUCT) return target.o;
    if (!ar_is_number(target)) return NULL;
    int32_t n = ar_to_int(target);
    if (n == AR_SELF) return ar_vm_self(vm);
    if (n == AR_OTHER) return ar_vm_other(vm);
    if (n == AR_GLOBAL) return vm->global;
    if (n >= AR_FIRST_INSTANCE_ID) {
        ARInstance *inst = ar_vm_find_instance(vm, n);
        return inst && !inst->destroyed ? inst->vars : NULL;
    }
    if (n >= 0 || n == AR_ALL) {
        for (int32_t i = 0; i < vm->instance_count; i++) {
            ARInstance *inst = vm->instances[i];
            if (inst->destroyed || !inst->active) continue;
            if (n == AR_ALL || ar_vm_is_object(vm, inst->object, n)) return inst->vars;
        }
    }
    return NULL;
}

int ar_vm_targets(ARVM *vm, ARValue target, ARStruct **out, int max) {
    if (target.type == AR_STRUCT) {
        if (max > 0) out[0] = target.o;
        return 1;
    }
    if (!ar_is_number(target)) return 0;
    int32_t n = ar_to_int(target);
    if (n == AR_SELF || n == AR_OTHER || n == AR_GLOBAL || n >= AR_FIRST_INSTANCE_ID) {
        ARStruct *s = struct_of(vm, target);
        if (!s) return 0;
        if (max > 0) out[0] = s;
        return 1;
    }
    if (n < 0 && n != AR_ALL) return 0;
    int count = 0;
    for (int32_t i = 0; i < vm->instance_count; i++) {
        ARInstance *inst = vm->instances[i];
        if (inst->destroyed || !inst->active) continue;
        if (n == AR_ALL || ar_vm_is_object(vm, inst->object, n)) {
            if (count < max) out[count] = inst->vars;
            count++;
        }
    }
    return count;
}

// -------------------------------------------------------------- variables

static ARValue *local_slot(Frame *f, int32_t key, int add) {
    for (int i = 0; i < f->local_count; i++) {
        if (f->locals[i].key == key) return &f->locals[i].value;
    }
    if (!add) return NULL;
    if (f->local_count == f->local_capacity) {
        int capacity = f->local_capacity * 2;
        Local *grown = malloc(sizeof(Local) * (size_t)capacity);
        memcpy(grown, f->locals, sizeof(Local) * (size_t)f->local_count);
        if (f->locals != f->local_store) free(f->locals);
        f->locals = grown;
        f->local_capacity = capacity;
    }
    Local *l = &f->locals[f->local_count++];
    l->key = key;
    l->value = ar_undefined();
    return &l->value;
}

static ARValue read_plain(ARVM *vm, ARStruct *target, const VarRef *v, int index, int has_index) {
    if (v->kind == V_GLOBAL_BUILTIN) return gVariables[v->builtin]->get(vm, target ? target->instance : NULL, index);
    if (v->kind == V_INSTANCE_BUILTIN && target && target->instance) {
        return gVariables[v->builtin]->get(vm, target->instance, index);
    }
    if (!target) {
        fault(vm, "read of %s with no instance", vm->slot_names[v->slot]);
        return ar_undefined();
    }
    ARValue *slot = ar_vars_find(&target->vars, v->slot);
    if (!slot) {
        if (target != vm->global) {
            // A function of a script, by its name.
            int32_t script = ar_vm_script(vm, vm->slot_names[v->slot]);
            if (script >= 0) return ar_real(script);
        }
        fault(vm, "%s is not set", vm->slot_names[v->slot]);
        return ar_undefined();
    }
    if (!has_index) return *slot;
    if (slot->type != AR_ARRAY) {
        if (index == 0) return *slot;
        fault(vm, "%s is not an array", vm->slot_names[v->slot]);
        return ar_undefined();
    }
    return ar_array_get(slot->a, index);
}

static void write_plain(ARVM *vm, ARStruct *target, const VarRef *v, int index, int has_index, ARValue value) {
    if (v->kind == V_GLOBAL_BUILTIN) {
        if (gVariables[v->builtin]->set) gVariables[v->builtin]->set(vm, target ? target->instance : NULL, index, value);
        return;
    }
    if (v->kind == V_INSTANCE_BUILTIN && target && target->instance) {
        if (gVariables[v->builtin]->set) gVariables[v->builtin]->set(vm, target->instance, index, value);
        else fault(vm, "%s cannot be set", vm->slot_names[v->slot]);
        return;
    }
    if (!target) {
        fault(vm, "write of %s with no instance", vm->slot_names[v->slot]);
        return;
    }
    ARValue *slot = ar_vars_slot(&target->vars, v->slot);
    if (!has_index) {
        *slot = value;
        return;
    }
    if (slot->type != AR_ARRAY) *slot = ar_arrayv(ar_array_new(vm->heap, 0));
    if (!ar_array_set(slot->a, index, value)) fault(vm, "bad index %d of %s", index, vm->slot_names[v->slot]);
}

// The target of a variable: the instance type of the instruction, or a
// value from the stack.
typedef struct {
    int kind;  // 0: structs, 1: locals, 2: arguments
    ARStruct *one;
    ARStruct **many;
    int count;
} Target;

static ARStruct *gTargetBuffer[4096];

static Target target_of(ARVM *vm, ARValue t, int for_write) {
    Target r = {0, NULL, NULL, 0};
    if (t.type == AR_STRUCT) {
        r.one = t.o;
        r.count = 1;
        return r;
    }
    int32_t n = ar_to_int(t);
    if (n == AR_LOCAL) {
        r.kind = 1;
        return r;
    }
    if (n == AR_ARGUMENT) {
        r.kind = 2;
        return r;
    }
    if (n == AR_BUILTIN || n == AR_STATIC) n = AR_SELF;
    if (for_write && (n >= 0 && n < AR_FIRST_INSTANCE_ID)) {
        r.count = ar_vm_targets(vm, ar_real(n), gTargetBuffer, 4096);
        if (r.count > 4096) r.count = 4096;
        r.many = gTargetBuffer;
        return r;
    }
    if (for_write && n == AR_ALL) {
        r.count = ar_vm_targets(vm, ar_real(AR_ALL), gTargetBuffer, 4096);
        if (r.count > 4096) r.count = 4096;
        r.many = gTargetBuffer;
        return r;
    }
    r.one = struct_of(vm, ar_real(n));
    r.count = r.one ? 1 : 0;
    return r;
}

static ARValue read_var(ARVM *vm, Frame *f, ARValue t, int32_t var, int index, int has_index) {
    const VarRef *v = &vm->vars[var];
    if (v->kind == V_ARGUMENT_COUNT) return ar_real(f->argc);
    if (v->kind == V_ARGUMENT || v->kind == V_ARGUMENTS) {
        int k = v->kind == V_ARGUMENT ? v->argument : index;
        return k >= 0 && k < f->argc ? f->args[k] : ar_undefined();
    }
    int32_t n = t.type == AR_STRUCT ? 0 : ar_to_int(t);
    if (t.type != AR_STRUCT && n == AR_LOCAL) {
        ARValue *slot = local_slot(f, v->slot, 0);
        if (!slot) return ar_undefined();
        if (!has_index) return *slot;
        if (slot->type != AR_ARRAY) return index == 0 ? *slot : ar_undefined();
        return ar_array_get(slot->a, index);
    }
    if (t.type != AR_STRUCT && n == AR_BUILTIN && v->kind == V_PLAIN) {
        // A name that the compiler did not know: self, then global.
        ARStruct *self = f->self;
        ARValue *slot = self ? ar_vars_find(&self->vars, v->slot) : NULL;
        if (!slot) slot = ar_vars_find(&vm->global->vars, v->slot);
        if (slot) return has_index && slot->type == AR_ARRAY ? ar_array_get(slot->a, index) : *slot;
        int32_t script = ar_vm_script(vm, vm->slot_names[v->slot]);
        if (script >= 0) return ar_real(script);
        fault(vm, "%s is not set", vm->slot_names[v->slot]);
        return ar_undefined();
    }
    Target r = target_of(vm, t, 0);
    if (r.kind == 2) return index >= 0 && index < f->argc ? f->args[index] : ar_undefined();
    return read_plain(vm, r.one, v, index, has_index);
}

static void write_var(ARVM *vm, Frame *f, ARValue t, int32_t var, int index, int has_index, ARValue value) {
    const VarRef *v = &vm->vars[var];
    if (v->kind == V_ARGUMENT || v->kind == V_ARGUMENTS) {
        int k = v->kind == V_ARGUMENT ? v->argument : index;
        if (k >= 0 && k < f->argc) f->args[k] = value;
        return;
    }
    int32_t n = t.type == AR_STRUCT ? 0 : ar_to_int(t);
    if (t.type != AR_STRUCT && n == AR_LOCAL) {
        ARValue *slot = local_slot(f, v->slot, 1);
        if (!has_index) {
            *slot = value;
            return;
        }
        if (slot->type != AR_ARRAY) *slot = ar_arrayv(ar_array_new(vm->heap, 0));
        ar_array_set(slot->a, index, value);
        return;
    }
    if (t.type != AR_STRUCT && n == AR_BUILTIN && v->kind == V_PLAIN) {
        ARStruct *self = f->self;
        if (self && !ar_vars_find(&self->vars, v->slot) && ar_vars_find(&vm->global->vars, v->slot)) self = vm->global;
        write_plain(vm, self, v, index, has_index, value);
        return;
    }
    Target r = target_of(vm, t, 1);
    if (r.many) {
        for (int i = 0; i < r.count; i++) write_plain(vm, r.many[i], v, index, has_index, value);
        return;
    }
    if (!r.one) {
        fault(vm, "write of %s with no instance", vm->slot_names[v->slot]);
        return;
    }
    write_plain(vm, r.one, v, index, has_index, value);
}

// The array of a variable for a write of two indices. Makes the array, and
// the inner array at the index, when they are not there.
static ARValue inner_array(ARVM *vm, Frame *f, ARValue t, int32_t var, int index) {
    // A variable that is not set yet is not a fault here: the write makes it.
    vm->quiet++;
    ARValue outer = read_var(vm, f, t, var, 0, 0);
    vm->quiet--;
    if (outer.type != AR_ARRAY) {
        outer = ar_arrayv(ar_array_new(vm->heap, 0));
        write_var(vm, f, t, var, 0, 0, outer);
    }
    ARValue inner = ar_array_get(outer.a, index);
    if (inner.type != AR_ARRAY) {
        inner = ar_arrayv(ar_array_new(vm->heap, 0));
        ar_array_set(outer.a, index, inner);
    }
    return inner;
}

// ------------------------------------------------------------ arithmetic

static ARValue arith(ARVM *vm, int op, ARValue a, ARValue b) {
    if (op == OP_ADD && (a.type == AR_STRING || b.type == AR_STRING)) {
        ARStr *x = ar_to_str(vm->heap, a), *y = ar_to_str(vm->heap, b);
        if (a.type != AR_STRING || b.type != AR_STRING) fault(vm, "add of a string and a non-string");
        return ar_strv(ar_str_concat(vm->heap, x, y));
    }
    if (op == OP_MUL && a.type == AR_STRING && ar_is_number(b)) {
        int n = ar_to_int(b);
        ARStr *s = ar_str_new(vm->heap, "", 0);
        for (int i = 0; i < n && i < 10000; i++) s = ar_str_concat(vm->heap, s, a.s);
        return ar_strv(s);
    }
    if (!ar_is_number(a) || !ar_is_number(b)) {
        fault(vm, "arithmetic on a value that is not a number");
        return ar_real(0);
    }
    if (op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_SHL || op == OP_SHR) {
        int64_t x = ar_to_int64(a), y = ar_to_int64(b), r = 0;
        switch (op) {
        case OP_AND: r = x & y; break;
        case OP_OR: r = x | y; break;
        case OP_XOR: r = x ^ y; break;
        case OP_SHL: r = y >= 64 || y < 0 ? 0 : (int64_t)((uint64_t)x << y); break;
        default: r = y >= 64 || y < 0 ? (x < 0 ? -1 : 0) : x >> y; break;
        }
        if (a.type == AR_INT64 || b.type == AR_INT64) return ar_int64(r);
        return ar_real((double)r);
    }
    if ((a.type == AR_INT64 || b.type == AR_INT64) && op != OP_DIV) {
        double ra = ar_to_real(a), rb = ar_to_real(b);
        if (ra == floor(ra) && rb == floor(rb)) {
            int64_t x = ar_to_int64(a), y = ar_to_int64(b);
            switch (op) {
            case OP_ADD: return ar_int64(x + y);
            case OP_SUB: return ar_int64(x - y);
            case OP_MUL: return ar_int64(x * y);
            case OP_REM: return y ? ar_int64(x / y) : ar_int64(0);
            case OP_MOD: return y ? ar_int64(x % y) : ar_int64(0);
            }
        }
    }
    double x = ar_to_real(a), y = ar_to_real(b);
    switch (op) {
    case OP_ADD: return ar_real(x + y);
    case OP_SUB: return ar_real(x - y);
    case OP_MUL: return ar_real(x * y);
    case OP_DIV:
        if (y == 0) {
            fault(vm, "divide by zero");
            return ar_real(0);
        }
        return ar_real(x / y);
    case OP_REM:
        if (y == 0) {
            fault(vm, "divide by zero");
            return ar_real(0);
        }
        return ar_real(trunc(x / y));
    case OP_MOD:
        if (y == 0) {
            fault(vm, "modulo by zero");
            return ar_real(0);
        }
        return ar_real(fmod(x, y));
    }
    return ar_real(0);
}

static ARValue convert(ARVM *vm, ARValue v, int to) {
    switch (to) {
    case T_BOOL: return ar_bool(ar_truthy(v));
    case T_INT:
        if (v.type == AR_REAL || v.type == AR_BOOL) return ar_real(v.r != v.r ? 0 : trunc(v.r));
        if (v.type == AR_INT64) return ar_real((double)(int32_t)v.l);
        return v;
    case T_DOUBLE:
    case T_FLOAT:
        if (v.type == AR_INT64 || v.type == AR_BOOL) return ar_real(ar_to_real(v));
        return v;
    case T_LONG:
        if (ar_is_number(v)) return ar_int64(ar_to_int64(v));
        return v;
    case T_STRING:
        if (v.type != AR_STRING && v.type != AR_UNDEFINED) return ar_strv(ar_to_str(vm->heap, v));
        return v;
    default: return v;
    }
}

// ------------------------------------------------------------------- calls

static ARValue run_code(ARVM *vm, int32_t code, ARStruct *self, ARStruct *other, int argc, ARValue *args);

static ARValue call_function(ARVM *vm, int32_t function, ARStruct *self, ARStruct *other, int argc, ARValue *args) {
    if (function >= AR_UNKNOWN_BASE) {
        int32_t k = function - AR_UNKNOWN_BASE;
        if (k < vm->data->function_count && !vm->logged[k]) {
            vm->logged[k] = 1;
            fprintf(stderr, "gml: no function %s (first call in %s)\n", vm->data->functions[k].name,
                    ar_vm_current_code(vm));
        }
        return ar_undefined();
    }
    if (function >= AR_BUILTIN_BASE) {
        int32_t b = function - AR_BUILTIN_BASE;
        if (b >= gFunctionCount) return ar_undefined();
        // The builtin sees the self and the other through a frame of its own.
        Frame frame;
        memset(&frame, 0, sizeof(frame));
        frame.code = vm->frame ? vm->frame->code : -1;
        frame.self = self;
        frame.other = other;
        frame.args = args;
        frame.argc = argc;
        frame.caller = vm->frame;
        frame.locals = frame.local_store;
        Frame *saved = vm->frame;
        vm->frame = &frame;
        ARValue r = gFunctions[b]->fn(vm, argc, args);
        vm->frame = saved;
        return r;
    }
    if (function >= 0 && function < vm->data->script_count) {
        int32_t code = vm->data->scripts[function].code;
        if (code < 0 || code >= vm->data->code_count) return ar_undefined();
        return run_code(vm, code, self, other, argc, args);
    }
    fault(vm, "call of function %d", function);
    return ar_undefined();
}

ARValue ar_vm_call(ARVM *vm, ARValue function, ARStruct *self, ARStruct *other, int argc, ARValue *args) {
    if (function.type == AR_METHOD) {
        ARMethod *m = function.m;
        ARStruct *bound = self;
        if (m->self.type != AR_UNDEFINED) {
            ARStruct *s = struct_of(vm, m->self);
            if (s) bound = s;
        }
        return call_function(vm, m->function, bound, self, argc, args);
    }
    if (ar_is_number(function)) return call_function(vm, ar_to_int(function), self, other, argc, args);
    fault(vm, "call of a value that is not a function");
    return ar_undefined();
}

ARValue ar_vm_call_script(ARVM *vm, int32_t script, ARStruct *self, ARStruct *other, int argc, ARValue *args) {
    return call_function(vm, script, self, other, argc, args);
}

ARValue ar_vm_run(ARVM *vm, int32_t code, ARStruct *self, ARStruct *other, int argc, ARValue *args) {
    if (code < 0 || code >= vm->data->code_count) return ar_undefined();
    return run_code(vm, code, self, other, argc, args);
}

void ar_vm_run_global_scripts(ARVM *vm) {
    for (int32_t i = 0; i < vm->data->global_init_count; i++) {
        int32_t code = (int32_t)vm->data->global_inits[i];
        run_code(vm, code, vm->global, vm->global, 0, NULL);
    }
}

// The methods and the constructors of GML 2.3, which the compiler calls
// by these names.
static ARValue value_of_struct(ARVM *vm, ARStruct *s) {
    if (!s) return ar_undefined();
    if (s->instance) return ar_real(((ARInstance *)s->instance)->id);
    (void)vm;
    return ar_structv(s);
}

static ARValue fn_method(ARVM *vm, int argc, ARValue *args) {
    if (argc < 2) return ar_undefined();
    ARValue self = args[0];
    ARValue function = args[1];
    if (function.type == AR_METHOD) function = ar_real(function.m->function);
    if (ar_is_number(self)) {
        int32_t n = ar_to_int(self);
        if (n == AR_SELF || n == AR_OTHER) {
            ARStruct *s = n == AR_SELF ? ar_vm_self(vm) : ar_vm_other(vm);
            // A function of a script is not bound to the global struct.
            self = s == vm->global ? ar_undefined() : value_of_struct(vm, s);
        }
    }
    ARMethod *m = ar_method_new(vm->heap, ar_to_int(function), self);
    ARValue v;
    v.type = AR_METHOD;
    v.m = m;
    return v;
}

static ARValue fn_this(ARVM *vm, int argc, ARValue *args) {
    (void)argc;
    (void)args;
    ARStruct *s = ar_vm_self(vm);
    return s == vm->global ? ar_structv(s) : value_of_struct(vm, s);
}

static ARValue fn_other(ARVM *vm, int argc, ARValue *args) {
    (void)argc;
    (void)args;
    ARStruct *s = ar_vm_other(vm);
    return s == vm->global ? ar_structv(s) : value_of_struct(vm, s);
}

static ARValue fn_new_array(ARVM *vm, int argc, ARValue *args) {
    ARArray *a = ar_array_new(vm->heap, argc);
    for (int i = 0; i < argc; i++) a->items[i] = args[i];
    return ar_arrayv(a);
}

static ARValue fn_new_object(ARVM *vm, int argc, ARValue *args) {
    ARStruct *s = ar_struct_new(vm->heap);
    if (argc < 1) return ar_structv(s);
    ARValue function = args[0];
    int32_t f = function.type == AR_METHOD ? function.m->function : ar_to_int(function);
    // The constructor runs with the new struct as self.
    call_function(vm, f, s, ar_vm_self(vm), argc - 1, args + 1);
    return ar_structv(s);
}

static const ARBuiltin kInternal[] = {
    {"method", fn_method},
    {"@@This@@", fn_this},
    {"@@Other@@", fn_other},
    {"@@NewGMLArray@@", fn_new_array},
    {"@@NewGMLObject@@", fn_new_object},
};

__attribute__((constructor)) static void register_internal(void) {
    ar_vm_register(kInternal, (int)(sizeof(kInternal) / sizeof(kInternal[0])), NULL, 0);
}

// ------------------------------------------------------------- interpreter

#define PUSH(v, sz)                                       \
    do {                                                  \
        if (vm->sp >= STACK_SIZE - 1) goto overflow;      \
        vm->sizes[vm->sp] = (uint8_t)(sz);                \
        vm->stack[vm->sp++] = (v);                        \
    } while (0)
#define POP() (vm->sp > base ? vm->stack[--vm->sp] : (underflow = 1, ar_undefined()))
#define TOP() (vm->stack[vm->sp - 1])

static void free_frame(Frame *f) {
    if (f->locals != f->local_store) free(f->locals);
    for (int i = 0; i < f->env_count; i++) free(f->envs[i].targets);
    free(f->envs);
}

static ARValue run_code(ARVM *vm, int32_t code, ARStruct *self, ARStruct *other, int argc, ARValue *args) {
    if (vm->depth >= MAX_DEPTH) {
        fault(vm, "the calls are too deep");
        return ar_undefined();
    }
    Frame f;
    memset(&f, 0, sizeof(f));
    f.code = code;
    f.self = self ? self : vm->global;
    f.other = other ? other : f.self;
    f.locals = f.local_store;
    f.local_capacity = 16;
    // The arguments are a copy, because the code can set them.
    ARValue arg_store[16];
    int arg_count = argc;
    int need = vm->data->codes[code].arguments;
    if (need > arg_count) arg_count = need;
    ARValue *arg_copy = arg_count <= 16 ? arg_store : malloc(sizeof(ARValue) * (size_t)arg_count);
    for (int i = 0; i < arg_count; i++) arg_copy[i] = i < argc ? args[i] : ar_undefined();
    f.args = arg_copy;
    f.argc = argc;
    f.caller = vm->frame;
    vm->frame = &f;
    vm->depth++;

    const Blob *blob = &vm->blobs[vm->code_blob[code]];
    const Insn *insns = blob->insns;
    int32_t pc = vm->code_start[code];
    int32_t base = vm->sp;
    int underflow = 0;
    ARValue result = ar_undefined();

    for (;;) {
        const Insn *in = &insns[pc++];
        switch (in->op) {
        case I_PUSH_CONST:
            PUSH(vm->consts[in->arg], type_size(in->t1));
            break;

        case I_PUSH_VAR: {
            int kind = in->sub;
            if (kind == VT_ARRAY || kind == VT_PUSHAF) {
                int index = ar_to_int(POP());
                ARValue t = POP();
                PUSH(read_var(vm, &f, t, in->arg, index, 1), 16);
            } else if (kind == VT_POPAF) {
                int index = ar_to_int(POP());
                ARValue t = POP();
                PUSH(inner_array(vm, &f, t, in->arg, index), 16);
            } else {
                ARValue t;
                if (kind == VT_STACKTOP || in->inst == AR_STACKTOP) {
                    t = POP();
                    if (t.type != AR_STRUCT && ar_to_int(t) == AR_STACKTOP) t = POP();
                } else {
                    t = ar_real(in->inst);
                }
                PUSH(read_var(vm, &f, t, in->arg, -1, 0), 16);
            }
            break;
        }

        case I_POP: {
            int kind = in->sub;
            ARValue value = ar_undefined(), t;
            if (kind == VT_ARRAY) {
                // With an int as the first type, the value is on the top.
                if (in->t1 == T_INT) value = POP();
                int index = ar_to_int(POP());
                t = POP();
                if (in->t1 != T_INT) value = POP();
                write_var(vm, &f, t, in->arg, index, 1, value);
            } else if (kind == VT_STACKTOP || in->inst == AR_STACKTOP) {
                if (in->t1 == T_INT) value = POP();
                t = POP();
                if (t.type != AR_STRUCT && ar_to_int(t) == AR_STACKTOP) t = POP();
                if (in->t1 != T_INT) value = POP();
                write_var(vm, &f, t, in->arg, -1, 0, value);
            } else {
                value = POP();
                write_var(vm, &f, ar_real(in->inst), in->arg, -1, 0, value);
            }
            break;
        }

        case I_POP_SWAP: {
            // pop.e.v N: takes the value from under N - 4 other items. This
            // form is in GameMaker 2.3; Deltarune does not use it.
            fault(vm, "pop swap is not supported");
            goto fail;
        }

        case I_CONV: {
            ARValue v = POP();
            PUSH(convert(vm, v, in->t2), type_size(in->t2));
            break;
        }

        case I_ARITH: {
            ARValue b = POP();
            ARValue a = POP();
            int size = type_size(in->t1) > type_size(in->t2) ? type_size(in->t1) : type_size(in->t2);
            PUSH(arith(vm, in->sub, a, b), size);
            break;
        }

        case I_NEG: {
            ARValue a = POP();
            if (a.type == AR_INT64) PUSH(ar_int64(-a.l), 8);
            else PUSH(ar_real(-ar_to_real(a)), type_size(in->t1));
            break;
        }

        case I_NOT: {
            ARValue a = POP();
            if (in->t1 == T_BOOL) PUSH(ar_bool(!ar_truthy(a)), 4);
            else if (a.type == AR_INT64) PUSH(ar_int64(~a.l), 8);
            else PUSH(ar_real((double)~ar_to_int64(a)), type_size(in->t1));
            break;
        }

        case I_CMP: {
            ARValue b = POP();
            ARValue a = POP();
            int ordered;
            int c = ar_compare(a, b, &ordered);
            int r = 0;
            switch (in->sub) {
            case 1: r = ordered && c < 0; break;
            case 2: r = ordered && c <= 0; break;
            case 3: r = ordered && c == 0; break;
            case 4: r = !(ordered && c == 0); break;
            case 5: r = ordered && c >= 0; break;
            case 6: r = ordered && c > 0; break;
            }
            PUSH(ar_bool(r), 4);
            break;
        }

        case I_DUP: {
            // Copies the top (count + 1) * size bytes.
            int bytes = (in->extra + 1) * type_size(in->t1);
            int32_t i = vm->sp;
            int sum = 0;
            while (sum < bytes && i > base) sum += vm->sizes[--i];
            int32_t n = vm->sp - i;
            if (vm->sp + n >= STACK_SIZE) goto overflow;
            for (int32_t k = 0; k < n; k++) {
                vm->stack[vm->sp + k] = vm->stack[i + k];
                vm->sizes[vm->sp + k] = vm->sizes[i + k];
            }
            vm->sp += n;
            break;
        }

        case I_DUP_SWAP: {
            // Moves the top block of count * size bytes under the next block
            // of ((sub & 0x7F) >> 3) * size bytes.
            int top_bytes = in->extra * type_size(in->t1);
            int under_bytes = ((in->sub & 0x7F) >> 3) * type_size(in->t1);
            int32_t i = vm->sp;
            int sum = 0;
            while (sum < top_bytes && i > base) sum += vm->sizes[--i];
            int32_t top_start = i;
            sum = 0;
            while (sum < under_bytes && i > base) sum += vm->sizes[--i];
            int32_t under_start = i;
            int32_t top_n = vm->sp - top_start, under_n = top_start - under_start;
            ARValue tmp_v[64];
            uint8_t tmp_s[64];
            if (top_n > 64) goto fail;
            for (int32_t k = 0; k < top_n; k++) {
                tmp_v[k] = vm->stack[top_start + k];
                tmp_s[k] = vm->sizes[top_start + k];
            }
            memmove(&vm->stack[under_start + top_n], &vm->stack[under_start], sizeof(ARValue) * (size_t)under_n);
            memmove(&vm->sizes[under_start + top_n], &vm->sizes[under_start], (size_t)under_n);
            for (int32_t k = 0; k < top_n; k++) {
                vm->stack[under_start + k] = tmp_v[k];
                vm->sizes[under_start + k] = tmp_s[k];
            }
            break;
        }

        case I_POPZ:
            (void)POP();
            break;

        case I_B:
            pc = in->arg;
            break;

        case I_BT:
            if (ar_truthy(POP())) pc = in->arg;
            break;

        case I_BF:
            if (!ar_truthy(POP())) pc = in->arg;
            break;

        case I_PUSHENV: {
            ARValue target = POP();
            ARStruct *list[1];
            int count = ar_vm_targets(vm, target, list, 0);
            if (count == 0) {
                // Go past the popenv of this with.
                pc = in->arg + 1;
                break;
            }
            if (f.env_count == f.env_capacity) {
                f.env_capacity = f.env_capacity ? f.env_capacity * 2 : 4;
                f.envs = realloc(f.envs, sizeof(Env) * (size_t)f.env_capacity);
            }
            Env *e = &f.envs[f.env_count++];
            e->targets = malloc(sizeof(ARStruct *) * (size_t)count);
            e->count = ar_vm_targets(vm, target, e->targets, count);
            if (e->count > count) e->count = count;
            e->index = 0;
            e->self = f.self;
            e->other = f.other;
            f.other = f.self;
            f.self = e->targets[0];
            break;
        }

        case I_POPENV: {
            if (f.env_count == 0) goto fail;
            Env *e = &f.envs[f.env_count - 1];
            // The next target that is still alive.
            e->index++;
            while (e->index < e->count && e->targets[e->index]->instance &&
                   ((ARInstance *)e->targets[e->index]->instance)->destroyed) {
                e->index++;
            }
            if (e->index < e->count) {
                f.self = e->targets[e->index];
                pc = in->arg;
            } else {
                f.self = e->self;
                f.other = e->other;
                free(e->targets);
                f.env_count--;
            }
            break;
        }

        case I_POPENV_DROP: {
            // A break or a return out of a with.
            if (f.env_count == 0) goto fail;
            Env *e = &f.envs[f.env_count - 1];
            f.self = e->self;
            f.other = e->other;
            free(e->targets);
            f.env_count--;
            break;
        }

        case I_CALL: {
            int n = in->extra;
            ARValue call_args[32];
            ARValue *a = n <= 32 ? call_args : malloc(sizeof(ARValue) * (size_t)n);
            for (int i = 0; i < n; i++) a[i] = POP();
            f.pc = pc;
            ARValue r = call_function(vm, in->arg, f.self, f.other, n, a);
            if (a != call_args) free(a);
            vm->frame = &f;
            PUSH(r, 16);
            break;
        }

        case I_CALLV: {
            int n = in->extra;
            ARValue function = POP();
            ARValue self_value = POP();
            ARValue call_args[32];
            ARValue *a = n <= 32 ? call_args : malloc(sizeof(ARValue) * (size_t)n);
            for (int i = 0; i < n; i++) a[i] = POP();
            ARStruct *call_self = struct_of(vm, self_value);
            if (!call_self) call_self = f.self;
            f.pc = pc;
            ARValue r;
            if (function.type == AR_METHOD) {
                ARStruct *bound = call_self;
                if (function.m->self.type != AR_UNDEFINED) {
                    ARStruct *s = struct_of(vm, function.m->self);
                    if (s) bound = s;
                }
                r = call_function(vm, function.m->function, bound, f.self, n, a);
            } else if (ar_is_number(function)) {
                r = call_function(vm, ar_to_int(function), call_self, f.self, n, a);
            } else {
                fault(vm, "call of a value that is not a function");
                r = ar_undefined();
            }
            if (a != call_args) free(a);
            vm->frame = &f;
            PUSH(r, 16);
            break;
        }

        case I_BREAK:
            switch (in->arg) {
            case -1: break;  // chkindex: the index check of an array.
            case -2: {       // pushaf
                int index = ar_to_int(POP());
                ARValue a = POP();
                if (a.type == AR_ARRAY) PUSH(ar_array_get(a.a, index), 16);
                else PUSH(index == 0 ? a : ar_undefined(), 16);
                break;
            }
            case -3: {  // popaf
                int index = ar_to_int(POP());
                ARValue a = POP();
                ARValue value = POP();
                if (a.type == AR_ARRAY) ar_array_set(a.a, index, value);
                else fault(vm, "write of an index of a value that is not an array");
                break;
            }
            case -4: {  // pushac: the array of a variable for a later popaf.
                break;
            }
            case -5:  // setowner: the owner of a copy on write.
                (void)POP();
                break;
            case -6:  // isstaticok
                PUSH(ar_bool(1), 4);
                break;
            case -7:  // setstatic
                break;
            case -8:  // savearef
            case -9:  // restorearef
                break;
            case -10: {  // chknullish: the value stays; a bool goes on top.
                ARValue v = TOP();
                PUSH(ar_bool(v.type == AR_UNDEFINED), 4);
                break;
            }
            default:
                fault(vm, "break %d is not supported", in->arg);
                goto fail;
            }
            break;

        case I_RET:
            result = POP();
            goto done;

        case I_EXIT:
        case I_END:
            goto done;
        }
        if (underflow) {
            fault(vm, "the stack is empty");
            goto fail;
        }
    }

overflow:
    fault(vm, "the stack is full");
fail:
    result = ar_undefined();
done:
    vm->sp = base;
    vm->depth--;
    vm->frame = f.caller;
    free_frame(&f);
    if (arg_copy != arg_store) free(arg_copy);
    return result;
}

// --------------------------------------------------------------- collector

void ar_vm_collect(ARVM *vm, void (*mark_more)(ARVM *vm, void *context), void *context) {
    ar_mark_obj(&vm->global->h);
    for (int32_t i = 0; i < vm->instance_count; i++) ar_mark_obj(&vm->instances[i]->vars->h);
    for (int32_t i = 0; i < vm->sp; i++) ar_mark_value(vm->stack[i]);
    for (int32_t i = 0; i < vm->const_count; i++) ar_mark_value(vm->consts[i]);
    if (mark_more) mark_more(vm, context);
    ar_heap_sweep(vm->heap);
}
