// ar_value: the values of GML and the heap that holds them.
//
// A value is a number, a string, an array, a struct, a method, a 64-bit
// integer, or undefined. Strings, arrays, structs, and methods live on a
// heap with a mark-and-sweep collector. The collector runs only when no
// GML code runs, so the C code can keep a heap pointer during one call.

#ifndef AR_VALUE_H
#define AR_VALUE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AR_UNDEFINED = 0,
    AR_REAL,    // A number. Int32 and bool of GML are numbers too.
    AR_BOOL,    // A number from a compare: 1 or 0.
    AR_INT64,
    AR_STRING,
    AR_ARRAY,
    AR_STRUCT,
    AR_METHOD,
    AR_PTR      // A handle of the runtime, as a number.
} ARType;

typedef struct ARHeap ARHeap;
typedef struct ARObj ARObj;
typedef struct ARStr ARStr;
typedef struct ARArray ARArray;
typedef struct ARStruct ARStruct;
typedef struct ARMethod ARMethod;

typedef struct {
    uint32_t type;
    union {
        double r;
        int64_t l;
        ARStr *s;
        ARArray *a;
        ARStruct *o;
        ARMethod *m;
        ARObj *obj;
    };
} ARValue;

// The header of each object on the heap.
struct ARObj {
    ARObj *next;
    uint8_t kind;    // AR_STRING, AR_ARRAY, AR_STRUCT, or AR_METHOD
    uint8_t mark;
    uint8_t fixed;   // The collector does not free a fixed object.
    uint8_t pad;
};

struct ARStr {
    ARObj h;
    uint32_t length;  // In bytes, with no end zero.
    int32_t chars;    // The count of UTF-8 characters, or -1 when unknown.
    char text[];      // Ends with a zero.
};

struct ARArray {
    ARObj h;
    int32_t length, capacity;
    ARValue *items;
};

// A map from a variable slot to a value.
typedef struct {
    int32_t *keys;
    ARValue *values;
    int32_t count, capacity;  // The capacity is 0 or a power of two.
} ARVarMap;

struct ARStruct {
    ARObj h;
    ARVarMap vars;
    void *instance;  // The instance of the runtime, or NULL for a struct.
};

// A function and the self that it runs with.
struct ARMethod {
    ARObj h;
    int32_t function;  // A script index, or AR_BUILTIN_BASE + a builtin.
    ARValue self;      // An instance id, a struct, or undefined.
};

#define AR_BUILTIN_BASE 1000000

static inline ARValue ar_real(double r) {
    ARValue v;
    v.type = AR_REAL;
    v.r = r;
    return v;
}

static inline ARValue ar_bool(int b) {
    ARValue v;
    v.type = AR_BOOL;
    v.r = b ? 1 : 0;
    return v;
}

static inline ARValue ar_undefined(void) {
    ARValue v;
    v.type = AR_UNDEFINED;
    v.l = 0;
    return v;
}

static inline ARValue ar_int64(int64_t l) {
    ARValue v;
    v.type = AR_INT64;
    v.l = l;
    return v;
}

static inline ARValue ar_strv(ARStr *s) {
    ARValue v;
    v.type = AR_STRING;
    v.s = s;
    return v;
}

static inline ARValue ar_arrayv(ARArray *a) {
    ARValue v;
    v.type = AR_ARRAY;
    v.a = a;
    return v;
}

static inline ARValue ar_structv(ARStruct *o) {
    ARValue v;
    v.type = AR_STRUCT;
    v.o = o;
    return v;
}

static inline int ar_is_number(ARValue v) {
    return v.type == AR_REAL || v.type == AR_BOOL || v.type == AR_INT64 || v.type == AR_PTR;
}

ARHeap *ar_heap_new(void);
void ar_heap_free(ARHeap *heap);
// The bytes on the heap now, and since the last collection.
size_t ar_heap_bytes(const ARHeap *heap);
size_t ar_heap_new_bytes(const ARHeap *heap);

ARStr *ar_str_new(ARHeap *heap, const char *text, size_t length);
ARStr *ar_str_cstr(ARHeap *heap, const char *text);
ARStr *ar_str_concat(ARHeap *heap, const ARStr *a, const ARStr *b);
// The count of characters. A character is one UTF-8 sequence.
int32_t ar_str_chars(ARStr *s);
// The byte offset of the character at a 0-based index, or the length.
size_t ar_str_offset(ARStr *s, int32_t index);

ARArray *ar_array_new(ARHeap *heap, int32_t length);
// Returns 0 when the index is negative or too large to grow to.
int ar_array_set(ARArray *a, int32_t index, ARValue v);
ARValue ar_array_get(const ARArray *a, int32_t index);
int ar_array_resize(ARArray *a, int32_t length);

ARStruct *ar_struct_new(ARHeap *heap);
ARMethod *ar_method_new(ARHeap *heap, int32_t function, ARValue self);

// The variables of a struct or of an instance.
ARValue *ar_vars_find(const ARVarMap *map, int32_t key);
// Finds the value of a key, or adds the key with undefined.
ARValue *ar_vars_slot(ARVarMap *map, int32_t key);
int ar_vars_remove(ARVarMap *map, int32_t key);
void ar_vars_clear(ARVarMap *map);

// Marks the objects that a value or a map reaches, for the collector.
void ar_mark_value(ARValue v);
void ar_mark_vars(const ARVarMap *map);
void ar_mark_obj(ARObj *obj);
// Frees each object that is not marked or fixed, and clears the marks.
void ar_heap_sweep(ARHeap *heap);

// The rules of GML for numbers, truth, and compare.
double ar_to_real(ARValue v);
int64_t ar_to_int64(ARValue v);
int32_t ar_to_int(ARValue v);
int ar_truthy(ARValue v);
// -1, 0, or 1. Numbers compare with an epsilon of 0.00001. A number and a
// string are not equal; *ordered is 0 when the values do not compare.
int ar_compare(ARValue a, ARValue b, int *ordered);
int ar_equal(ARValue a, ARValue b);

// The text of a value, as string() gives it.
ARStr *ar_to_str(ARHeap *heap, ARValue v);
// Writes the text of a number, as string() gives it.
void ar_format_real(double r, char *out, size_t size);

#endif
