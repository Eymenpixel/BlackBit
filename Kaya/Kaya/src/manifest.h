#ifndef KAYA_MANIFEST_H
#define KAYA_MANIFEST_H

#define MANIFEST_MAX_BUILDS 16

typedef struct {
    char id[64];
    char language[32];
    char language_version[32];
    char runtime[32];
    char runtime_version[32];
    char entry[128];
} manifest_build_t;

typedef struct {
    char name[64];
    char display_name[128];
    char version[32];
    char author[128];
    char description[256];
    int package_format;
    char runtime[32];    /* "native", "python", "sh", ... default "native" */
    char language[32];
    char language_version[32];
    char runtime_version[32];
    char entry[128];
    char icon[128];
    char category[64];

    manifest_build_t builds[MANIFEST_MAX_BUILDS];
    int build_count;

    char **dependencies;  /* other Kaya packages: "name" or "name>=1.0.0" */
    int dep_count;
    char **plugins;       /* Kaya plugins this package needs, e.g. "scratch", "wasm" */
    int plugin_count;
    char **permissions;   /* used by 'kaya run --sandbox' */
    int perm_count;
} manifest_t;

/* Parses manifest JSON text into *out. Returns 0 on success, -1 on error
 * (err set to a short static message). */
int manifest_parse(const char *json_text, manifest_t *out, const char **err);
int manifest_select_build(manifest_t *m, const char *build_id);
void manifest_free(manifest_t *m);
void manifest_print(const manifest_t *m);

#endif
