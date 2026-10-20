#ifndef KAYA_TAR_H
#define KAYA_TAR_H

#include <stddef.h>

typedef struct {
    char name[300];
    unsigned long size;
    long data_offset;
} tar_entry;

typedef struct tar_writer tar_writer;

/* Lists the regular-file entries in a USTAR archive. Caller must free()
 * the returned array. Returns NULL (and sets *count = 0) on error. */
tar_entry *tar_list(const char *archive_path, size_t *count);

/* Reads a single named entry's data into a newly malloc'd, NUL-terminated
 * buffer. Caller must free() it. Returns NULL if not found or on error. */
unsigned char *tar_read_entry(const char *archive_path, const char *entry_name, unsigned long *out_size);

/* Extracts every entry into dest_dir, preserving directory structure.
 * Rejects the whole archive (returns -1, writes nothing) if any entry
 * name is unsafe (absolute path or contains ".."). Returns 0 on success. */
int tar_extract_all(const char *archive_path, const char *dest_dir);

/* Streaming writer for building a new USTAR archive. */
tar_writer *tar_writer_open(const char *archive_path);
int tar_writer_add_file(tar_writer *tw, const char *name_in_archive, const char *source_path);
int tar_writer_close(tar_writer *tw);

#endif
