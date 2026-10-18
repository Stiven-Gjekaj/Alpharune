// The builtin functions of GML for numbers, strings, arrays, structs,
// scripts, and the ds structures.

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ar_run_internal.h"

#define FN(name) static ARValue name(ARVM *vm, int argc, ARValue *a)
#define UNUSED (void)vm, (void)argc, (void)a
#define HEAP ar_vm_heap(vm)
#define RUN ar_run_of(vm)
#define STR(text) ar_strv(ar_str_cstr(HEAP, text))
#define ARG_STR(i) (i < argc ? ar_to_str(HEAP, a[i]) : ar_str_cstr(HEAP, ""))

// ---------------------------------------------------------------- numbers

FN(f_abs) { UNUSED; return ar_real(fabs(ar_arg_real(argc, a, 0, 0))); }
FN(f_sign) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0);
    return ar_real(x > 0 ? 1 : x < 0 ? -1 : 0);
}
FN(f_round) { UNUSED; return ar_real(rint(ar_arg_real(argc, a, 0, 0))); }
FN(f_floor) { UNUSED; return ar_real(floor(ar_arg_real(argc, a, 0, 0))); }
FN(f_ceil) { UNUSED; return ar_real(ceil(ar_arg_real(argc, a, 0, 0))); }
FN(f_frac) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0);
    return ar_real(x - trunc(x));
}
FN(f_sqrt) { UNUSED; return ar_real(sqrt(ar_arg_real(argc, a, 0, 0))); }
FN(f_sqr) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0);
    return ar_real(x * x);
}
FN(f_power) { UNUSED; return ar_real(pow(ar_arg_real(argc, a, 0, 0), ar_arg_real(argc, a, 1, 1))); }
FN(f_exp) { UNUSED; return ar_real(exp(ar_arg_real(argc, a, 0, 0))); }
FN(f_ln) { UNUSED; return ar_real(log(ar_arg_real(argc, a, 0, 1))); }
FN(f_log2) { UNUSED; return ar_real(log2(ar_arg_real(argc, a, 0, 1))); }
FN(f_log10) { UNUSED; return ar_real(log10(ar_arg_real(argc, a, 0, 1))); }
FN(f_sin) { UNUSED; return ar_real(sin(ar_arg_real(argc, a, 0, 0))); }
FN(f_cos) { UNUSED; return ar_real(cos(ar_arg_real(argc, a, 0, 0))); }
FN(f_tan) { UNUSED; return ar_real(tan(ar_arg_real(argc, a, 0, 0))); }
FN(f_arcsin) { UNUSED; return ar_real(asin(ar_arg_real(argc, a, 0, 0))); }
FN(f_arccos) { UNUSED; return ar_real(acos(ar_arg_real(argc, a, 0, 0))); }
FN(f_arctan) { UNUSED; return ar_real(atan(ar_arg_real(argc, a, 0, 0))); }
FN(f_arctan2) { UNUSED; return ar_real(atan2(ar_arg_real(argc, a, 0, 0), ar_arg_real(argc, a, 1, 1))); }
FN(f_dsin) { UNUSED; return ar_real(sin(ar_arg_real(argc, a, 0, 0) * M_PI / 180)); }
FN(f_dcos) { UNUSED; return ar_real(cos(ar_arg_real(argc, a, 0, 0) * M_PI / 180)); }
FN(f_dtan) { UNUSED; return ar_real(tan(ar_arg_real(argc, a, 0, 0) * M_PI / 180)); }
FN(f_darcsin) { UNUSED; return ar_real(asin(ar_arg_real(argc, a, 0, 0)) * 180 / M_PI); }
FN(f_darccos) { UNUSED; return ar_real(acos(ar_arg_real(argc, a, 0, 0)) * 180 / M_PI); }
FN(f_darctan) { UNUSED; return ar_real(atan(ar_arg_real(argc, a, 0, 0)) * 180 / M_PI); }
FN(f_darctan2) {
    UNUSED;
    return ar_real(atan2(ar_arg_real(argc, a, 0, 0), ar_arg_real(argc, a, 1, 1)) * 180 / M_PI);
}
FN(f_degtorad) { UNUSED; return ar_real(ar_arg_real(argc, a, 0, 0) * M_PI / 180); }
FN(f_radtodeg) { UNUSED; return ar_real(ar_arg_real(argc, a, 0, 0) * 180 / M_PI); }

FN(f_min) {
    UNUSED;
    if (argc == 0) return ar_real(0);
    double m = ar_to_real(a[0]);
    for (int i = 1; i < argc; i++) {
        double x = ar_to_real(a[i]);
        if (x < m) m = x;
    }
    return ar_real(m);
}

FN(f_max) {
    UNUSED;
    if (argc == 0) return ar_real(0);
    double m = ar_to_real(a[0]);
    for (int i = 1; i < argc; i++) {
        double x = ar_to_real(a[i]);
        if (x > m) m = x;
    }
    return ar_real(m);
}

FN(f_mean) {
    UNUSED;
    double s = 0;
    for (int i = 0; i < argc; i++) s += ar_to_real(a[i]);
    return ar_real(argc ? s / argc : 0);
}

FN(f_clamp) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0), lo = ar_arg_real(argc, a, 1, 0), hi = ar_arg_real(argc, a, 2, 0);
    return ar_real(x < lo ? lo : x > hi ? hi : x);
}

FN(f_lerp) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0), y = ar_arg_real(argc, a, 1, 0), t = ar_arg_real(argc, a, 2, 0);
    return ar_real(x + (y - x) * t);
}

FN(f_point_distance) {
    UNUSED;
    double dx = ar_arg_real(argc, a, 2, 0) - ar_arg_real(argc, a, 0, 0);
    double dy = ar_arg_real(argc, a, 3, 0) - ar_arg_real(argc, a, 1, 0);
    return ar_real(sqrt(dx * dx + dy * dy));
}

static double direction_of(double dx, double dy) {
    if (dx == 0 && dy == 0) return 0;
    double d = atan2(-dy, dx) * 180 / M_PI;
    return d < 0 ? d + 360 : d;
}

FN(f_point_direction) {
    UNUSED;
    return ar_real(direction_of(ar_arg_real(argc, a, 2, 0) - ar_arg_real(argc, a, 0, 0),
                                ar_arg_real(argc, a, 3, 0) - ar_arg_real(argc, a, 1, 0)));
}

FN(f_lengthdir_x) {
    UNUSED;
    return ar_real(ar_arg_real(argc, a, 0, 0) * cos(ar_arg_real(argc, a, 1, 0) * M_PI / 180));
}

FN(f_lengthdir_y) {
    UNUSED;
    return ar_real(-ar_arg_real(argc, a, 0, 0) * sin(ar_arg_real(argc, a, 1, 0) * M_PI / 180));
}

FN(f_angle_difference) {
    UNUSED;
    double d = fmod(ar_arg_real(argc, a, 0, 0) - ar_arg_real(argc, a, 1, 0) + 180, 360);
    if (d < 0) d += 360;
    return ar_real(d - 180);
}

FN(f_random) { UNUSED; return ar_real(ar_run_random(RUN) * ar_arg_real(argc, a, 0, 1)); }
FN(f_random_range) {
    UNUSED;
    double lo = ar_arg_real(argc, a, 0, 0), hi = ar_arg_real(argc, a, 1, 1);
    return ar_real(lo + ar_run_random(RUN) * (hi - lo));
}
FN(f_irandom) {
    UNUSED;
    int n = ar_arg_int(argc, a, 0, 0);
    if (n < 0) return ar_real(-floor(ar_run_random(RUN) * (-n + 1)));
    return ar_real(floor(ar_run_random(RUN) * (n + 1)));
}
FN(f_irandom_range) {
    UNUSED;
    int lo = ar_arg_int(argc, a, 0, 0), hi = ar_arg_int(argc, a, 1, 0);
    if (hi < lo) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    return ar_real(lo + floor(ar_run_random(RUN) * (hi - lo + 1)));
}
FN(f_choose) {
    UNUSED;
    if (argc == 0) return ar_undefined();
    int i = (int)floor(ar_run_random(RUN) * argc);
    return a[i < argc ? i : argc - 1];
}
FN(f_randomize) {
    UNUSED;
    // ALPHARUNE_SEED makes each run the same, for the replays of the tools.
    const char *seed = getenv("ALPHARUNE_SEED");
    uint64_t base = seed ? (uint64_t)strtoull(seed, NULL, 10) : (uint64_t)ar_run_time_ms();
    RUN->random_state = base * 6364136223846793005ULL + 1442695040888963407ULL;
    if (RUN->random_state == 0) RUN->random_state = 1;
    return ar_undefined();
}
FN(f_random_set_seed) {
    UNUSED;
    RUN->random_state = (uint64_t)ar_arg_int(argc, a, 0, 0) * 6364136223846793005ULL + 1442695040888963407ULL;
    if (RUN->random_state == 0) RUN->random_state = 1;
    return ar_undefined();
}
FN(f_random_get_seed) { UNUSED; return ar_real((double)(RUN->random_state & 0x7FFFFFFF)); }

// ------------------------------------------------------------------ types

FN(f_is_string) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_STRING); }
FN(f_is_real) { UNUSED; return ar_bool(argc > 0 && (a[0].type == AR_REAL || a[0].type == AR_BOOL)); }
FN(f_is_numeric) { UNUSED; return ar_bool(argc > 0 && ar_is_number(a[0])); }
FN(f_is_undefined) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_UNDEFINED); }
FN(f_is_array) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_ARRAY); }
FN(f_is_struct) { UNUSED; return ar_bool(argc > 0 && (a[0].type == AR_STRUCT || a[0].type == AR_METHOD)); }
FN(f_is_method) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_METHOD); }
FN(f_is_bool) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_BOOL); }
FN(f_is_int64) { UNUSED; return ar_bool(argc > 0 && a[0].type == AR_INT64); }

FN(f_real) {
    UNUSED;
    if (argc == 0) return ar_real(0);
    if (a[0].type == AR_STRING) {
        const char *s = a[0].s->text;
        while (isspace((unsigned char)*s)) s++;
        return ar_real(strtod(s, NULL));
    }
    return ar_real(ar_to_real(a[0]));
}

FN(f_int64) { UNUSED; return ar_int64(argc ? ar_to_int64(a[0]) : 0); }
FN(f_bool) { UNUSED; return ar_bool(argc && ar_truthy(a[0])); }

FN(f_typeof) {
    UNUSED;
    if (argc == 0) return STR("undefined");
    static const char *names[] = {"undefined", "number", "bool", "int64", "string", "array", "struct", "method", "ptr"};
    return STR(names[a[0].type <= AR_PTR ? a[0].type : 0]);
}

// ---------------------------------------------------------------- strings

FN(f_string) {
    UNUSED;
    if (argc == 0) return STR("");
    return ar_strv(ar_to_str(HEAP, a[0]));
}

FN(f_string_length) {
    UNUSED;
    if (argc == 0 || a[0].type != AR_STRING) {
        if (argc && a[0].type != AR_UNDEFINED) return ar_real(ar_str_chars(ar_to_str(HEAP, a[0])));
        return ar_real(0);
    }
    return ar_real(ar_str_chars(a[0].s));
}

FN(f_string_byte_length) {
    UNUSED;
    return ar_real(ARG_STR(0)->length);
}

static ARValue substring(ARVM *vm, ARStr *s, int start, int count) {
    // start is 0-based, in characters.
    int n = ar_str_chars(s);
    if (start < 0) start = 0;
    if (start >= n || count <= 0) return STR("");
    if (start + count > n) count = n - start;
    size_t b0 = ar_str_offset(s, start), b1 = ar_str_offset(s, start + count);
    return ar_strv(ar_str_new(HEAP, s->text + b0, b1 - b0));
}

FN(f_string_char_at) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    int i = ar_arg_int(argc, a, 1, 1);
    if (i < 1) i = 1;
    return substring(vm, s, i - 1, 1);
}

FN(f_string_copy) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    int i = ar_arg_int(argc, a, 1, 1), n = ar_arg_int(argc, a, 2, 0);
    if (i < 1) i = 1;
    return substring(vm, s, i - 1, n);
}

FN(f_string_delete) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    int i = ar_arg_int(argc, a, 1, 1) - 1, n = ar_arg_int(argc, a, 2, 0);
    int len = ar_str_chars(s);
    if (i < 0) i = 0;
    if (i >= len || n <= 0) return ar_strv(s);
    if (i + n > len) n = len - i;
    size_t b0 = ar_str_offset(s, i), b1 = ar_str_offset(s, i + n);
    ARStr *head = ar_str_new(HEAP, s->text, b0);
    ARStr *tail = ar_str_new(HEAP, s->text + b1, s->length - b1);
    return ar_strv(ar_str_concat(HEAP, head, tail));
}

FN(f_string_insert) {
    UNUSED;
    ARStr *sub = ARG_STR(0), *s = ARG_STR(1);
    int i = ar_arg_int(argc, a, 2, 1) - 1;
    int len = ar_str_chars(s);
    if (i < 0) i = 0;
    if (i > len) i = len;
    size_t b = ar_str_offset(s, i);
    ARStr *head = ar_str_new(HEAP, s->text, b);
    ARStr *tail = ar_str_new(HEAP, s->text + b, s->length - b);
    return ar_strv(ar_str_concat(HEAP, ar_str_concat(HEAP, head, sub), tail));
}

static int char_index_of_byte(ARStr *s, size_t byte) {
    int n = 0;
    for (size_t i = 0; i < byte && i < s->length; i++) {
        if (((uint8_t)s->text[i] & 0xC0) != 0x80) n++;
    }
    return n;
}

FN(f_string_pos) {
    UNUSED;
    ARStr *sub = ARG_STR(0), *s = ARG_STR(1);
    if (sub->length == 0) return ar_real(0);
    const char *p = strstr(s->text, sub->text);
    if (!p) return ar_real(0);
    return ar_real(char_index_of_byte(s, (size_t)(p - s->text)) + 1);
}

FN(f_string_last_pos) {
    UNUSED;
    ARStr *sub = ARG_STR(0), *s = ARG_STR(1);
    if (sub->length == 0) return ar_real(0);
    const char *last = NULL, *p = s->text;
    while ((p = strstr(p, sub->text))) {
        last = p;
        p++;
    }
    if (!last) return ar_real(0);
    return ar_real(char_index_of_byte(s, (size_t)(last - s->text)) + 1);
}

static ARValue replace(ARVM *vm, ARStr *s, ARStr *from, ARStr *to, int all) {
    if (from->length == 0) return ar_strv(s);
    size_t cap = s->length + 16, len = 0;
    char *out = malloc(cap);
    const char *p = s->text;
    int done = 0;
    while (*p) {
        const char *hit = done && !all ? NULL : strstr(p, from->text);
        size_t keep = hit ? (size_t)(hit - p) : strlen(p);
        if (len + keep + to->length + 1 > cap) {
            cap = (len + keep + to->length + 1) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + len, p, keep);
        len += keep;
        if (!hit) break;
        memcpy(out + len, to->text, to->length);
        len += to->length;
        p = hit + from->length;
        done = 1;
    }
    ARStr *r = ar_str_new(HEAP, out, len);
    free(out);
    return ar_strv(r);
}

FN(f_string_replace_all) { UNUSED; return replace(vm, ARG_STR(0), ARG_STR(1), ARG_STR(2), 1); }
FN(f_string_replace) { UNUSED; return replace(vm, ARG_STR(0), ARG_STR(1), ARG_STR(2), 0); }

FN(f_string_count) {
    UNUSED;
    ARStr *sub = ARG_STR(0), *s = ARG_STR(1);
    if (sub->length == 0) return ar_real(0);
    int n = 0;
    const char *p = s->text;
    while ((p = strstr(p, sub->text))) {
        n++;
        p += sub->length;
    }
    return ar_real(n);
}

FN(f_string_upper) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    ARStr *r = ar_str_new(HEAP, s->text, s->length);
    for (uint32_t i = 0; i < r->length; i++) r->text[i] = (char)toupper((unsigned char)r->text[i]);
    return ar_strv(r);
}

FN(f_string_lower) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    ARStr *r = ar_str_new(HEAP, s->text, s->length);
    for (uint32_t i = 0; i < r->length; i++) r->text[i] = (char)tolower((unsigned char)r->text[i]);
    return ar_strv(r);
}

FN(f_string_repeat) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    int n = ar_arg_int(argc, a, 1, 0);
    ARStr *r = ar_str_new(HEAP, "", 0);
    for (int i = 0; i < n && i < 100000; i++) r = ar_str_concat(HEAP, r, s);
    return ar_strv(r);
}

static ARValue filter(ARVM *vm, ARStr *s, int (*keep)(int)) {
    char *out = malloc(s->length + 1);
    size_t n = 0;
    for (uint32_t i = 0; i < s->length; i++) {
        if (keep((unsigned char)s->text[i])) out[n++] = s->text[i];
    }
    ARStr *r = ar_str_new(HEAP, out, n);
    free(out);
    return ar_strv(r);
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int is_letter(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_letter_or_digit(int c) {
    return is_digit(c) || is_letter(c);
}

FN(f_string_digits) { UNUSED; return filter(vm, ARG_STR(0), is_digit); }
FN(f_string_letters) { UNUSED; return filter(vm, ARG_STR(0), is_letter); }
FN(f_string_lettersdigits) { UNUSED; return filter(vm, ARG_STR(0), is_letter_or_digit); }

FN(f_string_hash_to_newline) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    if (!memchr(s->text, '#', s->length)) return ar_strv(s);
    char *out = malloc(s->length + 1);
    size_t n = 0;
    for (uint32_t i = 0; i < s->length; i++) {
        if (s->text[i] == '\\' && i + 1 < s->length && s->text[i + 1] == '#') {
            out[n++] = '#';
            i++;
        } else if (s->text[i] == '#') {
            out[n++] = '\n';
        } else {
            out[n++] = s->text[i];
        }
    }
    ARStr *r = ar_str_new(HEAP, out, n);
    free(out);
    return ar_strv(r);
}

FN(f_string_split) {
    UNUSED;
    ARStr *s = ARG_STR(0), *d = ARG_STR(1);
    int remove_empty = argc > 2 && ar_truthy(a[2]);
    ARArray *arr = ar_array_new(HEAP, 0);
    if (d->length == 0) {
        ar_array_set(arr, 0, ar_strv(s));
        return ar_arrayv(arr);
    }
    const char *p = s->text;
    while (1) {
        const char *hit = strstr(p, d->text);
        size_t n = hit ? (size_t)(hit - p) : strlen(p);
        if (n > 0 || !remove_empty) ar_array_set(arr, arr->length, ar_strv(ar_str_new(HEAP, p, n)));
        if (!hit) break;
        p = hit + d->length;
    }
    return ar_arrayv(arr);
}

FN(f_chr) {
    UNUSED;
    uint32_t c = (uint32_t)ar_arg_int(argc, a, 0, 0);
    char out[5];
    size_t n;
    if (c < 0x80) {
        out[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        out[0] = (char)(0xC0 | c >> 6);
        out[1] = (char)(0x80 | (c & 63));
        n = 2;
    } else if (c < 0x10000) {
        out[0] = (char)(0xE0 | c >> 12);
        out[1] = (char)(0x80 | ((c >> 6) & 63));
        out[2] = (char)(0x80 | (c & 63));
        n = 3;
    } else {
        out[0] = (char)(0xF0 | c >> 18);
        out[1] = (char)(0x80 | ((c >> 12) & 63));
        out[2] = (char)(0x80 | ((c >> 6) & 63));
        out[3] = (char)(0x80 | (c & 63));
        n = 4;
    }
    return ar_strv(ar_str_new(HEAP, out, n));
}

FN(f_ord) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    if (s->length == 0) return ar_real(0);
    const uint8_t *p = (const uint8_t *)s->text;
    uint32_t c = p[0];
    if (c >= 0xF0 && s->length >= 4) c = (c & 7) << 18 | (p[1] & 63) << 12 | (p[2] & 63) << 6 | (p[3] & 63);
    else if (c >= 0xE0 && s->length >= 3) c = (c & 15) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
    else if (c >= 0xC0 && s->length >= 2) c = (c & 31) << 6 | (p[1] & 63);
    return ar_real(c);
}

FN(f_string_format) {
    UNUSED;
    double x = ar_arg_real(argc, a, 0, 0);
    int tot = ar_arg_int(argc, a, 1, 0), dec = ar_arg_int(argc, a, 2, 0);
    char out[128];
    snprintf(out, sizeof(out), "%*.*f", tot + (dec > 0 ? dec + 1 : 0), dec, x);
    return STR(out);
}

// ----------------------------------------------------------------- arrays

FN(f_array_create) {
    UNUSED;
    int n = ar_arg_int(argc, a, 0, 0);
    ARArray *arr = ar_array_new(HEAP, n > 0 ? n : 0);
    ARValue fill = argc > 1 ? a[1] : ar_real(0);
    for (int i = 0; i < arr->length; i++) arr->items[i] = fill;
    return ar_arrayv(arr);
}

FN(f_array_length) {
    UNUSED;
    if (argc == 0 || a[0].type != AR_ARRAY) return ar_real(0);
    return ar_real(a[0].a->length);
}

FN(f_array_length_2d) {
    UNUSED;
    if (argc < 2 || a[0].type != AR_ARRAY) return ar_real(0);
    ARValue row = ar_array_get(a[0].a, ar_to_int(a[1]));
    return ar_real(row.type == AR_ARRAY ? row.a->length : 0);
}

FN(f_array_push) {
    UNUSED;
    if (argc == 0 || a[0].type != AR_ARRAY) return ar_undefined();
    for (int i = 1; i < argc; i++) ar_array_set(a[0].a, a[0].a->length, a[i]);
    return ar_undefined();
}

FN(f_array_pop) {
    UNUSED;
    if (argc == 0 || a[0].type != AR_ARRAY || a[0].a->length == 0) return ar_undefined();
    ARValue v = a[0].a->items[a[0].a->length - 1];
    ar_array_resize(a[0].a, a[0].a->length - 1);
    return v;
}

FN(f_array_insert) {
    UNUSED;
    if (argc < 2 || a[0].type != AR_ARRAY) return ar_undefined();
    ARArray *arr = a[0].a;
    int i = ar_to_int(a[1]);
    int n = argc - 2;
    if (i < 0) i = 0;
    if (i > arr->length) ar_array_resize(arr, i);
    int old = arr->length;
    ar_array_resize(arr, old + n);
    memmove(&arr->items[i + n], &arr->items[i], sizeof(ARValue) * (size_t)(old - i));
    for (int k = 0; k < n; k++) arr->items[i + k] = a[2 + k];
    return ar_undefined();
}

FN(f_array_delete) {
    UNUSED;
    if (argc < 3 || a[0].type != AR_ARRAY) return ar_undefined();
    ARArray *arr = a[0].a;
    int i = ar_to_int(a[1]), n = ar_to_int(a[2]);
    if (i < 0 || i >= arr->length || n <= 0) return ar_undefined();
    if (i + n > arr->length) n = arr->length - i;
    memmove(&arr->items[i], &arr->items[i + n], sizeof(ARValue) * (size_t)(arr->length - i - n));
    ar_array_resize(arr, arr->length - n);
    return ar_undefined();
}

FN(f_array_resize) {
    UNUSED;
    if (argc >= 2 && a[0].type == AR_ARRAY) ar_array_resize(a[0].a, ar_to_int(a[1]));
    return ar_undefined();
}

FN(f_array_copy) {
    UNUSED;
    if (argc < 5 || a[0].type != AR_ARRAY || a[2].type != AR_ARRAY) return ar_undefined();
    ARArray *dst = a[0].a, *src = a[2].a;
    int di = ar_to_int(a[1]), si = ar_to_int(a[3]), n = ar_to_int(a[4]);
    ARValue *tmp = malloc(sizeof(ARValue) * (size_t)(n > 0 ? n : 1));
    for (int i = 0; i < n; i++) tmp[i] = ar_array_get(src, si + i);
    for (int i = 0; i < n; i++) ar_array_set(dst, di + i, tmp[i]);
    free(tmp);
    return ar_undefined();
}

FN(f_array_get) {
    UNUSED;
    if (argc < 2 || a[0].type != AR_ARRAY) return ar_undefined();
    return ar_array_get(a[0].a, ar_to_int(a[1]));
}

FN(f_array_set) {
    UNUSED;
    if (argc >= 3 && a[0].type == AR_ARRAY) ar_array_set(a[0].a, ar_to_int(a[1]), a[2]);
    return ar_undefined();
}

FN(f_array_equals) {
    UNUSED;
    if (argc < 2 || a[0].type != AR_ARRAY || a[1].type != AR_ARRAY) return ar_bool(0);
    if (a[0].a->length != a[1].a->length) return ar_bool(0);
    for (int i = 0; i < a[0].a->length; i++) {
        if (!ar_equal(a[0].a->items[i], a[1].a->items[i])) return ar_bool(0);
    }
    return ar_bool(1);
}

// ------------------------------------------------------ structs and vars

static ARStruct *struct_arg(ARVM *vm, ARValue v) {
    if (v.type == AR_STRUCT) return v.o;
    ARInstance *inst = ar_run_instance(RUN, v);
    if (inst) return inst->vars;
    if (ar_is_number(v) && ar_to_int(v) == AR_GLOBAL) return ar_vm_global(vm);
    return NULL;
}

static int32_t name_slot(ARVM *vm, int argc, ARValue *a, int i) {
    if (i >= argc) return -1;
    return ar_vm_slot(vm, ar_to_str(HEAP, a[i])->text);
}

FN(f_variable_global_exists) {
    UNUSED;
    int32_t slot = name_slot(vm, argc, a, 0);
    return ar_bool(slot >= 0 && ar_vars_find(&ar_vm_global(vm)->vars, slot) != NULL);
}

FN(f_variable_global_get) {
    UNUSED;
    int32_t slot = name_slot(vm, argc, a, 0);
    ARValue *v = slot >= 0 ? ar_vars_find(&ar_vm_global(vm)->vars, slot) : NULL;
    return v ? *v : ar_undefined();
}

FN(f_variable_global_set) {
    UNUSED;
    int32_t slot = name_slot(vm, argc, a, 0);
    if (slot >= 0 && argc > 1) *ar_vars_slot(&ar_vm_global(vm)->vars, slot) = a[1];
    return ar_undefined();
}

FN(f_variable_instance_exists) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    int32_t slot = name_slot(vm, argc, a, 1);
    return ar_bool(s && slot >= 0 && ar_vars_find(&s->vars, slot) != NULL);
}

FN(f_variable_instance_get) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    int32_t slot = name_slot(vm, argc, a, 1);
    ARValue *v = s && slot >= 0 ? ar_vars_find(&s->vars, slot) : NULL;
    return v ? *v : ar_undefined();
}

FN(f_variable_instance_set) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    int32_t slot = name_slot(vm, argc, a, 1);
    if (s && slot >= 0 && argc > 2) *ar_vars_slot(&s->vars, slot) = a[2];
    return ar_undefined();
}

FN(f_variable_struct_remove) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    int32_t slot = name_slot(vm, argc, a, 1);
    if (s && slot >= 0) ar_vars_remove(&s->vars, slot);
    return ar_undefined();
}

FN(f_variable_struct_names_count) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    return ar_real(s ? s->vars.count : 0);
}

FN(f_variable_struct_get_names) {
    UNUSED;
    ARStruct *s = argc > 0 ? struct_arg(vm, a[0]) : NULL;
    ARArray *arr = ar_array_new(HEAP, 0);
    if (!s) return ar_arrayv(arr);
    for (int i = 0; i < s->vars.capacity; i++) {
        if (s->vars.keys[i] >= 0) ar_array_set(arr, arr->length, STR(ar_vm_slot_name(vm, s->vars.keys[i])));
    }
    return ar_arrayv(arr);
}

// ---------------------------------------------------------------- scripts

FN(f_script_execute) {
    UNUSED;
    if (argc == 0) return ar_undefined();
    return ar_vm_call(vm, a[0], ar_vm_self(vm), ar_vm_other(vm), argc - 1, a + 1);
}

FN(f_script_exists) {
    UNUSED;
    int n = ar_arg_int(argc, a, 0, -1);
    return ar_bool(n >= 0 && n < ar_vm_data(vm)->script_count);
}

FN(f_method_get_index) {
    UNUSED;
    if (argc && a[0].type == AR_METHOD) return ar_real(a[0].m->function);
    return ar_undefined();
}

FN(f_method_get_self) {
    UNUSED;
    if (argc && a[0].type == AR_METHOD) return a[0].m->self;
    return ar_undefined();
}

FN(f_asset_get_index) {
    UNUSED;
    const char *name = ARG_STR(0)->text;
    const ARData *d = ar_vm_data(vm);
    int i;
    if ((i = ar_data_find_object(d, name)) >= 0) return ar_real(i);
    if ((i = ar_data_find_sprite(d, name)) >= 0) return ar_real(i);
    if ((i = ar_data_find_room(d, name)) >= 0) return ar_real(i);
    for (i = 0; i < d->sound_count; i++) {
        if (d->sounds[i].name && strcmp(d->sounds[i].name, name) == 0) return ar_real(i);
    }
    for (i = 0; i < d->font_count; i++) {
        if (d->fonts[i].name && strcmp(d->fonts[i].name, name) == 0) return ar_real(i);
    }
    if ((i = ar_vm_script(vm, name)) >= 0) return ar_real(i);
    return ar_real(-1);
}

FN(f_show_debug_message) {
    UNUSED;
    if (argc) fprintf(stderr, "debug: %s\n", ar_to_str(HEAP, a[0])->text);
    return ar_undefined();
}

FN(f_show_error) {
    UNUSED;
    if (argc) fprintf(stderr, "error: %s\n", ar_to_str(HEAP, a[0])->text);
    return ar_undefined();
}

FN(f_event_user) {
    UNUSED;
    ARInstance *self = ar_vm_self_instance(vm);
    if (self) ar_run_event(RUN, self, AR_EV_OTHER, AR_OTHER_USER0 + ar_arg_int(argc, a, 0, 0));
    return ar_undefined();
}

FN(f_event_perform) {
    UNUSED;
    ARInstance *self = ar_vm_self_instance(vm);
    if (self) ar_run_event(RUN, self, ar_arg_int(argc, a, 0, 0), ar_arg_int(argc, a, 1, 0));
    return ar_undefined();
}

FN(f_event_inherited) {
    UNUSED;
    ar_run_event_inherited(RUN);
    return ar_undefined();
}

FN(f_get_instance) {
    UNUSED;
    // @@GetInstance@@: the value of an instance id.
    return argc ? a[0] : ar_undefined();
}

FN(f_null_object) {
    UNUSED;
    return ar_undefined();
}

// -------------------------------------------------------------------- ds

static int ds_new(ARRun *run, int type) {
    ARDs *ds = calloc(1, sizeof(ARDs));
    ds->type = type;
    // An id that a destroyed structure left is used again, as in GameMaker.
    for (int i = 0; i < run->ds_count; i++) {
        if (!run->ds[i]) {
            run->ds[i] = ds;
            return i;
        }
    }
    if (run->ds_count == run->ds_capacity) {
        run->ds_capacity = run->ds_capacity ? run->ds_capacity * 2 : 64;
        run->ds = realloc(run->ds, sizeof(ARDs *) * (size_t)run->ds_capacity);
    }
    run->ds[run->ds_count] = ds;
    return run->ds_count++;
}

static ARDs *ds_get(ARVM *vm, ARValue id, int type) {
    ARRun *run = RUN;
    int i = ar_to_int(id);
    if (i < 0 || i >= run->ds_count || !run->ds[i]) return NULL;
    // The ids of each type share one space here, so a map id cannot be a
    // list id. The type check stops a wrong use.
    if (type && run->ds[i]->type != type) return NULL;
    return run->ds[i];
}

static void ds_free(ARRun *run, int i) {
    if (i < 0 || i >= run->ds_count || !run->ds[i]) return;
    ARDs *ds = run->ds[i];
    free(ds->items);
    free(ds->entries);
    free(ds->index);
    free(ds);
    run->ds[i] = NULL;
}

static void items_reserve(ARDs *ds, int n) {
    if (n <= ds->capacity) return;
    int cap = ds->capacity ? ds->capacity : 8;
    while (cap < n) cap *= 2;
    ds->items = realloc(ds->items, sizeof(ARValue) * (size_t)cap);
    ds->capacity = cap;
}

FN(f_ds_list_create) { UNUSED; return ar_real(ds_new(RUN, AR_DS_LIST)); }
FN(f_ds_list_destroy) {
    UNUSED;
    if (ds_get(vm, argc ? a[0] : ar_real(-1), AR_DS_LIST)) ds_free(RUN, ar_to_int(a[0]));
    return ar_undefined();
}
FN(f_ds_list_add) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    if (!ds) return ar_undefined();
    items_reserve(ds, ds->count + argc - 1);
    for (int i = 1; i < argc; i++) ds->items[ds->count++] = a[i];
    return ar_undefined();
}
FN(f_ds_list_size) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    return ar_real(ds ? ds->count : 0);
}
FN(f_ds_list_find_value) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    int i = ar_arg_int(argc, a, 1, 0);
    if (!ds || i < 0 || i >= ds->count) return ar_undefined();
    return ds->items[i];
}
FN(f_ds_list_set) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    int i = ar_arg_int(argc, a, 1, 0);
    if (!ds || i < 0 || argc < 3) return ar_undefined();
    if (i >= ds->count) {
        items_reserve(ds, i + 1);
        for (int k = ds->count; k <= i; k++) ds->items[k] = ar_real(0);
        ds->count = i + 1;
    }
    ds->items[i] = a[2];
    return ar_undefined();
}
FN(f_ds_list_delete) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    int i = ar_arg_int(argc, a, 1, 0);
    if (!ds || i < 0 || i >= ds->count) return ar_undefined();
    memmove(&ds->items[i], &ds->items[i + 1], sizeof(ARValue) * (size_t)(ds->count - i - 1));
    ds->count--;
    return ar_undefined();
}
FN(f_ds_list_insert) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    int i = ar_arg_int(argc, a, 1, 0);
    if (!ds || i < 0 || argc < 3) return ar_undefined();
    if (i > ds->count) i = ds->count;
    items_reserve(ds, ds->count + 1);
    memmove(&ds->items[i + 1], &ds->items[i], sizeof(ARValue) * (size_t)(ds->count - i));
    ds->items[i] = a[2];
    ds->count++;
    return ar_undefined();
}
FN(f_ds_list_clear) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    if (ds) ds->count = 0;
    return ar_undefined();
}
FN(f_ds_list_find_index) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    if (!ds || argc < 2) return ar_real(-1);
    for (int i = 0; i < ds->count; i++) {
        if (ar_equal(ds->items[i], a[1])) return ar_real(i);
    }
    return ar_real(-1);
}
FN(f_ds_list_empty) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    return ar_bool(!ds || ds->count == 0);
}

// The text form of ds_list_write: the bytes in hex, upper case.
static void hex_append(char **out, size_t *len, size_t *cap, const void *bytes, size_t n) {
    static const char digits[] = "0123456789ABCDEF";
    if (*len + n * 2 + 1 > *cap) {
        *cap = (*len + n * 2 + 1) * 2;
        *out = realloc(*out, *cap);
    }
    const uint8_t *b = bytes;
    for (size_t i = 0; i < n; i++) {
        (*out)[(*len)++] = digits[b[i] >> 4];
        (*out)[(*len)++] = digits[b[i] & 15];
    }
    (*out)[*len] = 0;
}

static void hex_u32(char **out, size_t *len, size_t *cap, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    hex_append(out, len, cap, b, 4);
}

static void hex_value(ARVM *vm, char **out, size_t *len, size_t *cap, ARValue v) {
    if (v.type == AR_STRING) {
        hex_u32(out, len, cap, 1);
        hex_u32(out, len, cap, v.s->length);
        hex_append(out, len, cap, v.s->text, v.s->length);
    } else if (v.type == AR_UNDEFINED) {
        hex_u32(out, len, cap, 5);
        double d = 0;
        hex_append(out, len, cap, &d, 8);
    } else {
        hex_u32(out, len, cap, 0);
        double d = ar_to_real(v);
        hex_append(out, len, cap, &d, 8);
    }
    (void)vm;
}

FN(f_ds_list_write) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    if (!ds) return STR("");
    char *out = NULL;
    size_t len = 0, cap = 0;
    hex_u32(&out, &len, &cap, 0x12E);
    hex_u32(&out, &len, &cap, (uint32_t)ds->count);
    for (int i = 0; i < ds->count; i++) hex_value(vm, &out, &len, &cap, ds->items[i]);
    ARValue r = STR(out ? out : "");
    free(out);
    return r;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

typedef struct {
    const char *p;
    const char *end;
    int bad;
} HexReader;

static int hex_read(HexReader *r, void *out, size_t n) {
    uint8_t *b = out;
    for (size_t i = 0; i < n; i++) {
        if (r->end - r->p < 2) {
            r->bad = 1;
            return 0;
        }
        int hi = hex_digit(r->p[0]), lo = hex_digit(r->p[1]);
        if (hi < 0 || lo < 0) {
            r->bad = 1;
            return 0;
        }
        b[i] = (uint8_t)(hi << 4 | lo);
        r->p += 2;
    }
    return 1;
}

static uint32_t hex_read_u32(HexReader *r) {
    uint8_t b[4] = {0};
    hex_read(r, b, 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static ARValue hex_read_value(ARVM *vm, HexReader *r) {
    uint32_t type = hex_read_u32(r);
    if (type == 1) {
        uint32_t n = hex_read_u32(r);
        if (n > (uint32_t)(r->end - r->p) / 2) {
            r->bad = 1;
            return ar_undefined();
        }
        char *text = malloc(n + 1);
        hex_read(r, text, n);
        ARValue v = ar_strv(ar_str_new(HEAP, text, n));
        free(text);
        return v;
    }
    double d = 0;
    hex_read(r, &d, 8);
    if (type == 5) return ar_undefined();
    if (type == 2) {
        int32_t i;
        memcpy(&i, &d, 4);
        return ar_real(i);
    }
    return ar_real(d);
}

FN(f_ds_list_read) {
    UNUSED;
    ARDs *ds = argc ? ds_get(vm, a[0], AR_DS_LIST) : NULL;
    if (!ds || argc < 2) return ar_undefined();
    ARStr *s = ar_to_str(HEAP, a[1]);
    HexReader r = {s->text, s->text + s->length, 0};
    uint32_t magic = hex_read_u32(&r);
    if (magic != 0x12E && magic != 0x12F) return ar_undefined();
    uint32_t n = hex_read_u32(&r);
    ds->count = 0;
    for (uint32_t i = 0; i < n && !r.bad; i++) {
        ARValue v = hex_read_value(vm, &r);
        if (r.bad) break;
        items_reserve(ds, ds->count + 1);
        ds->items[ds->count++] = v;
    }
    return ar_undefined();
}

// A map keeps its entries in the order of insertion and an open hash of
// their indices.
static uint32_t value_hash(ARValue k) {
    if (k.type == AR_STRING) {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < k.s->length; i++) h = (h ^ (uint8_t)k.s->text[i]) * 16777619u;
        return h;
    }
    double d = ar_to_real(k);
    if (d == 0) d = 0;  // -0 and 0 are one key.
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return (uint32_t)(bits ^ (bits >> 29)) * 2654435761u;
}

static int key_equal(ARValue a, ARValue b) {
    if (a.type == AR_STRING || b.type == AR_STRING) {
        return a.type == AR_STRING && b.type == AR_STRING && a.s->length == b.s->length &&
               memcmp(a.s->text, b.s->text, a.s->length) == 0;
    }
    if (a.type == AR_UNDEFINED || b.type == AR_UNDEFINED) return a.type == b.type;
    return ar_to_real(a) == ar_to_real(b);
}

static int map_find(ARDs *m, ARValue key) {
    if (m->index_capacity == 0) return -1;
    uint32_t mask = (uint32_t)m->index_capacity - 1;
    for (uint32_t i = value_hash(key) & mask;; i = (i + 1) & mask) {
        int e = m->index[i];
        if (e == -1) return -1;
        if (e >= 0 && key_equal(m->entries[e].key, key)) return e;
    }
}

static void map_rehash(ARDs *m) {
    // Compact the entries, then make the index again.
    int n = 0;
    for (int i = 0; i < m->entry_count; i++) {
        if (m->entries[i].key.type != AR_UNDEFINED) m->entries[n++] = m->entries[i];
    }
    m->entry_count = n;
    int cap = 16;
    while (cap < n * 2 + 2) cap *= 2;
    free(m->index);
    m->index = malloc(sizeof(int) * (size_t)cap);
    memset(m->index, 0xFF, sizeof(int) * (size_t)cap);
    m->index_capacity = cap;
    uint32_t mask = (uint32_t)cap - 1;
    for (int e = 0; e < n; e++) {
        uint32_t i = value_hash(m->entries[e].key) & mask;
        while (m->index[i] != -1) i = (i + 1) & mask;
        m->index[i] = e;
    }
}

static void map_set(ARDs *m, ARValue key, ARValue value, int replace_only_new) {
    int e = map_find(m, key);
    if (e >= 0) {
        if (!replace_only_new) m->entries[e].value = value;
        return;
    }
    if (m->entry_count == m->entry_capacity) {
        m->entry_capacity = m->entry_capacity ? m->entry_capacity * 2 : 16;
        m->entries = realloc(m->entries, sizeof(ARMapEntry) * (size_t)m->entry_capacity);
    }
    m->entries[m->entry_count].key = key;
    m->entries[m->entry_count].value = value;
    m->entry_count++;
    m->live++;
    if (m->entry_count * 2 + 2 > m->index_capacity) {
        map_rehash(m);
        return;
    }
    uint32_t mask = (uint32_t)m->index_capacity - 1;
    uint32_t i = value_hash(key) & mask;
    while (m->index[i] >= 0) i = (i + 1) & mask;
    m->index[i] = m->entry_count - 1;
}

static void map_delete(ARDs *m, ARValue key) {
    int e = map_find(m, key);
    if (e < 0) return;
    m->entries[e].key = ar_undefined();
    m->entries[e].value = ar_undefined();
    m->live--;
    map_rehash(m);
}

FN(f_ds_map_create) { UNUSED; return ar_real(ds_new(RUN, AR_DS_MAP)); }
FN(f_ds_map_destroy) {
    UNUSED;
    if (argc && ds_get(vm, a[0], AR_DS_MAP)) ds_free(RUN, ar_to_int(a[0]));
    return ar_undefined();
}
FN(f_ds_map_add) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (m && argc >= 3) map_set(m, a[1], a[2], 1);
    return ar_bool(m != NULL);
}
FN(f_ds_map_set) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (m && argc >= 3) map_set(m, a[1], a[2], 0);
    return ar_undefined();
}
FN(f_ds_map_find_value) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (!m || argc < 2) return ar_undefined();
    int e = map_find(m, a[1]);
    return e >= 0 ? m->entries[e].value : ar_undefined();
}
FN(f_ds_map_exists) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    return ar_bool(m && argc >= 2 && map_find(m, a[1]) >= 0);
}
FN(f_ds_map_delete) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (m && argc >= 2) map_delete(m, a[1]);
    return ar_undefined();
}
FN(f_ds_map_size) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    return ar_real(m ? m->live : 0);
}
FN(f_ds_map_clear) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (m) {
        m->entry_count = 0;
        m->live = 0;
        map_rehash(m);
    }
    return ar_undefined();
}
FN(f_ds_map_find_first) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (!m) return ar_undefined();
    for (int i = 0; i < m->entry_count; i++) {
        if (m->entries[i].key.type != AR_UNDEFINED) return m->entries[i].key;
    }
    return ar_undefined();
}
FN(f_ds_map_find_next) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    if (!m || argc < 2) return ar_undefined();
    int e = map_find(m, a[1]);
    if (e < 0) return ar_undefined();
    for (int i = e + 1; i < m->entry_count; i++) {
        if (m->entries[i].key.type != AR_UNDEFINED) return m->entries[i].key;
    }
    return ar_undefined();
}
FN(f_ds_map_keys_to_array) {
    UNUSED;
    ARDs *m = argc ? ds_get(vm, a[0], AR_DS_MAP) : NULL;
    ARArray *arr = ar_array_new(HEAP, 0);
    if (!m) return ar_arrayv(arr);
    for (int i = 0; i < m->entry_count; i++) {
        if (m->entries[i].key.type != AR_UNDEFINED) ar_array_set(arr, arr->length, m->entries[i].key);
    }
    return ar_arrayv(arr);
}

FN(f_ds_exists) {
    UNUSED;
    int type = ar_arg_int(argc, a, 1, 0);
    // The ds_type constants: map 1, list 2, stack 3, grid 4, queue 5, priority 6.
    static const int kinds[] = {0, AR_DS_MAP, AR_DS_LIST, AR_DS_STACK, AR_DS_GRID, AR_DS_QUEUE, AR_DS_PRIORITY};
    int kind = type >= 0 && type <= 6 ? kinds[type] : -1;
    return ar_bool(argc > 0 && kind > 0 && ds_get(vm, a[0], kind) != NULL);
}

// A grid.
FN(f_ds_grid_create) {
    UNUSED;
    int id = ds_new(RUN, AR_DS_GRID);
    ARDs *g = RUN->ds[id];
    g->width = ar_arg_int(argc, a, 0, 1);
    g->height = ar_arg_int(argc, a, 1, 1);
    if (g->width < 1) g->width = 1;
    if (g->height < 1) g->height = 1;
    items_reserve(g, g->width * g->height);
    g->count = g->width * g->height;
    for (int i = 0; i < g->count; i++) g->items[i] = ar_real(0);
    return ar_real(id);
}
FN(f_ds_grid_destroy) {
    UNUSED;
    if (argc && ds_get(vm, a[0], AR_DS_GRID)) ds_free(RUN, ar_to_int(a[0]));
    return ar_undefined();
}
FN(f_ds_grid_get) {
    UNUSED;
    ARDs *g = argc ? ds_get(vm, a[0], AR_DS_GRID) : NULL;
    int x = ar_arg_int(argc, a, 1, 0), y = ar_arg_int(argc, a, 2, 0);
    if (!g || x < 0 || y < 0 || x >= g->width || y >= g->height) return ar_undefined();
    return g->items[y * g->width + x];
}
FN(f_ds_grid_set) {
    UNUSED;
    ARDs *g = argc ? ds_get(vm, a[0], AR_DS_GRID) : NULL;
    int x = ar_arg_int(argc, a, 1, 0), y = ar_arg_int(argc, a, 2, 0);
    if (g && argc > 3 && x >= 0 && y >= 0 && x < g->width && y < g->height) g->items[y * g->width + x] = a[3];
    return ar_undefined();
}
FN(f_ds_grid_width) {
    UNUSED;
    ARDs *g = argc ? ds_get(vm, a[0], AR_DS_GRID) : NULL;
    return ar_real(g ? g->width : 0);
}
FN(f_ds_grid_height) {
    UNUSED;
    ARDs *g = argc ? ds_get(vm, a[0], AR_DS_GRID) : NULL;
    return ar_real(g ? g->height : 0);
}

// A stack and a queue.
FN(f_ds_stack_create) { UNUSED; return ar_real(ds_new(RUN, AR_DS_STACK)); }
FN(f_ds_queue_create) { UNUSED; return ar_real(ds_new(RUN, AR_DS_QUEUE)); }
FN(f_ds_stack_push) {
    UNUSED;
    ARDs *s = argc ? ds_get(vm, a[0], 0) : NULL;
    if (!s) return ar_undefined();
    items_reserve(s, s->count + argc - 1);
    for (int i = 1; i < argc; i++) s->items[s->count++] = a[i];
    return ar_undefined();
}
FN(f_ds_stack_pop) {
    UNUSED;
    ARDs *s = argc ? ds_get(vm, a[0], AR_DS_STACK) : NULL;
    if (!s || s->count == 0) return ar_undefined();
    return s->items[--s->count];
}
FN(f_ds_queue_dequeue) {
    UNUSED;
    ARDs *q = argc ? ds_get(vm, a[0], AR_DS_QUEUE) : NULL;
    if (!q || q->count == 0) return ar_undefined();
    ARValue v = q->items[0];
    memmove(&q->items[0], &q->items[1], sizeof(ARValue) * (size_t)(q->count - 1));
    q->count--;
    return v;
}
FN(f_ds_size_any) {
    UNUSED;
    ARDs *s = argc ? ds_get(vm, a[0], 0) : NULL;
    return ar_real(s ? s->count : 0);
}
FN(f_ds_destroy_any) {
    UNUSED;
    if (argc && ds_get(vm, a[0], 0)) ds_free(RUN, ar_to_int(a[0]));
    return ar_undefined();
}

// ------------------------------------------------------------------- json

typedef struct {
    const char *p;
    ARVM *vm;
    int bad;
} Json;

static void json_space(Json *j) {
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
}

static ARValue json_value(Json *j);

static ARValue json_string(Json *j) {
    ARVM *vm = j->vm;
    j->p++;
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    while (*j->p && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && *j->p) {
            char e = *j->p++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else if (e == 'u') {
                unsigned code = 0;
                for (int i = 0; i < 4 && *j->p; i++) {
                    int d = hex_digit(*j->p++);
                    code = code * 16 + (unsigned)(d < 0 ? 0 : d);
                }
                char buf[4];
                size_t n;
                if (code < 0x80) {
                    buf[0] = (char)code;
                    n = 1;
                } else if (code < 0x800) {
                    buf[0] = (char)(0xC0 | code >> 6);
                    buf[1] = (char)(0x80 | (code & 63));
                    n = 2;
                } else {
                    buf[0] = (char)(0xE0 | code >> 12);
                    buf[1] = (char)(0x80 | ((code >> 6) & 63));
                    buf[2] = (char)(0x80 | (code & 63));
                    n = 3;
                }
                if (len + n + 1 > cap) {
                    cap = (len + n + 1) * 2;
                    out = realloc(out, cap);
                }
                memcpy(out + len, buf, n);
                len += n;
                continue;
            } else c = e;
        }
        if (len + 2 > cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[len++] = c;
    }
    if (*j->p == '"') j->p++;
    else j->bad = 1;
    ARValue v = ar_strv(ar_str_new(HEAP, out, len));
    free(out);
    return v;
}

static ARValue json_value(Json *j) {
    ARVM *vm = j->vm;
    json_space(j);
    char c = *j->p;
    if (c == '{') {
        j->p++;
        int id = ds_new(RUN, AR_DS_MAP);
        json_space(j);
        if (*j->p == '}') {
            j->p++;
            return ar_real(id);
        }
        while (!j->bad) {
            json_space(j);
            if (*j->p != '"') {
                j->bad = 1;
                break;
            }
            ARValue key = json_string(j);
            json_space(j);
            if (*j->p != ':') {
                j->bad = 1;
                break;
            }
            j->p++;
            ARValue value = json_value(j);
            // The runtime may make new structures, so read the map again.
            map_set(RUN->ds[id], key, value, 0);
            json_space(j);
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            if (*j->p == '}') {
                j->p++;
                break;
            }
            j->bad = 1;
        }
        return ar_real(id);
    }
    if (c == '[') {
        j->p++;
        int id = ds_new(RUN, AR_DS_LIST);
        json_space(j);
        if (*j->p == ']') {
            j->p++;
            return ar_real(id);
        }
        while (!j->bad) {
            ARValue value = json_value(j);
            ARDs *l = RUN->ds[id];
            items_reserve(l, l->count + 1);
            l->items[l->count++] = value;
            json_space(j);
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            if (*j->p == ']') {
                j->p++;
                break;
            }
            j->bad = 1;
        }
        return ar_real(id);
    }
    if (c == '"') return json_string(j);
    if (strncmp(j->p, "true", 4) == 0) {
        j->p += 4;
        return ar_bool(1);
    }
    if (strncmp(j->p, "false", 5) == 0) {
        j->p += 5;
        return ar_bool(0);
    }
    if (strncmp(j->p, "null", 4) == 0) {
        j->p += 4;
        return ar_undefined();
    }
    char *end;
    double d = strtod(j->p, &end);
    if (end == j->p) {
        j->bad = 1;
        return ar_undefined();
    }
    j->p = end;
    return ar_real(d);
}

FN(f_json_decode) {
    UNUSED;
    ARStr *s = ARG_STR(0);
    Json j = {s->text, vm, 0};
    json_space(&j);
    ARValue v = json_value(&j);
    if (j.bad) return ar_real(-1);
    if (ar_is_number(v) && *s->text != '{') {
        // A list or a value at the top goes in a map under "default".
        int id = ds_new(RUN, AR_DS_MAP);
        map_set(RUN->ds[id], STR("default"), v, 0);
        return ar_real(id);
    }
    return v;
}

static void json_write(ARVM *vm, ARValue v, char **out, size_t *len, size_t *cap, int as_map, int depth);

static void put_text(char **out, size_t *len, size_t *cap, const char *t, size_t n) {
    if (*len + n + 1 > *cap) {
        *cap = (*len + n + 1) * 2;
        *out = realloc(*out, *cap);
    }
    memcpy(*out + *len, t, n);
    *len += n;
    (*out)[*len] = 0;
}

static void json_write_string(const char *s, size_t n, char **out, size_t *len, size_t *cap) {
    put_text(out, len, cap, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', c};
            put_text(out, len, cap, e, 2);
        } else if (c == '\n') {
            put_text(out, len, cap, "\\n", 2);
        } else if (c == '\r') {
            put_text(out, len, cap, "\\r", 2);
        } else if (c == '\t') {
            put_text(out, len, cap, "\\t", 2);
        } else {
            put_text(out, len, cap, &c, 1);
        }
    }
    put_text(out, len, cap, "\"", 1);
}

static void json_write(ARVM *vm, ARValue v, char **out, size_t *len, size_t *cap, int as_map, int depth) {
    char tmp[64];
    if (as_map && depth < 32) {
        ARDs *m = ds_get(vm, v, AR_DS_MAP);
        if (m) {
            put_text(out, len, cap, "{ ", 2);
            int first = 1;
            for (int i = 0; i < m->entry_count; i++) {
                if (m->entries[i].key.type == AR_UNDEFINED) continue;
                if (!first) put_text(out, len, cap, ", ", 2);
                first = 0;
                ARStr *k = ar_to_str(HEAP, m->entries[i].key);
                json_write_string(k->text, k->length, out, len, cap);
                put_text(out, len, cap, ": ", 2);
                json_write(vm, m->entries[i].value, out, len, cap, 0, depth + 1);
            }
            put_text(out, len, cap, " }", 2);
            return;
        }
    }
    switch (v.type) {
    case AR_STRING: json_write_string(v.s->text, v.s->length, out, len, cap); break;
    case AR_UNDEFINED: put_text(out, len, cap, "null", 4); break;
    case AR_BOOL: put_text(out, len, cap, v.r ? "true" : "false", v.r ? 4 : 5); break;
    case AR_ARRAY:
        put_text(out, len, cap, "[ ", 2);
        for (int i = 0; i < v.a->length && depth < 32; i++) {
            if (i) put_text(out, len, cap, ", ", 2);
            json_write(vm, v.a->items[i], out, len, cap, 0, depth + 1);
        }
        put_text(out, len, cap, " ]", 2);
        break;
    default: {
        double d = ar_to_real(v);
        if (d == floor(d) && fabs(d) < 1e15) snprintf(tmp, sizeof(tmp), "%.1f", d);
        else snprintf(tmp, sizeof(tmp), "%.17g", d);
        put_text(out, len, cap, tmp, strlen(tmp));
    }
    }
}

FN(f_json_encode) {
    UNUSED;
    if (argc == 0) return STR("");
    char *out = NULL;
    size_t len = 0, cap = 0;
    put_text(&out, &len, &cap, "", 0);
    json_write(vm, a[0], &out, &len, &cap, 1, 0);
    ARValue r = STR(out);
    free(out);
    return r;
}

static const ARBuiltin kCore[] = {
    {"abs", f_abs}, {"sign", f_sign}, {"round", f_round}, {"floor", f_floor}, {"ceil", f_ceil},
    {"frac", f_frac}, {"sqrt", f_sqrt}, {"sqr", f_sqr}, {"power", f_power}, {"exp", f_exp}, {"ln", f_ln},
    {"log2", f_log2}, {"log10", f_log10}, {"sin", f_sin}, {"cos", f_cos}, {"tan", f_tan},
    {"arcsin", f_arcsin}, {"arccos", f_arccos}, {"arctan", f_arctan}, {"arctan2", f_arctan2},
    {"dsin", f_dsin}, {"dcos", f_dcos}, {"dtan", f_dtan}, {"darcsin", f_darcsin}, {"darccos", f_darccos},
    {"darctan", f_darctan}, {"darctan2", f_darctan2}, {"degtorad", f_degtorad}, {"radtodeg", f_radtodeg},
    {"min", f_min}, {"max", f_max}, {"mean", f_mean}, {"clamp", f_clamp}, {"lerp", f_lerp},
    {"point_distance", f_point_distance}, {"point_direction", f_point_direction},
    {"lengthdir_x", f_lengthdir_x}, {"lengthdir_y", f_lengthdir_y}, {"angle_difference", f_angle_difference},
    {"random", f_random}, {"random_range", f_random_range}, {"irandom", f_irandom},
    {"irandom_range", f_irandom_range}, {"choose", f_choose}, {"randomize", f_randomize},
    {"randomise", f_randomize}, {"random_set_seed", f_random_set_seed}, {"random_get_seed", f_random_get_seed},
    {"is_string", f_is_string}, {"is_real", f_is_real}, {"is_numeric", f_is_numeric},
    {"is_undefined", f_is_undefined}, {"is_array", f_is_array}, {"is_struct", f_is_struct},
    {"is_method", f_is_method}, {"is_bool", f_is_bool}, {"is_int64", f_is_int64}, {"real", f_real},
    {"int64", f_int64}, {"bool", f_bool}, {"typeof", f_typeof}, {"string", f_string},
    {"string_length", f_string_length}, {"string_byte_length", f_string_byte_length},
    {"string_char_at", f_string_char_at}, {"string_copy", f_string_copy}, {"string_delete", f_string_delete},
    {"string_insert", f_string_insert}, {"string_pos", f_string_pos}, {"string_last_pos", f_string_last_pos},
    {"string_replace_all", f_string_replace_all}, {"string_replace", f_string_replace},
    {"string_count", f_string_count}, {"string_upper", f_string_upper}, {"string_lower", f_string_lower},
    {"string_repeat", f_string_repeat}, {"string_digits", f_string_digits}, {"string_letters", f_string_letters},
    {"string_lettersdigits", f_string_lettersdigits}, {"string_hash_to_newline", f_string_hash_to_newline},
    {"string_split", f_string_split}, {"chr", f_chr}, {"ord", f_ord}, {"string_format", f_string_format},
    {"array_create", f_array_create}, {"array_length", f_array_length}, {"array_length_1d", f_array_length},
    {"array_length_2d", f_array_length_2d}, {"array_height_2d", f_array_length}, {"array_push", f_array_push},
    {"array_pop", f_array_pop}, {"array_insert", f_array_insert}, {"array_delete", f_array_delete},
    {"array_resize", f_array_resize}, {"array_copy", f_array_copy}, {"array_get", f_array_get},
    {"array_set", f_array_set}, {"array_equals", f_array_equals},
    {"variable_global_exists", f_variable_global_exists}, {"variable_global_get", f_variable_global_get},
    {"variable_global_set", f_variable_global_set}, {"variable_instance_exists", f_variable_instance_exists},
    {"variable_instance_get", f_variable_instance_get}, {"variable_instance_set", f_variable_instance_set},
    {"variable_struct_exists", f_variable_instance_exists}, {"variable_struct_get", f_variable_instance_get},
    {"variable_struct_set", f_variable_instance_set}, {"variable_struct_remove", f_variable_struct_remove},
    {"variable_struct_names_count", f_variable_struct_names_count},
    {"variable_struct_get_names", f_variable_struct_get_names}, {"struct_get", f_variable_instance_get},
    {"struct_set", f_variable_instance_set}, {"struct_exists", f_variable_instance_exists},
    {"script_execute", f_script_execute}, {"script_exists", f_script_exists},
    {"method_get_index", f_method_get_index}, {"method_get_self", f_method_get_self},
    {"asset_get_index", f_asset_get_index}, {"show_debug_message", f_show_debug_message},
    {"show_error", f_show_error}, {"event_user", f_event_user}, {"event_perform", f_event_perform},
    {"event_inherited", f_event_inherited}, {"@@GetInstance@@", f_get_instance}, {"@@NullObject@@", f_null_object},
    {"ds_list_create", f_ds_list_create}, {"ds_list_destroy", f_ds_list_destroy}, {"ds_list_add", f_ds_list_add},
    {"ds_list_size", f_ds_list_size}, {"ds_list_find_value", f_ds_list_find_value}, {"ds_list_set", f_ds_list_set},
    {"ds_list_delete", f_ds_list_delete}, {"ds_list_insert", f_ds_list_insert}, {"ds_list_clear", f_ds_list_clear},
    {"ds_list_find_index", f_ds_list_find_index}, {"ds_list_empty", f_ds_list_empty},
    {"ds_list_write", f_ds_list_write}, {"ds_list_read", f_ds_list_read}, {"ds_map_create", f_ds_map_create},
    {"ds_map_destroy", f_ds_map_destroy}, {"ds_map_add", f_ds_map_add}, {"ds_map_set", f_ds_map_set},
    {"ds_map_replace", f_ds_map_set}, {"ds_map_set_post", f_ds_map_set},
    {"ds_map_find_value", f_ds_map_find_value}, {"ds_map_exists", f_ds_map_exists},
    {"ds_map_delete", f_ds_map_delete}, {"ds_map_size", f_ds_map_size}, {"ds_map_clear", f_ds_map_clear},
    {"ds_map_find_first", f_ds_map_find_first}, {"ds_map_find_next", f_ds_map_find_next},
    {"ds_map_keys_to_array", f_ds_map_keys_to_array}, {"ds_exists", f_ds_exists},
    {"ds_grid_create", f_ds_grid_create}, {"ds_grid_destroy", f_ds_grid_destroy}, {"ds_grid_get", f_ds_grid_get},
    {"ds_grid_set", f_ds_grid_set}, {"ds_grid_width", f_ds_grid_width}, {"ds_grid_height", f_ds_grid_height},
    {"ds_stack_create", f_ds_stack_create}, {"ds_stack_push", f_ds_stack_push}, {"ds_stack_pop", f_ds_stack_pop},
    {"ds_stack_size", f_ds_size_any}, {"ds_stack_destroy", f_ds_destroy_any}, {"ds_queue_create", f_ds_queue_create},
    {"ds_queue_enqueue", f_ds_stack_push}, {"ds_queue_dequeue", f_ds_queue_dequeue},
    {"ds_queue_size", f_ds_size_any}, {"ds_queue_destroy", f_ds_destroy_any}, {"json_decode", f_json_decode},
    {"json_encode", f_json_encode},
};

void ar_lib_register_game(void);
void ar_lib_register_io(void);

void ar_lib_register_all(void) {
    ar_vm_register(kCore, (int)(sizeof(kCore) / sizeof(kCore[0])), NULL, 0);
    ar_lib_register_game();
    ar_lib_register_io();
}
