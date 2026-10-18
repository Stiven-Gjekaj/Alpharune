#include "ARAsm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ARForm.h"

#define MAX_ITEMS 512

typedef struct {
    int word;     // The word of the instruction in its code.
    int kind;     // 0: a variable, 1: a function
    int key;      // The index of the variable or function.
    int vartype;  // The kind of the variable reference, shifted up 24.
} Occurrence;

typedef struct {
    char name[64];
    int arguments;
    uint32_t *words;
    int count, capacity;
    char labels[MAX_ITEMS][32];
    int label_words[MAX_ITEMS];
    int label_count;
    char fix_labels[MAX_ITEMS][32];
    int fix_words[MAX_ITEMS];
    int fix_count;
    Occurrence occ[MAX_ITEMS * 2];
    int occ_count;
    char strings[MAX_ITEMS][64];
    int string_words[MAX_ITEMS];
    int string_count;
    int parent;  // For a child entry: the code with the blob.
    int offset;  // For a child entry: the start word.
} Code;

typedef struct {
    char name[64];
    int inst;
} Var;

struct ARAsm {
    Code *codes[64];
    int code_count;
    Var vars[MAX_ITEMS];
    int var_count;
    char funcs[MAX_ITEMS][64];
    int func_count;
    char objects[32][64];
    int parents[32];
    int events[32][15][16];  // The code of each event, or -1.
    int object_count;
    int globals[64];
    int global_count;
    char error[160];
    ARForm *form;
};

ARAsm *ar_asm_new(void) {
    return calloc(1, sizeof(ARAsm));
}

void ar_asm_free(ARAsm *a) {
    for (int i = 0; i < a->code_count; i++) {
        free(a->codes[i]->words);
        free(a->codes[i]);
    }
    if (a->form) ar_form_free(a->form);
    free(a);
}

const char *ar_asm_error(ARAsm *a) {
    return a->error;
}

int ar_asm_object(ARAsm *a, const char *name, int parent) {
    int i = a->object_count++;
    snprintf(a->objects[i], sizeof(a->objects[i]), "%s", name);
    a->parents[i] = parent;
    for (int t = 0; t < 15; t++) {
        for (int s = 0; s < 16; s++) a->events[i][t][s] = -1;
    }
    return i;
}

void ar_asm_event(ARAsm *a, int object, int type, int subtype, int code) {
    a->events[object][type][subtype] = code;
}

void ar_asm_global_script(ARAsm *a, int code) {
    a->globals[a->global_count++] = code;
}

static void emit(Code *c, uint32_t w) {
    if (c->count == c->capacity) {
        c->capacity = c->capacity ? c->capacity * 2 : 64;
        c->words = realloc(c->words, sizeof(uint32_t) * (size_t)c->capacity);
    }
    c->words[c->count++] = w;
}

static int var_key(ARAsm *a, const char *name, int inst) {
    for (int i = 0; i < a->var_count; i++) {
        if (strcmp(a->vars[i].name, name) == 0 && a->vars[i].inst == inst) return i;
    }
    snprintf(a->vars[a->var_count].name, 64, "%s", name);
    a->vars[a->var_count].inst = inst;
    return a->var_count++;
}

static int func_key(ARAsm *a, const char *name) {
    for (int i = 0; i < a->func_count; i++) {
        if (strcmp(a->funcs[i], name) == 0) return i;
    }
    snprintf(a->funcs[a->func_count], 64, "%s", name);
    return a->func_count++;
}

static int type_of(char c) {
    switch (c) {
    case 'd': return 0;
    case 'f': return 1;
    case 'i': return 2;
    case 'l': return 3;
    case 'b': return 4;
    case 'v': return 5;
    case 's': return 6;
    case 'e': return 0xF;
    default: return -1;
    }
}

static int vartype_of(const char *word) {
    if (!word || !*word) return 0xA0;
    if (strcmp(word, "array") == 0) return 0x00;
    if (strcmp(word, "stacktop") == 0) return 0x80;
    if (strcmp(word, "pushaf") == 0) return 0x10;
    if (strcmp(word, "popaf") == 0) return 0x90;
    return -1;
}

static int fail(ARAsm *a, int line, const char *text) {
    snprintf(a->error, sizeof(a->error), "line %d: %s", line, text);
    return 0;
}

// Reads "INST.name" into inst and name.
static int read_var(const char *text, int *inst, char *name) {
    const char *dot = strchr(text, '.');
    if (!dot) return 0;
    *inst = atoi(text);
    snprintf(name, 64, "%s", dot + 1);
    return 1;
}

static int assemble_line(ARAsm *a, Code *c, char *line, int number) {
    char *hash = strstr(line, "//");
    if (hash) *hash = 0;
    char op[32] = "", x[96] = "", y[96] = "", z[96] = "";
    // A string operand can hold spaces, so read it apart.
    char *quote = strchr(line, '"');
    char text[128] = "";
    if (quote) {
        char *end = strrchr(line, '"');
        if (end == quote) return fail(a, number, "a string has no end");
        size_t n = (size_t)(end - quote - 1);
        if (n >= sizeof(text)) n = sizeof(text) - 1;
        memcpy(text, quote + 1, n);
        text[n] = 0;
        *quote = 0;
    }
    int got = sscanf(line, "%31s %95s %95s %95s", op, x, y, z);
    if (got <= 0 && !quote) return 1;
    size_t len = strlen(op);
    if (len > 0 && op[len - 1] == ':') {
        op[len - 1] = 0;
        snprintf(c->labels[c->label_count], 32, "%s", op);
        c->label_words[c->label_count++] = c->count;
        return 1;
    }
    char base[16] = "", t1c = 0, t2c = 0;
    {
        char *d1 = strchr(op, '.');
        if (d1) {
            memcpy(base, op, (size_t)(d1 - op));
            base[d1 - op] = 0;
            t1c = d1[1];
            char *d2 = strchr(d1 + 1, '.');
            if (d2) t2c = d2[1];
        } else {
            snprintf(base, sizeof(base), "%s", op);
        }
    }
    int t1 = type_of(t1c), t2 = type_of(t2c);
    if (t1 < 0) t1 = 5;
    if (t2 < 0) t2 = 5;

    static const struct { const char *name; int op; } kArith[] = {
        {"mul", 0x08}, {"div", 0x09}, {"rem", 0x0A}, {"mod", 0x0B}, {"add", 0x0C}, {"sub", 0x0D},
        {"and", 0x0E}, {"or", 0x0F},  {"xor", 0x10}, {"shl", 0x13}, {"shr", 0x14}, {"conv", 0x07},
    };
    for (size_t i = 0; i < sizeof(kArith) / sizeof(kArith[0]); i++) {
        if (strcmp(base, kArith[i].name) == 0) {
            emit(c, (uint32_t)kArith[i].op << 24 | (uint32_t)t2 << 20 | (uint32_t)t1 << 16);
            return 1;
        }
    }
    if (strcmp(base, "neg") == 0 || strcmp(base, "not") == 0 || strcmp(base, "ret") == 0 ||
        strcmp(base, "exit") == 0 || strcmp(base, "popz") == 0) {
        int code = base[0] == 'n' ? (base[1] == 'e' ? 0x11 : 0x12)
                   : base[0] == 'r' ? 0x9C : base[0] == 'e' ? 0x9D : 0x9E;
        emit(c, (uint32_t)code << 24 | (uint32_t)t1 << 16);
        return 1;
    }
    if (strcmp(base, "cmp") == 0) {
        static const char *kinds[] = {"", "lt", "le", "eq", "neq", "ge", "gt"};
        int kind = 0;
        for (int i = 1; i <= 6; i++) {
            if (strcmp(x, kinds[i]) == 0) kind = i;
        }
        if (!kind) return fail(a, number, "a compare has no kind");
        emit(c, 0x15u << 24 | (uint32_t)t2 << 20 | (uint32_t)t1 << 16 | (uint32_t)kind << 8);
        return 1;
    }
    if (strcmp(base, "pushi") == 0) {
        emit(c, 0x84u << 24 | 0xFu << 16 | ((uint32_t)atoi(x) & 0xFFFF));
        return 1;
    }
    if (strcmp(base, "dup") == 0) {
        int swap = 0;
        if (strncmp(y, "swap", 4) == 0) swap = atoi(y + 4);
        emit(c, 0x86u << 24 | (uint32_t)t1 << 16 | (uint32_t)swap << 8 | ((uint32_t)atoi(x) & 0xFF));
        return 1;
    }
    if (strcmp(base, "b") == 0 || strcmp(base, "bt") == 0 || strcmp(base, "bf") == 0 ||
        strcmp(base, "pushenv") == 0 || strcmp(base, "popenv") == 0) {
        int code = strcmp(base, "b") == 0 ? 0xB6 : strcmp(base, "bt") == 0 ? 0xB7
                   : strcmp(base, "bf") == 0 ? 0xB8 : strcmp(base, "pushenv") == 0 ? 0xBA : 0xBB;
        if (code == 0xBB && strcmp(x, "drop") == 0) {
            emit(c, 0xBBu << 24 | 0xF00000u);
            return 1;
        }
        snprintf(c->fix_labels[c->fix_count], 32, "%s", x);
        c->fix_words[c->fix_count++] = c->count;
        emit(c, (uint32_t)code << 24);
        return 1;
    }
    if (strcmp(base, "call") == 0) {
        Occurrence *o = &c->occ[c->occ_count++];
        o->word = c->count;
        o->kind = 1;
        o->key = func_key(a, x);
        o->vartype = 0;
        emit(c, 0xD9u << 24 | 2u << 16 | ((uint32_t)atoi(y) & 0xFFFF));
        emit(c, 0);
        return 1;
    }
    if (strcmp(base, "callv") == 0) {
        emit(c, 0x99u << 24 | 5u << 16 | ((uint32_t)atoi(x) & 0xFFFF));
        return 1;
    }
    if (strcmp(base, "break") == 0) {
        emit(c, 0xFFu << 24 | 0xFu << 16 | ((uint32_t)atoi(x) & 0xFFFF));
        return 1;
    }
    if (strcmp(base, "push") == 0 || strcmp(base, "pushloc") == 0 || strcmp(base, "pushglb") == 0 ||
        strcmp(base, "pushbltn") == 0) {
        int code = strcmp(base, "push") == 0 ? 0xC0 : strcmp(base, "pushloc") == 0 ? 0xC1
                   : strcmp(base, "pushglb") == 0 ? 0xC2 : 0xC3;
        if (t1 == 5) {
            int inst;
            char name[64];
            if (!read_var(x, &inst, name)) return fail(a, number, "a push has no INST.name");
            int vt = vartype_of(y);
            if (vt < 0) return fail(a, number, "an unknown variable kind");
            Occurrence *o = &c->occ[c->occ_count++];
            o->word = c->count;
            o->kind = 0;
            o->key = var_key(a, name, inst);
            o->vartype = vt;
            emit(c, (uint32_t)code << 24 | 5u << 16 | ((uint32_t)inst & 0xFFFF));
            emit(c, 0);
            return 1;
        }
        if (t1 == 6) {
            snprintf(c->strings[c->string_count], 64, "%s", text);
            c->string_words[c->string_count++] = c->count + 1;
            emit(c, (uint32_t)code << 24 | 6u << 16);
            emit(c, 0);
            return 1;
        }
        if (t1 == 2 && strncmp(x, "func:", 5) == 0) {
            Occurrence *o = &c->occ[c->occ_count++];
            o->word = c->count;
            o->kind = 1;
            o->key = func_key(a, x + 5);
            o->vartype = 0;
            emit(c, (uint32_t)code << 24 | 2u << 16);
            emit(c, 0);
            return 1;
        }
        if (t1 == 0xF) {
            emit(c, (uint32_t)code << 24 | 0xFu << 16 | ((uint32_t)atoi(x) & 0xFFFF));
            return 1;
        }
        emit(c, (uint32_t)code << 24 | (uint32_t)t1 << 16);
        if (t1 == 0) {
            double d = atof(x);
            uint32_t w[2];
            memcpy(w, &d, 8);
            emit(c, w[0]);
            emit(c, w[1]);
        } else if (t1 == 3) {
            int64_t l = atoll(x);
            uint32_t w[2];
            memcpy(w, &l, 8);
            emit(c, w[0]);
            emit(c, w[1]);
        } else {
            emit(c, (uint32_t)atoi(x));
        }
        return 1;
    }
    if (strcmp(base, "pop") == 0) {
        int inst;
        char name[64];
        if (!read_var(x, &inst, name)) return fail(a, number, "a pop has no INST.name");
        int vt = vartype_of(y);
        if (vt < 0) return fail(a, number, "an unknown variable kind");
        Occurrence *o = &c->occ[c->occ_count++];
        o->word = c->count;
        o->kind = 0;
        o->key = var_key(a, name, inst);
        o->vartype = vt;
        emit(c, 0x45u << 24 | (uint32_t)t2 << 20 | (uint32_t)t1 << 16 | ((uint32_t)inst & 0xFFFF));
        emit(c, 0);
        return 1;
    }
    return fail(a, number, "an unknown instruction");
}

int ar_asm_code(ARAsm *a, const char *name, int arguments, const char *source) {
    Code *c = calloc(1, sizeof(Code));
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->arguments = arguments;
    c->parent = -1;
    char *copy = strdup(source);
    int number = 0;
    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        number++;
        if (!assemble_line(a, c, line, number)) {
            free(copy);
            free(c->words);
            free(c);
            return -1;
        }
    }
    free(copy);
    // Set the offset of each branch.
    for (int i = 0; i < c->fix_count; i++) {
        int target = -1;
        for (int k = 0; k < c->label_count; k++) {
            if (strcmp(c->labels[k], c->fix_labels[i]) == 0) target = c->label_words[k];
        }
        if (target < 0) {
            snprintf(a->error, sizeof(a->error), "no label %s", c->fix_labels[i]);
            free(c->words);
            free(c);
            return -1;
        }
        c->words[c->fix_words[i]] |= (uint32_t)(target - c->fix_words[i]) & 0x7FFFFF;
    }
    a->codes[a->code_count] = c;
    return a->code_count++;
}

int ar_asm_child(ARAsm *a, const char *name, int arguments, const char *label) {
    int parent = a->code_count - 1;
    if (parent < 0) return -1;
    Code *p = a->codes[parent];
    while (p->parent >= 0) p = a->codes[p->parent];
    int word = -1;
    for (int k = 0; k < p->label_count; k++) {
        if (strcmp(p->labels[k], label) == 0) word = p->label_words[k];
    }
    if (word < 0) return -1;
    Code *c = calloc(1, sizeof(Code));
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->arguments = arguments;
    for (int i = 0; i < a->code_count; i++) {
        if (a->codes[i] == p) c->parent = i;
    }
    c->offset = word;
    a->codes[a->code_count] = c;
    return a->code_count++;
}

const uint8_t *ar_asm_finish(ARAsm *a, size_t *size) {
    ARForm *f = ar_form_new();
    a->form = f;
    ar_form_gen8(f);
    // The strings of push.s get their index now.
    for (int i = 0; i < a->code_count; i++) {
        Code *c = a->codes[i];
        for (int k = 0; k < c->string_count; k++) c->words[c->string_words[k]] = (uint32_t)ar_form_string_index(f, c->strings[k]);
    }

    if (a->object_count > 0) {
        ar_form_chunk(f, "OBJT");
        ar_form_u32(f, (uint32_t)a->object_count);
        char label[48];
        for (int i = 0; i < a->object_count; i++) {
            snprintf(label, sizeof(label), "obj%d", i);
            ar_form_ptr(f, label);
        }
        for (int i = 0; i < a->object_count; i++) {
            snprintf(label, sizeof(label), "obj%d", i);
            ar_form_label(f, label);
            ar_form_str(f, a->objects[i]);
            int32_t head[20] = {-1, 1, 0, 0, 0, 0, a->parents[i] < 0 ? -100 : a->parents[i], -1, 0, 0,
                                1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            for (int k = 0; k < 20; k++) ar_form_u32(f, (uint32_t)head[k]);
            ar_form_u32(f, 15);
            for (int t = 0; t < 15; t++) {
                snprintf(label, sizeof(label), "obj%d_t%d", i, t);
                ar_form_ptr(f, label);
            }
            for (int t = 0; t < 15; t++) {
                snprintf(label, sizeof(label), "obj%d_t%d", i, t);
                ar_form_label(f, label);
                int count = 0;
                for (int s = 0; s < 16; s++) count += a->events[i][t][s] >= 0;
                ar_form_u32(f, (uint32_t)count);
                for (int s = 0; s < 16; s++) {
                    if (a->events[i][t][s] < 0) continue;
                    snprintf(label, sizeof(label), "obj%d_t%d_s%d", i, t, s);
                    ar_form_ptr(f, label);
                }
                for (int s = 0; s < 16; s++) {
                    if (a->events[i][t][s] < 0) continue;
                    snprintf(label, sizeof(label), "obj%d_t%d_s%d", i, t, s);
                    ar_form_label(f, label);
                    ar_form_u32(f, (uint32_t)s);
                    ar_form_u32(f, 1);
                    snprintf(label, sizeof(label), "obj%d_t%d_s%d_a", i, t, s);
                    ar_form_ptr(f, label);
                    ar_form_label(f, label);
                    uint32_t action[14] = {1, 603, 7, 0, 0, 0, 2, 0, (uint32_t)a->events[i][t][s], 0,
                                           0xFFFFFFFF, 0, 0, 0};
                    for (int w = 0; w < 14; w++) ar_form_u32(f, action[w]);
                }
            }
        }
    }

    ar_form_chunk(f, "CODE");
    uint32_t chunk = ar_form_offset(f);
    int n = a->code_count;
    // The start of each blob in the file.
    uint32_t starts[64];
    uint32_t p = chunk + 4 + 4 * (uint32_t)n;
    for (int i = 0; i < n; i++) {
        if (a->codes[i]->parent >= 0) continue;
        starts[i] = p;
        p += 4 * (uint32_t)a->codes[i]->count;
    }
    for (int i = 0; i < n; i++) {
        if (a->codes[i]->parent >= 0) starts[i] = starts[a->codes[i]->parent];
    }
    // The chains of the occurrences, in the order of the file.
    uint32_t var_first[MAX_ITEMS], func_first[MAX_ITEMS];
    int var_occ[MAX_ITEMS] = {0}, func_occ[MAX_ITEMS] = {0};
    uint32_t *last_var[MAX_ITEMS] = {0}, *last_func[MAX_ITEMS] = {0};
    uint32_t last_var_at[MAX_ITEMS], last_func_at[MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        Code *c = a->codes[i];
        if (c->parent >= 0) continue;
        for (int k = 0; k < c->occ_count; k++) {
            Occurrence *o = &c->occ[k];
            uint32_t at = starts[i] + 4 * (uint32_t)o->word;
            uint32_t *operand = &c->words[o->word + 1];
            *operand = (uint32_t)o->vartype << 24;
            if (o->kind == 0) {
                if (var_occ[o->key] == 0) var_first[o->key] = at;
                else *last_var[o->key] |= (at - last_var_at[o->key]) & 0x07FFFFFF;
                last_var[o->key] = operand;
                last_var_at[o->key] = at;
                var_occ[o->key]++;
            } else {
                if (func_occ[o->key] == 0) func_first[o->key] = at;
                else *last_func[o->key] |= (at - last_func_at[o->key]) & 0x07FFFFFF;
                last_func[o->key] = operand;
                last_func_at[o->key] = at;
                func_occ[o->key]++;
            }
        }
    }
    ar_form_u32(f, (uint32_t)n);
    char label[48];
    for (int i = 0; i < n; i++) {
        snprintf(label, sizeof(label), "code%d", i);
        ar_form_ptr(f, label);
    }
    for (int i = 0; i < n; i++) {
        if (a->codes[i]->parent < 0) ar_form_bytes(f, a->codes[i]->words, 4 * (size_t)a->codes[i]->count);
    }
    for (int i = 0; i < n; i++) {
        Code *c = a->codes[i];
        Code *blob = c->parent >= 0 ? a->codes[c->parent] : c;
        snprintf(label, sizeof(label), "code%d", i);
        ar_form_label(f, label);
        ar_form_str(f, c->name);
        ar_form_u32(f, 4 * (uint32_t)blob->count);
        ar_form_u16(f, 4);
        ar_form_u16(f, (uint16_t)c->arguments);
        uint32_t here = ar_form_offset(f);
        ar_form_u32(f, (uint32_t)((int32_t)starts[i] - (int32_t)here));
        ar_form_u32(f, 4 * (uint32_t)c->offset);
    }

    ar_form_chunk(f, "VARI");
    ar_form_u32(f, (uint32_t)a->var_count);
    ar_form_u32(f, (uint32_t)a->var_count);
    ar_form_u32(f, 4);
    for (int i = 0; i < a->var_count; i++) {
        ar_form_str(f, a->vars[i].name);
        ar_form_u32(f, (uint32_t)a->vars[i].inst);
        ar_form_u32(f, (uint32_t)i);
        ar_form_u32(f, (uint32_t)var_occ[i]);
        ar_form_u32(f, var_occ[i] ? var_first[i] : 0xFFFFFFFF);
    }
    ar_form_chunk(f, "FUNC");
    ar_form_u32(f, (uint32_t)a->func_count);
    for (int i = 0; i < a->func_count; i++) {
        ar_form_str(f, a->funcs[i]);
        ar_form_u32(f, (uint32_t)func_occ[i]);
        ar_form_u32(f, func_occ[i] ? func_first[i] + 4 : 0);
    }
    ar_form_u32(f, 0);  // no code locals

    ar_form_chunk(f, "SCPT");
    int scripts = 0;
    for (int i = 0; i < n; i++) scripts += strncmp(a->codes[i]->name, "gml_Script_", 11) == 0;
    ar_form_u32(f, (uint32_t)scripts);
    for (int i = 0; i < n; i++) {
        if (strncmp(a->codes[i]->name, "gml_Script_", 11) != 0) continue;
        snprintf(label, sizeof(label), "scpt%d", i);
        ar_form_ptr(f, label);
    }
    for (int i = 0; i < n; i++) {
        if (strncmp(a->codes[i]->name, "gml_Script_", 11) != 0) continue;
        snprintf(label, sizeof(label), "scpt%d", i);
        ar_form_label(f, label);
        ar_form_str(f, a->codes[i]->name);
        ar_form_u32(f, (uint32_t)i);
    }
    ar_form_chunk(f, "GLOB");
    ar_form_u32(f, (uint32_t)a->global_count);
    for (int i = 0; i < a->global_count; i++) ar_form_u32(f, (uint32_t)a->globals[i]);
    return ar_form_finish(f, size);
}
