// The builtin functions of GML for files, ini files, buffers, the system,
// the window, the gamepads, and the console platforms.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "ar_run_internal.h"

#define FN(name) static ARValue name(ARVM *vm, int argc, ARValue *a)
#define UNUSED (void)vm, (void)argc, (void)a
#define HEAP ar_vm_heap(vm)
#define RUN ar_run_of(vm)
#define STR(text) ar_strv(ar_str_cstr(HEAP, text))
#define ARG_STR(i) (i < argc ? ar_to_str(HEAP, a[i]) : ar_str_cstr(HEAP, ""))
#define REAL(i, d) ar_arg_real(argc, a, i, d)
#define INT(i, d) ar_arg_int(argc, a, i, d)

// ------------------------------------------------------------------ files

static void make_parent_dirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

FN(f_file_exists) {
    UNUSED;
    char path[1024];
    ar_run_path(RUN, ARG_STR(0)->text, 0, path, sizeof(path));
    struct stat st;
    return ar_bool(stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

FN(f_file_delete) {
    UNUSED;
    char path[1024];
    ar_run_path(RUN, ARG_STR(0)->text, 1, path, sizeof(path));
    return ar_bool(remove(path) == 0);
}

FN(f_file_copy) {
    UNUSED;
    char from[1024], to[1024];
    ar_run_path(RUN, ARG_STR(0)->text, 0, from, sizeof(from));
    ar_run_path(RUN, ARG_STR(1)->text, 1, to, sizeof(to));
    FILE *in = fopen(from, "rb");
    if (!in) return ar_bool(0);
    make_parent_dirs(to);
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return ar_bool(0);
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return ar_bool(1);
}

FN(f_directory_exists) {
    UNUSED;
    char path[1024];
    ar_run_path(RUN, ARG_STR(0)->text, 0, path, sizeof(path));
    struct stat st;
    return ar_bool(stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

FN(f_directory_create) {
    UNUSED;
    char path[1024];
    ar_run_path(RUN, ARG_STR(0)->text, 1, path, sizeof(path));
    make_parent_dirs(path);
    mkdir(path, 0755);
    return ar_undefined();
}

static int open_text(ARVM *vm, const char *name, const char *mode, int write) {
    ARRun *run = RUN;
    char path[1024];
    ar_run_path(run, name, write, path, sizeof(path));
    if (write) make_parent_dirs(path);
    FILE *f = fopen(path, mode);
    if (!f) return -1;
    for (int i = 0; i < 32; i++) {
        if (!run->files[i].file) {
            run->files[i].file = f;
            run->files[i].write = write;
            run->files[i].eof = 0;
            return i;
        }
    }
    fclose(f);
    return -1;
}

static ARTextFile *text_file(ARVM *vm, int argc, ARValue *a) {
    int i = INT(0, -1);
    if (i < 0 || i >= 32 || !RUN->files[i].file) return NULL;
    return &RUN->files[i];
}

FN(f_file_text_open_read) { UNUSED; return ar_real(open_text(vm, ARG_STR(0)->text, "rb", 0)); }
FN(f_file_text_open_write) { UNUSED; return ar_real(open_text(vm, ARG_STR(0)->text, "wb", 1)); }
FN(f_file_text_open_append) { UNUSED; return ar_real(open_text(vm, ARG_STR(0)->text, "ab", 1)); }

FN(f_file_text_close) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (f) {
        fclose(f->file);
        f->file = NULL;
    }
    return ar_undefined();
}

FN(f_file_text_eof) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (!f) return ar_bool(1);
    int c = fgetc(f->file);
    if (c == EOF) return ar_bool(1);
    ungetc(c, f->file);
    return ar_bool(0);
}

FN(f_file_text_readln) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (!f) return STR("");
    // Reads the rest of the line and the end of the line.
    char buf[4096];
    size_t n = 0;
    int c;
    while ((c = fgetc(f->file)) != EOF && c != '\n') {
        if (n < sizeof(buf) - 1) buf[n++] = (char)c;
    }
    if (n > 0 && buf[n - 1] == '\r') n--;
    return ar_strv(ar_str_new(HEAP, buf, n));
}

FN(f_file_text_read_string) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (!f) return STR("");
    size_t cap = 256, n = 0;
    char *buf = malloc(cap);
    int c;
    while ((c = fgetc(f->file)) != EOF && c != '\n' && c != '\r') {
        if (n + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        buf[n++] = (char)c;
    }
    if (c != EOF) ungetc(c, f->file);
    ARValue r = ar_strv(ar_str_new(HEAP, buf, n));
    free(buf);
    return r;
}

FN(f_file_text_read_real) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (!f) return ar_real(0);
    char buf[128];
    size_t n = 0;
    int c;
    while ((c = fgetc(f->file)) == ' ' || c == '\t') {
    }
    while (c != EOF && c != '\n' && c != '\r' && c != ' ' && n < sizeof(buf) - 1) {
        buf[n++] = (char)c;
        c = fgetc(f->file);
    }
    if (c != EOF) ungetc(c, f->file);
    buf[n] = 0;
    return ar_real(strtod(buf, NULL));
}

FN(f_file_text_write_string) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (f) {
        ARStr *s = ARG_STR(1);
        fwrite(s->text, 1, s->length, f->file);
    }
    return ar_undefined();
}

FN(f_file_text_write_real) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (f) {
        double d = REAL(1, 0);
        if (d == floor(d) && fabs(d) < 1e15) fprintf(f->file, "%.0f", d == 0 ? 0.0 : d);
        else fprintf(f->file, "%.16g", d);
    }
    return ar_undefined();
}

FN(f_file_text_writeln) {
    UNUSED;
    ARTextFile *f = text_file(vm, argc, a);
    if (f) fputs("\r\n", f->file);
    return ar_undefined();
}

// ---------------------------------------------------------------- ini files

static void ini_clear(ARIni *ini) {
    for (int i = 0; i < ini->count; i++) {
        free(ini->keys[i].section);
        free(ini->keys[i].key);
        free(ini->keys[i].value);
    }
    ini->count = 0;
    ini->changed = 0;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static ARIniKey *ini_find(ARIni *ini, const char *section, const char *key) {
    for (int i = 0; i < ini->count; i++) {
        if (strcmp(ini->keys[i].section, section) == 0 && strcmp(ini->keys[i].key, key) == 0) return &ini->keys[i];
    }
    return NULL;
}

static void ini_put(ARIni *ini, const char *section, const char *key, const char *value) {
    ARIniKey *k = ini_find(ini, section, key);
    if (k) {
        free(k->value);
        k->value = strdup(value);
        return;
    }
    if (ini->count == ini->capacity) {
        ini->capacity = ini->capacity ? ini->capacity * 2 : 32;
        ini->keys = realloc(ini->keys, sizeof(ARIniKey) * (size_t)ini->capacity);
    }
    ini->keys[ini->count].section = strdup(section);
    ini->keys[ini->count].key = strdup(key);
    ini->keys[ini->count].value = strdup(value);
    ini->count++;
}

static void ini_parse(ARIni *ini, const char *text) {
    char section[256] = "";
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[2048];
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = 0;
        char *t = trim(line);
        if (*t == '[') {
            char *close = strchr(t, ']');
            if (close) *close = 0;
            snprintf(section, sizeof(section), "%s", t + 1);
        } else if (*t && *t != ';' && *t != '#') {
            char *eq = strchr(t, '=');
            if (eq) {
                *eq = 0;
                char *key = trim(t), *value = trim(eq + 1);
                size_t vl = strlen(value);
                if (vl >= 2 && value[0] == '"' && value[vl - 1] == '"') {
                    value[vl - 1] = 0;
                    value++;
                }
                ini_put(ini, section, key, value);
            }
        }
        if (!end) break;
        p = end + 1;
    }
}

// The text of the ini, with the sections in the order of their first key.
static char *ini_text(ARIni *ini) {
    size_t cap = 1024, len = 0;
    char *out = malloc(cap);
    out[0] = 0;
    for (int i = 0; i < ini->count; i++) {
        int first = 1;
        for (int k = 0; k < i; k++) {
            if (strcmp(ini->keys[k].section, ini->keys[i].section) == 0) first = 0;
        }
        if (!first) continue;
        size_t need = strlen(ini->keys[i].section) + 8;
        for (int k = i; k < ini->count; k++) {
            if (strcmp(ini->keys[k].section, ini->keys[i].section) == 0) {
                need += strlen(ini->keys[k].key) + strlen(ini->keys[k].value) + 8;
            }
        }
        if (len + need + 1 > cap) {
            cap = (len + need + 1) * 2;
            out = realloc(out, cap);
        }
        len += (size_t)sprintf(out + len, "[%s]\r\n", ini->keys[i].section);
        for (int k = i; k < ini->count; k++) {
            if (strcmp(ini->keys[k].section, ini->keys[i].section) != 0) continue;
            len += (size_t)sprintf(out + len, "%s=\"%s\"\r\n", ini->keys[k].key, ini->keys[k].value);
        }
    }
    return out;
}

static char *ini_close(ARRun *run) {
    ARIni *ini = &run->ini;
    char *text = ini_text(ini);
    if (ini->open && ini->path[0] && ini->changed) {
        make_parent_dirs(ini->path);
        FILE *f = fopen(ini->path, "wb");
        if (f) {
            fputs(text, f);
            fclose(f);
        }
    }
    ini_clear(ini);
    ini->open = 0;
    return text;
}

FN(f_ini_open) {
    UNUSED;
    ARRun *run = RUN;
    if (run->ini.open) free(ini_close(run));
    char path[1024];
    ar_run_path(run, ARG_STR(0)->text, 0, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    ar_run_path(run, ARG_STR(0)->text, 1, run->ini.path, sizeof(run->ini.path));
    run->ini.open = 1;
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *text = malloc((size_t)n + 1);
        size_t got = fread(text, 1, (size_t)n, f);
        text[got] = 0;
        fclose(f);
        ini_parse(&run->ini, text);
        free(text);
    }
    return ar_undefined();
}

FN(f_ini_open_from_string) {
    UNUSED;
    ARRun *run = RUN;
    if (run->ini.open) free(ini_close(run));
    run->ini.path[0] = 0;
    run->ini.open = 1;
    ini_parse(&run->ini, ARG_STR(0)->text);
    return ar_undefined();
}

FN(f_ini_close) {
    UNUSED;
    char *text = ini_close(RUN);
    ARValue r = STR(text);
    free(text);
    return r;
}

FN(f_ini_read_string) {
    UNUSED;
    ARIniKey *k = ini_find(&RUN->ini, ARG_STR(0)->text, ARG_STR(1)->text);
    if (k) return STR(k->value);
    return argc > 2 ? ar_strv(ar_to_str(HEAP, a[2])) : STR("");
}

FN(f_ini_read_real) {
    UNUSED;
    ARIniKey *k = ini_find(&RUN->ini, ARG_STR(0)->text, ARG_STR(1)->text);
    if (k) {
        char *end;
        double d = strtod(k->value, &end);
        if (end != k->value) return ar_real(d);
    }
    return ar_real(REAL(2, 0));
}

FN(f_ini_write_string) {
    UNUSED;
    ARRun *run = RUN;
    ini_put(&run->ini, ARG_STR(0)->text, ARG_STR(1)->text, ARG_STR(2)->text);
    run->ini.changed = 1;
    return ar_undefined();
}

FN(f_ini_write_real) {
    UNUSED;
    ARRun *run = RUN;
    char text[64];
    double d = REAL(2, 0);
    if (d == floor(d) && fabs(d) < 1e15) snprintf(text, sizeof(text), "%.6f", d);
    else snprintf(text, sizeof(text), "%.6f", d);
    ini_put(&run->ini, ARG_STR(0)->text, ARG_STR(1)->text, text);
    run->ini.changed = 1;
    return ar_undefined();
}

FN(f_ini_key_exists) {
    UNUSED;
    return ar_bool(ini_find(&RUN->ini, ARG_STR(0)->text, ARG_STR(1)->text) != NULL);
}

FN(f_ini_section_exists) {
    UNUSED;
    ARIni *ini = &RUN->ini;
    const char *section = ARG_STR(0)->text;
    for (int i = 0; i < ini->count; i++) {
        if (strcmp(ini->keys[i].section, section) == 0) return ar_bool(1);
    }
    return ar_bool(0);
}

FN(f_ini_key_delete) {
    UNUSED;
    ARIni *ini = &RUN->ini;
    ARIniKey *k = ini_find(ini, ARG_STR(0)->text, ARG_STR(1)->text);
    if (k) {
        free(k->section);
        free(k->key);
        free(k->value);
        *k = ini->keys[--ini->count];
        ini->changed = 1;
    }
    return ar_undefined();
}

// ---------------------------------------------------------------- buffers

static ARBuffer *buffer_arg(ARVM *vm, int argc, ARValue *a) {
    int i = INT(0, -1);
    if (i < 0 || i >= 64 || !RUN->buffers[i].used) return NULL;
    return &RUN->buffers[i];
}

static int buffer_new(ARRun *run, size_t size, int type) {
    for (int i = 0; i < 64; i++) {
        if (run->buffers[i].used) continue;
        ARBuffer *b = &run->buffers[i];
        b->used = 1;
        b->capacity = size ? size : 1;
        b->bytes = calloc(1, b->capacity);
        b->size = size;
        b->pos = 0;
        b->type = type;
        return i;
    }
    return -1;
}

FN(f_buffer_create) { UNUSED; return ar_real(buffer_new(RUN, (size_t)INT(0, 1), INT(1, 1))); }

FN(f_buffer_delete) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    if (b) {
        free(b->bytes);
        memset(b, 0, sizeof(*b));
    }
    return ar_undefined();
}

FN(f_buffer_get_size) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    return ar_real(b ? (double)b->size : 0);
}

static int type_bytes(int type) {
    switch (type) {
    case 1: case 2: case 10: return 1;
    case 3: case 4: case 7: return 2;
    case 5: case 6: case 8: return 4;
    case 9: case 12: return 8;
    default: return 0;
    }
}

static void buffer_put(ARBuffer *b, const void *bytes, size_t n) {
    if (b->pos + n > b->capacity) {
        if (b->type == 1) {
            while (b->pos + n > b->capacity) b->capacity *= 2;
            b->bytes = realloc(b->bytes, b->capacity);
        } else if (b->type == 2 && b->capacity > 0) {
            b->pos = 0;
            if (n > b->capacity) return;
        } else {
            return;
        }
    }
    memcpy(b->bytes + b->pos, bytes, n);
    b->pos += n;
    if (b->pos > b->size) b->size = b->pos;
}

FN(f_buffer_write) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    if (!b || argc < 3) return ar_real(-1);
    int type = INT(1, 0);
    if (type == 11 || type == 13) {
        ARStr *s = ar_to_str(HEAP, a[2]);
        buffer_put(b, s->text, s->length + (type == 11 ? 1 : 0));
        return ar_real(0);
    }
    double d = ar_to_real(a[2]);
    union {
        uint8_t u8;
        int8_t s8;
        uint16_t u16;
        int16_t s16;
        uint32_t u32;
        int32_t s32;
        float f;
        double d;
        uint64_t u64;
    } v;
    switch (type) {
    case 1: v.u8 = (uint8_t)(int)d; break;
    case 2: v.s8 = (int8_t)d; break;
    case 3: v.u16 = (uint16_t)(int)d; break;
    case 4: v.s16 = (int16_t)d; break;
    case 5: v.u32 = (uint32_t)(int64_t)d; break;
    case 6: v.s32 = (int32_t)d; break;
    case 8: v.f = (float)d; break;
    case 9: v.d = d; break;
    case 10: v.u8 = d != 0; break;
    case 12: v.u64 = (uint64_t)ar_to_int64(a[2]); break;
    default: return ar_real(-1);
    }
    buffer_put(b, &v, (size_t)type_bytes(type));
    return ar_real(0);
}

FN(f_buffer_read) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    if (!b) return ar_undefined();
    int type = INT(1, 0);
    if (type == 11 || type == 13) {
        size_t start = b->pos;
        while (b->pos < b->size && b->bytes[b->pos]) b->pos++;
        ARValue s = ar_strv(ar_str_new(HEAP, (const char *)b->bytes + start, b->pos - start));
        if (b->pos < b->size) b->pos++;
        return s;
    }
    int n = type_bytes(type);
    if (n == 0 || b->pos + (size_t)n > b->size) return ar_undefined();
    const uint8_t *p = b->bytes + b->pos;
    b->pos += (size_t)n;
    switch (type) {
    case 1: return ar_real(p[0]);
    case 2: return ar_real((int8_t)p[0]);
    case 3: return ar_real((uint16_t)(p[0] | p[1] << 8));
    case 4: return ar_real((int16_t)(p[0] | p[1] << 8));
    case 5: {
        uint32_t u;
        memcpy(&u, p, 4);
        return ar_real(u);
    }
    case 6: {
        int32_t s;
        memcpy(&s, p, 4);
        return ar_real(s);
    }
    case 8: {
        float f;
        memcpy(&f, p, 4);
        return ar_real(f);
    }
    case 9: {
        double d;
        memcpy(&d, p, 8);
        return ar_real(d);
    }
    case 10: return ar_bool(p[0] != 0);
    case 12: {
        int64_t l;
        memcpy(&l, p, 8);
        return ar_int64(l);
    }
    }
    return ar_undefined();
}

FN(f_buffer_seek) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    if (!b) return ar_undefined();
    int base = INT(1, 0);
    long off = INT(2, 0);
    long pos = base == 0 ? off : base == 1 ? (long)b->pos + off : (long)b->size + off;
    if (pos < 0) pos = 0;
    b->pos = (size_t)pos;
    return ar_undefined();
}

FN(f_buffer_tell) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    return ar_real(b ? (double)b->pos : 0);
}

FN(f_buffer_load) {
    UNUSED;
    ARRun *run = RUN;
    char path[1024];
    ar_run_path(run, ARG_STR(0)->text, 0, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return ar_real(-1);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    int id = buffer_new(run, (size_t)n, 1);
    if (id >= 0) {
        size_t got = fread(run->buffers[id].bytes, 1, (size_t)n, f);
        run->buffers[id].size = got;
    }
    fclose(f);
    return ar_real(id);
}

FN(f_buffer_save) {
    UNUSED;
    ARBuffer *b = buffer_arg(vm, argc, a);
    if (!b) return ar_undefined();
    char path[1024];
    ar_run_path(RUN, ARG_STR(1)->text, 1, path, sizeof(path));
    make_parent_dirs(path);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(b->bytes, 1, b->size, f);
        fclose(f);
    }
    return ar_undefined();
}

// ------------------------------------------------------------------ system

FN(f_nothing) {
    UNUSED;
    return ar_undefined();
}

FN(f_zero) {
    UNUSED;
    return ar_real(0);
}

FN(f_minus_one) {
    UNUSED;
    return ar_real(-1);
}

FN(f_false) {
    UNUSED;
    return ar_bool(0);
}

FN(f_empty_string) {
    UNUSED;
    return STR("");
}

FN(f_game_end) {
    UNUSED;
    RUN->game_end = 1;
    return ar_undefined();
}

FN(f_game_restart) {
    UNUSED;
    RUN->restart = 1;
    return ar_undefined();
}

FN(f_game_change) {
    UNUSED;
    ARRun *run = RUN;
    snprintf(run->change_dir, sizeof(run->change_dir), "%s", ARG_STR(0)->text);
    snprintf(run->change_params, sizeof(run->change_params), "%s", ARG_STR(1)->text);
    run->change = 1;
    return ar_undefined();
}

// The parameters start with the path of the program, as argv does.
// parameter_count does not count it.
FN(f_parameter_count) {
    UNUSED;
    return ar_real(RUN->parameter_count > 0 ? RUN->parameter_count - 1 : 0);
}

FN(f_parameter_string) {
    UNUSED;
    int i = INT(0, 0);
    ARRun *run = RUN;
    if (i < 0 || i >= run->parameter_count || !run->parameters) return STR("");
    return STR(run->parameters[i]);
}

FN(f_os_get_config) {
    UNUSED;
    return STR("Default");
}

FN(f_os_get_language) {
    UNUSED;
    return STR("en");
}

FN(f_get_timer) {
    UNUSED;
    return ar_real(floor((ar_run_time_ms() - RUN->start_time) * 1000));
}

FN(f_date_current_datetime) {
    UNUSED;
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    // Days since 1899-12-30, as the dates of GML.
    double days = (double)(now + lt.tm_gmtoff) / 86400.0 + 25569.0;
    return ar_real(days);
}

FN(f_window_get_width) {
    UNUSED;
    return ar_real(RUN->window_w);
}

FN(f_window_get_height) {
    UNUSED;
    return ar_real(RUN->window_h);
}

FN(f_display_get_width) {
    UNUSED;
    return ar_real(RUN->window_w);
}

FN(f_display_get_height) {
    UNUSED;
    return ar_real(RUN->window_h);
}

static const ARBuiltin kIO[] = {
    {"file_exists", f_file_exists}, {"file_delete", f_file_delete}, {"file_copy", f_file_copy},
    {"directory_exists", f_directory_exists}, {"directory_create", f_directory_create},
    {"file_text_open_read", f_file_text_open_read}, {"file_text_open_write", f_file_text_open_write},
    {"file_text_open_append", f_file_text_open_append}, {"file_text_close", f_file_text_close},
    {"file_text_eof", f_file_text_eof}, {"file_text_readln", f_file_text_readln},
    {"file_text_read_string", f_file_text_read_string}, {"file_text_read_real", f_file_text_read_real},
    {"file_text_write_string", f_file_text_write_string}, {"file_text_write_real", f_file_text_write_real},
    {"file_text_writeln", f_file_text_writeln}, {"ini_open", f_ini_open},
    {"ini_open_from_string", f_ini_open_from_string}, {"ini_close", f_ini_close},
    {"ini_read_string", f_ini_read_string}, {"ini_read_real", f_ini_read_real},
    {"ini_write_string", f_ini_write_string}, {"ini_write_real", f_ini_write_real},
    {"ini_key_exists", f_ini_key_exists}, {"ini_section_exists", f_ini_section_exists},
    {"ini_key_delete", f_ini_key_delete}, {"buffer_create", f_buffer_create}, {"buffer_delete", f_buffer_delete},
    {"buffer_get_size", f_buffer_get_size}, {"buffer_write", f_buffer_write}, {"buffer_read", f_buffer_read},
    {"buffer_seek", f_buffer_seek}, {"buffer_tell", f_buffer_tell}, {"buffer_load", f_buffer_load},
    {"buffer_save", f_buffer_save}, {"buffer_load_async", f_minus_one}, {"buffer_save_async", f_minus_one},
    {"buffer_async_group_begin", f_nothing}, {"buffer_async_group_end", f_minus_one},
    {"buffer_async_group_option", f_nothing}, {"game_end", f_game_end}, {"game_restart", f_game_restart},
    {"game_change", f_game_change}, {"parameter_count", f_parameter_count}, {"parameter_string", f_parameter_string},
    {"environment_get_variable", f_empty_string}, {"os_get_config", f_os_get_config}, {"os_get_info", f_minus_one},
    {"os_get_language", f_os_get_language}, {"os_is_paused", f_false}, {"get_timer", f_get_timer},
    {"date_current_datetime", f_date_current_datetime}, {"window_get_width", f_window_get_width},
    {"window_get_height", f_window_get_height}, {"window_set_size", f_nothing}, {"window_center", f_nothing},
    {"window_set_caption", f_nothing}, {"window_set_cursor", f_nothing}, {"window_get_fullscreen", f_false},
    {"window_set_fullscreen", f_nothing}, {"window_enable_borderless_fullscreen", f_nothing},
    {"display_get_width", f_display_get_width}, {"display_get_height", f_display_get_height},
    {"display_set_gui_size", f_nothing}, {"gamepad_get_device_count", f_zero}, {"gamepad_is_connected", f_false},
    {"gamepad_button_check", f_false}, {"gamepad_button_check_pressed", f_false},
    {"gamepad_button_check_released", f_false}, {"gamepad_axis_value", f_zero},
    {"gamepad_get_description", f_empty_string}, {"gamepad_get_guid", f_empty_string},
    {"gamepad_test_mapping", f_nothing}, {"gamepad_set_axis_deadzone", f_nothing},
    {"ps4_touchpad_mouse_enable", f_nothing}, {"psn_init_np_libs", f_nothing}, {"psn_init_trophy", f_nothing},
    {"psn_load_modules", f_nothing}, {"psn_tick", f_nothing}, {"psn_np_commerce_dialog_tick", f_nothing},
    {"psn_post_uds_event", f_nothing}, {"psn_unlock_trophy", f_nothing}, {"psn_get_trophy_unlock_state", f_zero},
    {"switch_accounts_is_user_open", f_false}, {"switch_accounts_open_user", f_nothing},
    {"switch_accounts_select_account", f_nothing}, {"switch_controller_set_supported_styles", f_nothing},
    {"switch_controller_support_get_selected_id", f_zero}, {"switch_controller_support_set_defaults", f_nothing},
    {"switch_controller_support_set_singleplayer_only", f_nothing}, {"switch_controller_support_show", f_nothing},
    {"switch_language_get_desired_language", f_empty_string}, {"switch_save_data_commit", f_nothing},
    {"switch_save_data_mount", f_nothing}, {"switch_save_data_unmount", f_nothing}, {"switch_show_store", f_nothing},
    {"steam_initialised", f_false}, {"steam_stats_ready", f_false},
};

void ar_lib_register_io(void) {
    ar_vm_register(kIO, (int)(sizeof(kIO) / sizeof(kIO[0])), NULL, 0);
}
