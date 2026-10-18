#include "ar_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ARHeap {
    ARObj *objects;
    size_t bytes;
    size_t new_bytes;
};

ARHeap *ar_heap_new(void) {
    return calloc(1, sizeof(ARHeap));
}

// The count of bytes is the size of the object itself. The items of an
// array and the map of a struct are not in the count, because they change
// size after the allocation.
static size_t obj_size(const ARObj *o) {
    switch (o->kind) {
    case AR_STRING: return sizeof(ARStr) + ((const ARStr *)o)->length + 1;
    case AR_ARRAY: return sizeof(ARArray);
    case AR_STRUCT: return sizeof(ARStruct);
    default: return sizeof(ARMethod);
    }
}

static void obj_free(ARObj *o) {
    if (o->kind == AR_ARRAY) {
        free(((ARArray *)o)->items);
    } else if (o->kind == AR_STRUCT) {
        ARStruct *s = (ARStruct *)o;
        free(s->vars.keys);
        free(s->vars.values);
    }
    free(o);
}

void ar_heap_free(ARHeap *heap) {
    if (!heap) return;
    ARObj *o = heap->objects;
    while (o) {
        ARObj *next = o->next;
        obj_free(o);
        o = next;
    }
    free(heap);
}

size_t ar_heap_bytes(const ARHeap *heap) {
    return heap->bytes;
}

size_t ar_heap_new_bytes(const ARHeap *heap) {
    return heap->new_bytes;
}

static void *obj_new(ARHeap *heap, size_t size, int kind) {
    ARObj *o = calloc(1, size);
    if (!o) {
        fprintf(stderr, "ar_value: out of memory\n");
        abort();
    }
    o->kind = (uint8_t)kind;
    o->next = heap->objects;
    heap->objects = o;
    heap->bytes += size;
    heap->new_bytes += size;
    return o;
}

ARStr *ar_str_new(ARHeap *heap, const char *text, size_t length) {
    ARStr *s = obj_new(heap, sizeof(ARStr) + length + 1, AR_STRING);
    s->length = (uint32_t)length;
    s->chars = -1;
    if (length) memcpy(s->text, text, length);
    s->text[length] = 0;
    return s;
}

ARStr *ar_str_cstr(ARHeap *heap, const char *text) {
    return ar_str_new(heap, text, strlen(text));
}

ARStr *ar_str_concat(ARHeap *heap, const ARStr *a, const ARStr *b) {
    ARStr *s = obj_new(heap, sizeof(ARStr) + a->length + b->length + 1, AR_STRING);
    s->length = a->length + b->length;
    s->chars = (a->chars >= 0 && b->chars >= 0) ? a->chars + b->chars : -1;
    memcpy(s->text, a->text, a->length);
    memcpy(s->text + a->length, b->text, b->length);
    s->text[s->length] = 0;
    return s;
}

int32_t ar_str_chars(ARStr *s) {
    if (s->chars < 0) {
        int32_t n = 0;
        for (uint32_t i = 0; i < s->length; i++) {
            if (((uint8_t)s->text[i] & 0xC0) != 0x80) n++;
        }
        s->chars = n;
    }
    return s->chars;
}

size_t ar_str_offset(ARStr *s, int32_t index) {
    if (index <= 0) return 0;
    if (ar_str_chars(s) == (int32_t)s->length) return index < (int32_t)s->length ? (size_t)index : s->length;
    int32_t n = -1;
    for (uint32_t i = 0; i < s->length; i++) {
        if (((uint8_t)s->text[i] & 0xC0) != 0x80) {
            n++;
            if (n == index) return i;
        }
    }
    return s->length;
}

ARArray *ar_array_new(ARHeap *heap, int32_t length) {
    ARArray *a = obj_new(heap, sizeof(ARArray), AR_ARRAY);
    if (length > 0) ar_array_resize(a, length);
    return a;
}

int ar_array_resize(ARArray *a, int32_t length) {
    if (length < 0 || length > (1 << 24)) return 0;
    if (length > a->capacity) {
        int32_t capacity = a->capacity ? a->capacity : 4;
        while (capacity < length) capacity *= 2;
        ARValue *items = realloc(a->items, (size_t)capacity * sizeof(ARValue));
        if (!items) return 0;
        a->items = items;
        a->capacity = capacity;
    }
    for (int32_t i = a->length; i < length; i++) a->items[i] = ar_real(0);
    a->length = length;
    return 1;
}

int ar_array_set(ARArray *a, int32_t index, ARValue v) {
    if (index < 0) return 0;
    if (index >= a->length && !ar_array_resize(a, index + 1)) return 0;
    a->items[index] = v;
    return 1;
}

ARValue ar_array_get(const ARArray *a, int32_t index) {
    if (index < 0 || index >= a->length) return ar_undefined();
    return a->items[index];
}

ARStruct *ar_struct_new(ARHeap *heap) {
    return obj_new(heap, sizeof(ARStruct), AR_STRUCT);
}

ARMethod *ar_method_new(ARHeap *heap, int32_t function, ARValue self) {
    ARMethod *m = obj_new(heap, sizeof(ARMethod), AR_METHOD);
    m->function = function;
    m->self = self;
    return m;
}

// The map uses open addressing with linear probes. A key of -1 is empty.
static uint32_t hash_key(int32_t key) {
    uint32_t x = (uint32_t)key * 2654435761u;
    return x ^ (x >> 16);
}

ARValue *ar_vars_find(const ARVarMap *map, int32_t key) {
    if (map->capacity == 0) return NULL;
    uint32_t mask = (uint32_t)map->capacity - 1;
    for (uint32_t i = hash_key(key) & mask;; i = (i + 1) & mask) {
        if (map->keys[i] == key) return &map->values[i];
        if (map->keys[i] == -1) return NULL;
    }
}

static void vars_grow(ARVarMap *map) {
    int32_t old_capacity = map->capacity;
    int32_t *old_keys = map->keys;
    ARValue *old_values = map->values;
    int32_t capacity = old_capacity ? old_capacity * 2 : 8;
    map->keys = malloc((size_t)capacity * sizeof(int32_t));
    map->values = malloc((size_t)capacity * sizeof(ARValue));
    if (!map->keys || !map->values) {
        fprintf(stderr, "ar_value: out of memory\n");
        abort();
    }
    memset(map->keys, 0xFF, (size_t)capacity * sizeof(int32_t));
    map->capacity = capacity;
    uint32_t mask = (uint32_t)capacity - 1;
    for (int32_t k = 0; k < old_capacity; k++) {
        if (old_keys[k] < 0) continue;
        uint32_t i = hash_key(old_keys[k]) & mask;
        while (map->keys[i] != -1) i = (i + 1) & mask;
        map->keys[i] = old_keys[k];
        map->values[i] = old_values[k];
    }
    free(old_keys);
    free(old_values);
}

ARValue *ar_vars_slot(ARVarMap *map, int32_t key) {
    ARValue *v = ar_vars_find(map, key);
    if (v) return v;
    if ((map->count + 1) * 4 > map->capacity * 3) vars_grow(map);
    uint32_t mask = (uint32_t)map->capacity - 1;
    uint32_t i = hash_key(key) & mask;
    while (map->keys[i] != -1) i = (i + 1) & mask;
    map->keys[i] = key;
    map->values[i] = ar_undefined();
    map->count++;
    return &map->values[i];
}

int ar_vars_remove(ARVarMap *map, int32_t key) {
    ARValue *v = ar_vars_find(map, key);
    if (!v) return 0;
    // Take the entries of the probe chain out, and put them back.
    uint32_t mask = (uint32_t)map->capacity - 1;
    uint32_t i = (uint32_t)(v - map->values);
    map->keys[i] = -1;
    map->count--;
    for (uint32_t j = (i + 1) & mask; map->keys[j] != -1; j = (j + 1) & mask) {
        int32_t k = map->keys[j];
        ARValue value = map->values[j];
        map->keys[j] = -1;
        map->count--;
        *ar_vars_slot(map, k) = value;
    }
    return 1;
}

void ar_vars_clear(ARVarMap *map) {
    free(map->keys);
    free(map->values);
    memset(map, 0, sizeof(*map));
}

// The mark uses a stack of its own, so a deep structure does not use the C
// stack.
static ARObj **gMarkStack;
static size_t gMarkCount, gMarkCapacity;

static void push_mark(ARObj *o) {
    if (!o || o->mark) return;
    o->mark = 1;
    if (o->kind == AR_STRING) return;
    if (gMarkCount == gMarkCapacity) {
        gMarkCapacity = gMarkCapacity ? gMarkCapacity * 2 : 1024;
        gMarkStack = realloc(gMarkStack, gMarkCapacity * sizeof(ARObj *));
        if (!gMarkStack) abort();
    }
    gMarkStack[gMarkCount++] = o;
}

static void push_value(ARValue v) {
    if (v.type >= AR_STRING && v.type <= AR_METHOD) push_mark(v.obj);
}

static void drain(void) {
    while (gMarkCount > 0) {
        ARObj *o = gMarkStack[--gMarkCount];
        if (o->kind == AR_ARRAY) {
            ARArray *a = (ARArray *)o;
            for (int32_t i = 0; i < a->length; i++) push_value(a->items[i]);
        } else if (o->kind == AR_STRUCT) {
            const ARVarMap *m = &((ARStruct *)o)->vars;
            for (int32_t i = 0; i < m->capacity; i++) {
                if (m->keys[i] >= 0) push_value(m->values[i]);
            }
        } else if (o->kind == AR_METHOD) {
            push_value(((ARMethod *)o)->self);
        }
    }
}

void ar_mark_obj(ARObj *obj) {
    push_mark(obj);
    drain();
}

void ar_mark_value(ARValue v) {
    push_value(v);
    drain();
}

void ar_mark_vars(const ARVarMap *map) {
    for (int32_t i = 0; i < map->capacity; i++) {
        if (map->keys[i] >= 0) push_value(map->values[i]);
    }
    drain();
}

void ar_heap_sweep(ARHeap *heap) {
    ARObj **link = &heap->objects;
    while (*link) {
        ARObj *o = *link;
        if (o->mark || o->fixed) {
            o->mark = 0;
            link = &o->next;
        } else {
            *link = o->next;
            heap->bytes -= obj_size(o);
            obj_free(o);
        }
    }
    heap->new_bytes = 0;
}

double ar_to_real(ARValue v) {
    switch (v.type) {
    case AR_REAL:
    case AR_BOOL:
    case AR_PTR: return v.r;
    case AR_INT64: return (double)v.l;
    case AR_STRING: return strtod(v.s->text, NULL);
    default: return 0;
    }
}

int64_t ar_to_int64(ARValue v) {
    if (v.type == AR_INT64) return v.l;
    double r = ar_to_real(v);
    if (r != r) return 0;
    if (r >= 9.2e18) return INT64_MAX;
    if (r <= -9.2e18) return INT64_MIN;
    return (int64_t)r;
}

int32_t ar_to_int(ARValue v) {
    if (v.type == AR_INT64) return (int32_t)v.l;
    double r = ar_to_real(v);
    if (r != r) return 0;
    if (r >= 2147483647.0) return INT32_MAX;
    if (r <= -2147483648.0) return INT32_MIN;
    return (int32_t)r;
}

int ar_truthy(ARValue v) {
    switch (v.type) {
    case AR_REAL:
    case AR_BOOL:
    case AR_PTR: return v.r > 0.5;
    case AR_INT64: return v.l > 0;
    case AR_ARRAY:
    case AR_STRUCT:
    case AR_METHOD: return 1;
    default: return 0;
    }
}

int ar_compare(ARValue a, ARValue b, int *ordered) {
    *ordered = 1;
    if (ar_is_number(a) && ar_is_number(b)) {
        if (a.type == AR_INT64 && b.type == AR_INT64) return a.l < b.l ? -1 : a.l > b.l;
        double x = ar_to_real(a), y = ar_to_real(b);
        if (fabs(x - y) <= 0.00001) return 0;
        return x < y ? -1 : 1;
    }
    if (a.type == AR_STRING && b.type == AR_STRING) {
        int c = strcmp(a.s->text, b.s->text);
        return c < 0 ? -1 : c > 0;
    }
    if (a.type == AR_UNDEFINED && b.type == AR_UNDEFINED) return 0;
    if (a.type == b.type && a.type >= AR_ARRAY && a.type <= AR_METHOD) {
        if (a.obj == b.obj) return 0;
        // Two methods of one function and one self are equal.
        if (a.type == AR_METHOD && a.m->function == b.m->function && ar_equal(a.m->self, b.m->self)) return 0;
        *ordered = 0;
        return a.obj < b.obj ? -1 : 1;
    }
    *ordered = 0;
    return a.type < b.type ? -1 : 1;
}

int ar_equal(ARValue a, ARValue b) {
    int ordered;
    return ar_compare(a, b, &ordered) == 0;
}

void ar_format_real(double r, char *out, size_t size) {
    if (r != r) {
        snprintf(out, size, "NaN");
    } else if (isinf(r)) {
        snprintf(out, size, r > 0 ? "inf" : "-inf");
    } else if (r == floor(r) && fabs(r) < 1e15) {
        snprintf(out, size, "%.0f", r == 0 ? 0.0 : r);
    } else {
        snprintf(out, size, "%.2f", r);
        if (strcmp(out, "-0.00") == 0) snprintf(out, size, "0.00");
    }
}

static void append(char **buf, size_t *len, size_t *cap, const char *text, size_t n) {
    if (*len + n + 1 > *cap) {
        *cap = (*len + n + 1) * 2;
        *buf = realloc(*buf, *cap);
        if (!*buf) abort();
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = 0;
}

static void write_value(ARHeap *heap, ARValue v, char **buf, size_t *len, size_t *cap, int depth) {
    char tmp[64];
    switch (v.type) {
    case AR_UNDEFINED: append(buf, len, cap, "undefined", 9); break;
    case AR_REAL:
    case AR_BOOL:
    case AR_PTR:
        ar_format_real(v.r, tmp, sizeof(tmp));
        append(buf, len, cap, tmp, strlen(tmp));
        break;
    case AR_INT64:
        snprintf(tmp, sizeof(tmp), "%lld", (long long)v.l);
        append(buf, len, cap, tmp, strlen(tmp));
        break;
    case AR_STRING:
        if (depth > 0) append(buf, len, cap, "\"", 1);
        append(buf, len, cap, v.s->text, v.s->length);
        if (depth > 0) append(buf, len, cap, "\"", 1);
        break;
    case AR_ARRAY:
        append(buf, len, cap, "[ ", 2);
        if (depth < 8) {
            for (int32_t i = 0; i < v.a->length; i++) {
                if (i) append(buf, len, cap, ",", 1);
                write_value(heap, v.a->items[i], buf, len, cap, depth + 1);
            }
        }
        append(buf, len, cap, " ]", 2);
        break;
    case AR_STRUCT: append(buf, len, cap, "{ }", 3); break;
    case AR_METHOD:
        snprintf(tmp, sizeof(tmp), "function %d", (int)v.m->function);
        append(buf, len, cap, tmp, strlen(tmp));
        break;
    }
}

ARStr *ar_to_str(ARHeap *heap, ARValue v) {
    if (v.type == AR_STRING) return v.s;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    append(&buf, &len, &cap, "", 0);
    write_value(heap, v, &buf, &len, &cap, 0);
    ARStr *s = ar_str_new(heap, buf, len);
    free(buf);
    return s;
}
