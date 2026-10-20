#define _POSIX_C_SOURCE 200809L
#include "manifest.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int manifest_parse(const char *json_text, manifest_t *out, const char **err) {
    memset(out, 0, sizeof(*out));
    const char *jerr = NULL;
    json_value *root = json_parse(json_text, &jerr);
    if (!root) { if (err) *err = jerr ? jerr : "invalid JSON"; return -1; }
    if (root->type != JSON_OBJECT) { if (err) *err = "manifest root must be an object"; json_free(root); return -1; }

    const char *name = json_get_string(root, "name", NULL);
    if (!name || !*name) { if (err) *err = "manifest missing required field: name"; json_free(root); return -1; }
    const char *entry = json_get_string(root, "entry", "");
    json_value *declared_builds = json_object_get(root, "builds");
    if ((!entry || !*entry) && !declared_builds) {
        if (err) *err = "manifest missing required field: entry";
        json_free(root);
        return -1;
    }

    strncpy(out->name, name, sizeof(out->name) - 1);
    strncpy(out->entry, entry, sizeof(out->entry) - 1);
    strncpy(out->display_name, json_get_string(root, "displayName", name), sizeof(out->display_name) - 1);
    strncpy(out->version, json_get_string(root, "version", "0.0.0"), sizeof(out->version) - 1);
    strncpy(out->author, json_get_string(root, "author", ""), sizeof(out->author) - 1);
    strncpy(out->description, json_get_string(root, "description", ""), sizeof(out->description) - 1);
    strncpy(out->runtime, json_get_string(root, "runtime", "native"), sizeof(out->runtime) - 1);
    strncpy(out->language, json_get_string(root, "language", ""), sizeof(out->language) - 1);
    strncpy(out->language_version, json_get_string(root, "languageVersion", "*"), sizeof(out->language_version) - 1);
    strncpy(out->runtime_version, json_get_string(root, "runtimeVersion", "*"), sizeof(out->runtime_version) - 1);
    strncpy(out->icon, json_get_string(root, "icon", ""), sizeof(out->icon) - 1);
    strncpy(out->category, json_get_string(root, "category", ""), sizeof(out->category) - 1);
    out->package_format = (int)json_get_number(root, "packageFormat", 1);

    /* Format 2 packages may ship several language/runtime builds. Keep every
     * variant in the manifest so setup can select the one installed locally. */
    json_value *builds = json_object_get(root, "builds");
    if (builds) {
        if (builds->type != JSON_ARRAY || builds->u.array.count == 0 ||
            builds->u.array.count > MANIFEST_MAX_BUILDS) {
            if (err) *err = "builds must contain 1..16 build objects";
            json_free(root);
            return -1;
        }
        for (size_t i = 0; i < builds->u.array.count; i++) {
            json_value *b = builds->u.array.items[i];
            if (!b || b->type != JSON_OBJECT) {
                if (err) *err = "each build must be an object";
                json_free(root);
                return -1;
            }
            manifest_build_t *dst = &out->builds[i];
            const char *id = json_get_string(b, "id", NULL);
            const char *be = json_get_string(b, "entry", NULL);
            if (!id || !*id || !be || !*be) {
                if (err) *err = "each build requires id and entry";
                json_free(root);
                return -1;
            }
            snprintf(dst->id, sizeof(dst->id), "%s", id);
            snprintf(dst->entry, sizeof(dst->entry), "%s", be);
            snprintf(dst->language, sizeof(dst->language), "%s", json_get_string(b, "language", out->language));
            snprintf(dst->language_version, sizeof(dst->language_version), "%s", json_get_string(b, "languageVersion", out->language_version));
            snprintf(dst->runtime, sizeof(dst->runtime), "%s", json_get_string(b, "runtime", out->runtime));
            snprintf(dst->runtime_version, sizeof(dst->runtime_version), "%s", json_get_string(b, "runtimeVersion", out->runtime_version));
            for (size_t j = 0; j < i; j++) {
                if (strcmp(out->builds[j].id, dst->id) == 0) {
                    if (err) *err = "build ids must be unique";
                    json_free(root);
                    return -1;
                }
            }
            out->build_count++;
        }
    }

    size_t dep_n = 0;
    const char **deps = json_get_string_array(root, "dependencies", &dep_n);
    out->dep_count = (int)dep_n;
    if (dep_n > 0) {
        out->dependencies = malloc(dep_n * sizeof(char *));
        for (size_t i = 0; i < dep_n; i++) out->dependencies[i] = strdup(deps[i]);
    }
    free(deps);

    size_t plug_n = 0;
    const char **plugs = json_get_string_array(root, "plugins", &plug_n);
    out->plugin_count = (int)plug_n;
    if (plug_n > 0) {
        out->plugins = malloc(plug_n * sizeof(char *));
        for (size_t i = 0; i < plug_n; i++) out->plugins[i] = strdup(plugs[i]);
    }
    free(plugs);

    size_t perm_n = 0;
    const char **perms = json_get_string_array(root, "permissions", &perm_n);
    out->perm_count = (int)perm_n;
    if (perm_n > 0) {
        out->permissions = malloc(perm_n * sizeof(char *));
        for (size_t i = 0; i < perm_n; i++) out->permissions[i] = strdup(perms[i]);
    }
    free(perms);

    json_free(root);
    if (err) *err = NULL;
    return 0;
}

int manifest_select_build(manifest_t *m, const char *build_id) {
    if (!m || !build_id || !*build_id) return -1;
    for (int i = 0; i < m->build_count; i++) {
        manifest_build_t *b = &m->builds[i];
        if (strcmp(b->id, build_id) != 0) continue;
        snprintf(m->entry, sizeof(m->entry), "%s", b->entry);
        snprintf(m->language, sizeof(m->language), "%s", b->language);
        snprintf(m->language_version, sizeof(m->language_version), "%s", b->language_version);
        snprintf(m->runtime, sizeof(m->runtime), "%s", b->runtime);
        snprintf(m->runtime_version, sizeof(m->runtime_version), "%s", b->runtime_version);
        return 0;
    }
    return -1;
}

void manifest_free(manifest_t *m) {
    for (int i = 0; i < m->dep_count; i++) free(m->dependencies[i]);
    free(m->dependencies);
    for (int i = 0; i < m->plugin_count; i++) free(m->plugins[i]);
    free(m->plugins);
    for (int i = 0; i < m->perm_count; i++) free(m->permissions[i]);
    free(m->permissions);
}

void manifest_print(const manifest_t *m) {
    printf("  name        : %s\n", m->name);
    printf("  displayName : %s\n", m->display_name);
    printf("  version     : %s\n", m->version);
    if (*m->author) printf("  author      : %s\n", m->author);
    if (*m->description) printf("  description : %s\n", m->description);
    printf("  runtime     : %s\n", m->runtime);
    if (*m->language) printf("  language    : %s\n", m->language);
    if (*m->language_version && strcmp(m->language_version, "*") != 0)
        printf("  languageVersion: %s\n", m->language_version);
    if (*m->runtime_version && strcmp(m->runtime_version, "*") != 0)
        printf("  runtimeVersion : %s\n", m->runtime_version);
    printf("  entry       : %s\n", m->entry);
    if (*m->category) printf("  category    : %s\n", m->category);
    if (m->dep_count > 0) {
        printf("  dependencies:\n");
        for (int i = 0; i < m->dep_count; i++) printf("    - %s\n", m->dependencies[i]);
    }
    if (m->plugin_count > 0) {
        printf("  plugins     :\n");
        for (int i = 0; i < m->plugin_count; i++) printf("    - %s\n", m->plugins[i]);
    }
    if (m->perm_count > 0) {
        printf("  permissions :\n");
        for (int i = 0; i < m->perm_count; i++) printf("    - %s\n", m->permissions[i]);
    }
}
