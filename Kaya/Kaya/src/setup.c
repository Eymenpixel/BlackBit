#define _POSIX_C_SOURCE 200809L
#include "setup.h"
#include "json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SETUP_MAX_BYTES (1024u * 1024u)

static int make_root(const char *path) {
    char tmp[1024];
    size_t n = strlen(path);
    if (!n || n >= sizeof(tmp)) return -1;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int setup_locations(const char *root, char *dir, size_t dsz,
                           char *file, size_t fsz) {
    int nd = snprintf(dir, dsz, "%s/private", root);
    int nf = snprintf(file, fsz, "%s/private/setup.json", root);
    return nd < 0 || (size_t)nd >= dsz || nf < 0 || (size_t)nf >= fsz ? -1 : 0;
}

int kaya_setup_path(const char *root_dir, char *out, size_t out_size) {
    if (!root_dir || !out || out_size == 0) return -1;
    int n = snprintf(out, out_size, "%s/private/setup.json", root_dir);
    return n < 0 || (size_t)n >= out_size ? -1 : 0;
}

static int ensure_private(const char *root, char *path, size_t psz) {
    char dir[1024];
    if (setup_locations(root, dir, sizeof(dir), path, psz) != 0) return -1;
    if (make_root(root) != 0) return -1;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    struct stat st;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        errno = EACCES;
        return -1;
    }
    if (chmod(dir, 0700) != 0) return -1;
    if (lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) { errno = EACCES; return -1; }
        if (chmod(path, 0600) != 0) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    return 0;
}

static json_value *new_profile(void) {
    json_value *root = json_new_object();
    json_value *runtimes = json_new_array();
    json_value *selected = json_new_object();
    if (!root || !runtimes || !selected) {
        json_free(root); json_free(runtimes); json_free(selected);
        return NULL;
    }
    if (json_object_set(root, "schemaVersion", json_new_number(1)) != 0 ||
        json_object_set(root, "runtimeInventory", runtimes) != 0 ||
        json_object_set(root, "selectedBuilds", selected) != 0) {
        json_free(root);
        return NULL;
    }
    return root;
}

static json_value *load_profile(const char *path, int missing_ok) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT && missing_ok) return new_profile();
        return NULL;
    }
    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_size < 0 ||
        (unsigned long)st.st_size > SETUP_MAX_BYTES) {
        errno = EINVAL;
        return NULL;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    size_t cap = (size_t)st.st_size + 1;
    char *text = malloc(cap);
    if (!text) { close(fd); return NULL; }
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t got = read(fd, text + used, cap - used - 1);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { free(text); close(fd); return NULL; }
        if (got == 0) break;
        used += (size_t)got;
    }
    close(fd);
    text[used] = '\0';
    const char *err = NULL;
    json_value *profile = json_parse(text, &err);
    free(text);
    if (!profile || profile->type != JSON_OBJECT ||
        json_get_number(profile, "schemaVersion", 0) != 1 ||
        !json_object_get(profile, "runtimeInventory") ||
        json_object_get(profile, "runtimeInventory")->type != JSON_ARRAY ||
        !json_object_get(profile, "selectedBuilds") ||
        json_object_get(profile, "selectedBuilds")->type != JSON_OBJECT) {
        json_free(profile);
        errno = EINVAL;
        return NULL;
    }
    return profile;
}

static int save_profile(const char *path, const json_value *profile) {
    char *serialized = json_stringify(profile);
    if (!serialized) return -1;
    size_t len = strlen(serialized);
    char tmp[1200];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) { free(serialized); return -1; }
    int fd = mkstemp(tmp);
    if (fd < 0) { free(serialized); return -1; }
    int rc = 0;
    if (fchmod(fd, 0600) != 0) rc = -1;
    size_t written = 0;
    while (rc == 0 && written < len) {
        ssize_t put = write(fd, serialized + written, len - written);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) { rc = -1; break; }
        written += (size_t)put;
    }
    if (rc == 0 && fsync(fd) != 0) rc = -1;
    if (close(fd) != 0) rc = -1;
    if (rc == 0 && rename(tmp, path) != 0) rc = -1;
    if (rc != 0) unlink(tmp);
    free(serialized);
    return rc;
}

int kaya_setup_init(const char *root_dir) {
    if (!root_dir || !*root_dir) return -1;
    char path[1200];
    if (ensure_private(root_dir, path, sizeof(path)) != 0) return -1;
    json_value *profile = load_profile(path, 1);
    if (!profile) return -1;
    int rc = save_profile(path, profile);
    json_free(profile);
    return rc;
}

static int valid_id(const char *s) {
    if (!s || !*s || strlen(s) > 63) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' )) return 0;
    return 1;
}

static json_value *selection_json(const kaya_build_selection *s) {
    json_value *v = json_new_object();
    if (!v) return NULL;
#define SET_STRING(k, f) do { \
    json_value *sv = json_new_string(s->f); \
    if (!sv || json_object_set(v, k, sv) != 0) { json_free(sv); json_free(v); return NULL; } \
} while (0)
    SET_STRING("buildId", build_id);
    SET_STRING("language", language);
    SET_STRING("languageVersion", language_version);
    SET_STRING("runtime", runtime);
    SET_STRING("runtimeVersion", runtime_version);
#undef SET_STRING
    return v;
}

int kaya_setup_set_build(const char *root_dir, const char *package_id,
                         const kaya_build_selection *selection) {
    if (!root_dir || !valid_id(package_id) || !selection ||
        !*selection->build_id || !*selection->language || !*selection->runtime) return -1;
    char path[1200];
    if (ensure_private(root_dir, path, sizeof(path)) != 0) return -1;
    json_value *profile = load_profile(path, 1);
    if (!profile) return -1;
    json_value *selected = json_object_get(profile, "selectedBuilds");
    json_value *value = selection_json(selection);
    if (!selected || selected->type != JSON_OBJECT || !value ||
        json_object_set(selected, package_id, value) != 0) {
        json_free(value); json_free(profile); return -1;
    }
    int rc = save_profile(path, profile);
    json_free(profile);
    return rc;
}

static int copy_json_string(const json_value *obj, const char *key, char *out, size_t n) {
    const char *s = json_get_string(obj, key, NULL);
    if (!s || strlen(s) >= n) return -1;
    memcpy(out, s, strlen(s) + 1);
    return 0;
}

int kaya_setup_get_build(const char *root_dir, const char *package_id,
                         kaya_build_selection *selection) {
    if (!root_dir || !valid_id(package_id) || !selection) return -1;
    char path[1200];
    if (ensure_private(root_dir, path, sizeof(path)) != 0) return -1;
    json_value *profile = load_profile(path, 1);
    if (!profile) return -1;
    json_value *selected = json_object_get(profile, "selectedBuilds");
    json_value *item = json_object_get(selected, package_id);
    if (!item) { json_free(profile); return 1; }
    memset(selection, 0, sizeof(*selection));
    int rc = copy_json_string(item, "buildId", selection->build_id, sizeof(selection->build_id)) ||
             copy_json_string(item, "language", selection->language, sizeof(selection->language)) ||
             copy_json_string(item, "languageVersion", selection->language_version, sizeof(selection->language_version)) ||
             copy_json_string(item, "runtime", selection->runtime, sizeof(selection->runtime)) ||
             copy_json_string(item, "runtimeVersion", selection->runtime_version, sizeof(selection->runtime_version));
    json_free(profile);
    return rc ? -1 : 0;
}
