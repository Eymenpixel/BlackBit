#ifndef KAYA_SETUP_H
#define KAYA_SETUP_H

#include <stddef.h>

typedef struct {
    char build_id[64];
    char language[32];
    char language_version[32];
    char runtime[64];
    char runtime_version[32];
} kaya_build_selection;

/* Creates the core-private setup profile if it does not exist. */
int kaya_setup_init(const char *root_dir);

/* Records the selected build. The private profile is never intended to be
 * mounted into a package process. */
int kaya_setup_set_build(const char *root_dir, const char *package_id,
                         const kaya_build_selection *selection);

/* Returns 0 when a selection exists, 1 when absent, and -1 on invalid data or
 * I/O errors. */
int kaya_setup_get_build(const char *root_dir, const char *package_id,
                         kaya_build_selection *selection);

/* Writes the manager-only setup.json path into out. */
int kaya_setup_path(const char *root_dir, char *out, size_t out_size);

#endif
