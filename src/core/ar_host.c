#include "ar_host.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PARAMS 32

struct ARHost {
    ARHostConfig config;
    char base[1024];
    char dir[1024];
    char save[1024];
    char cache[1024];
    char music[1024];
    char data_name[64];
    char *params[MAX_PARAMS];
    int param_count;
    const uint8_t *bytes;
    size_t size;
    ARData data;
    int has_data;
    ARRun *run;
    char error[256];
    char notice[256];
};

static void unload(ARHost *h) {
    if (h->run) ar_run_free(h->run);
    h->run = NULL;
    if (h->has_data) ar_data_free(&h->data);
    h->has_data = 0;
    if (h->bytes) munmap((void *)h->bytes, h->size);
    h->bytes = NULL;
}

static int load(ARHost *h) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", h->dir, h->data_name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(h->error, sizeof(h->error), "There is no %s", path);
        return 0;
    }
    struct stat st;
    fstat(fd, &st);
    h->size = (size_t)st.st_size;
    h->bytes = mmap(NULL, h->size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (h->bytes == MAP_FAILED) {
        h->bytes = NULL;
        snprintf(h->error, sizeof(h->error), "%s does not map into memory", path);
        return 0;
    }
    if (!ar_data_read(&h->data, h->bytes, h->size)) {
        snprintf(h->error, sizeof(h->error), "%s: %s", path, h->data.error);
        return 0;
    }
    h->has_data = 1;
    ARRunConfig rc;
    memset(&rc, 0, sizeof(rc));
    rc.game_dir = h->dir;
    rc.save_dir = h->save;
    rc.music_dir = h->music;
    rc.cache_dir = h->cache[0] ? h->cache : NULL;
    rc.parameters = (const char *const *)h->params;
    rc.parameter_count = h->param_count;
    rc.gfx = h->config.gfx;
    rc.audio = h->config.audio;
    h->run = ar_run_new(&h->data, &rc, h->error, sizeof(h->error));
    if (!h->run) return 0;
    ar_run_start(h->run);
    return 1;
}

ARHost *ar_host_open(const ARHostConfig *config, char *error, size_t size) {
    ARHost *h = calloc(1, sizeof(ARHost));
    h->config = *config;
    snprintf(h->base, sizeof(h->base), "%s", config->base_dir);
    snprintf(h->dir, sizeof(h->dir), "%s", config->base_dir);
    snprintf(h->save, sizeof(h->save), "%s", config->save_dir);
    if (config->cache_dir) {
        snprintf(h->cache, sizeof(h->cache), "%s", config->cache_dir);
        mkdir(h->cache, 0755);
    }
    snprintf(h->music, sizeof(h->music), "%s/mus", config->base_dir);
    snprintf(h->data_name, sizeof(h->data_name), "%s", config->data_name ? config->data_name : "game.ios");
    mkdir(h->save, 0755);
    h->params[0] = strdup("alpharune");
    h->param_count = 1;
    if (!load(h)) {
        snprintf(error, size, "%s", h->error);
        ar_host_close(h);
        return NULL;
    }
    return h;
}

void ar_host_close(ARHost *h) {
    if (!h) return;
    unload(h);
    for (int i = 0; i < h->param_count; i++) free(h->params[i]);
    free(h);
}

int ar_host_frame(ARHost *h) {
    if (!h->run) return 0;
    if (ar_run_frame(h->run)) return 1;
    char next[512], text[512];
    if (ar_run_change_request(h->run, next, sizeof(next), text, sizeof(text))) {
        // The next data file is in a directory of the base: "chapter1_mac",
        // or the base itself for the chapter select.
        const char *rel = next;
        while (*rel == '/' || *rel == '.') rel++;
        if (*rel) snprintf(h->dir, sizeof(h->dir), "%s/%s", h->base, rel);
        else snprintf(h->dir, sizeof(h->dir), "%s", h->base);
        for (int i = 1; i < h->param_count; i++) free(h->params[i]);
        h->param_count = 1;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", h->dir, h->data_name);
        if (*rel && access(path, R_OK) != 0) {
            // A chapter that this version does not play: the chapter select
            // opens again, and the host shows a notice.
            int chapter = 0;
            for (const char *c = rel; *c && !chapter; c++) {
                if (*c >= '1' && *c <= '9') chapter = atoi(c);
            }
            if (chapter) {
                snprintf(h->notice, sizeof(h->notice), "Chapter %d comes in a later version of Alpharune.", chapter);
            } else {
                snprintf(h->notice, sizeof(h->notice), "There is no %s.", path);
            }
            snprintf(h->dir, sizeof(h->dir), "%s", h->base);
        } else {
            for (char *word = strtok(text, " "); word && h->param_count < MAX_PARAMS; word = strtok(NULL, " ")) {
                h->params[h->param_count++] = strdup(word);
            }
        }
    } else if (!ar_run_restart_request(h->run)) {
        return 0;
    }
    unload(h);
    return load(h);
}

ARRun *ar_host_run(ARHost *h) {
    return h->run;
}

const char *ar_host_notice(ARHost *h) {
    if (!h->notice[0]) return NULL;
    static char shown[256];
    snprintf(shown, sizeof(shown), "%s", h->notice);
    h->notice[0] = 0;
    return shown;
}

const char *ar_host_error(ARHost *h) {
    return h->error;
}

const char *ar_host_dir(ARHost *h) {
    return h->dir;
}
