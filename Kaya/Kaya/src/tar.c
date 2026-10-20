#include "tar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK 512

struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static unsigned long oct2ul(const char *s, size_t len) {
    unsigned long v = 0;
    for (size_t i = 0; i < len && s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') break;
        v = v * 8 + (s[i] - '0');
    }
    return v;
}

static void ul2oct(char *dst, size_t dstlen, unsigned long v) {
    for (int i = (int)dstlen - 2; i >= 0; i--) {
        dst[i] = '0' + (v & 7);
        v >>= 3;
    }
    dst[dstlen - 1] = '\0';
}

static int is_zero_block(const unsigned char *b) {
    for (int i = 0; i < BLOCK; i++) if (b[i] != 0) return 0;
    return 1;
}

/* Rejects absolute paths and any ".." path segment, so a tar entry can
 * never escape the extraction directory (no "/etc/passwd", no
 * "foo/../../bar", no "..", no "foo/.."). This is the fix for the path
 * traversal issue: previously entry names went straight into the
 * destination path with no validation at all. */
static int is_safe_entry_name(const char *name) {
    if (!name || !*name) return 0;
    if (name[0] == '/') return 0;
    size_t len = strlen(name);
    if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
    const char *p = name;
    while ((p = strstr(p, "..")) != NULL) {
        int before_ok = (p == name) || (p[-1] == '/');
        int after_ok = (p[2] == '\0') || (p[2] == '/');
        if (before_ok && after_ok) return 0;
        p += 2;
    }
    return 1;
}

static void mkdirs_for(const char *dest_dir, const char *relpath) {
    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", dest_dir, relpath);
    char tmp[2048];
    strncpy(tmp, full, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

tar_entry *tar_list(const char *archive_path, size_t *count) {
    FILE *f = fopen(archive_path, "rb");
    if (!f) { if (count) *count = 0; return NULL; }
    size_t cap = 8, n = 0;
    tar_entry *entries = malloc(cap * sizeof(tar_entry));
    unsigned char block[BLOCK];
    long offset = 0;
    while (fread(block, 1, BLOCK, f) == BLOCK) {
        offset += BLOCK;
        if (is_zero_block(block)) break;
        struct ustar_header *h = (struct ustar_header *)block;
        unsigned long size = oct2ul(h->size, sizeof(h->size));
        if (h->typeflag[0] == '0' || h->typeflag[0] == '\0') {
            if (n == cap) { cap *= 2; entries = realloc(entries, cap * sizeof(tar_entry)); }
            char full_name[300] = {0};
            if (h->prefix[0]) snprintf(full_name, sizeof(full_name), "%.155s/%.100s", h->prefix, h->name);
            else snprintf(full_name, sizeof(full_name), "%.100s", h->name);
            strncpy(entries[n].name, full_name, sizeof(entries[n].name) - 1);
            entries[n].name[sizeof(entries[n].name) - 1] = '\0';
            entries[n].size = size;
            entries[n].data_offset = offset;
            n++;
        }
        unsigned long padded = ((size + BLOCK - 1) / BLOCK) * BLOCK;
        fseek(f, (long)padded, SEEK_CUR);
        offset += (long)padded;
    }
    fclose(f);
    if (count) *count = n;
    return entries;
}

unsigned char *tar_read_entry(const char *archive_path, const char *entry_name, unsigned long *out_size) {
    size_t count;
    tar_entry *entries = tar_list(archive_path, &count);
    if (!entries) return NULL;
    unsigned char *data = NULL;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(entries[i].name, entry_name) == 0) {
            FILE *f = fopen(archive_path, "rb");
            if (!f) break;
            fseek(f, entries[i].data_offset, SEEK_SET);
            data = malloc(entries[i].size + 1);
            size_t got = fread(data, 1, entries[i].size, f);
            data[got] = '\0';
            if (out_size) *out_size = entries[i].size;
            fclose(f);
            break;
        }
    }
    free(entries);
    return data;
}

/* Fails closed: if ANY entry in the archive has an unsafe name, the whole
 * extraction is aborted before anything is written. Partial extraction of
 * a package that contains one bad entry is worse than refusing it outright. */
int tar_extract_all(const char *archive_path, const char *dest_dir) {
    size_t count;
    tar_entry *entries = tar_list(archive_path, &count);
    if (!entries) return -1;

    for (size_t i = 0; i < count; i++) {
        if (!is_safe_entry_name(entries[i].name)) {
            fprintf(stderr, "tar: güvensiz girdi adı reddedildi: %s\n", entries[i].name);
            free(entries);
            return -1;
        }
    }

    mkdir(dest_dir, 0755);
    FILE *f = fopen(archive_path, "rb");
    if (!f) { free(entries); return -1; }
    for (size_t i = 0; i < count; i++) {
        mkdirs_for(dest_dir, entries[i].name);
        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dest_dir, entries[i].name);
        FILE *out = fopen(full, "wb");
        if (!out) continue;
        fseek(f, entries[i].data_offset, SEEK_SET);
        unsigned char buf[8192];
        unsigned long remaining = entries[i].size;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
            size_t got = fread(buf, 1, chunk, f);
            if (got == 0) break;
            fwrite(buf, 1, got, out);
            remaining -= got;
        }
        fclose(out);
    }
    fclose(f);
    free(entries);
    return 0;
}

struct tar_writer {
    FILE *f;
};

tar_writer *tar_writer_open(const char *archive_path) {
    FILE *f = fopen(archive_path, "wb");
    if (!f) return NULL;
    tar_writer *tw = malloc(sizeof(tar_writer));
    tw->f = f;
    return tw;
}

int tar_writer_add_file(tar_writer *tw, const char *name_in_archive, const char *source_path) {
    FILE *in = fopen(source_path, "rb");
    if (!in) return -1;
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    struct ustar_header h;
    memset(&h, 0, sizeof(h));
    strncpy(h.name, name_in_archive, sizeof(h.name) - 1);
    ul2oct(h.mode, sizeof(h.mode), 0644);
    ul2oct(h.uid, sizeof(h.uid), 0);
    ul2oct(h.gid, sizeof(h.gid), 0);
    ul2oct(h.size, sizeof(h.size), (unsigned long)size);
    ul2oct(h.mtime, sizeof(h.mtime), (unsigned long)0);
    memset(h.chksum, ' ', sizeof(h.chksum));
    h.typeflag[0] = '0';
    memcpy(h.magic, "ustar", 5);
    h.magic[5] = 0;
    h.version[0] = '0';
    h.version[1] = '0';

    unsigned char *raw = (unsigned char *)&h;
    unsigned long sum = 0;
    for (size_t i = 0; i < sizeof(h); i++) sum += raw[i];
    snprintf(h.chksum, sizeof(h.chksum), "%06lo", sum);
    h.chksum[6] = '\0';
    h.chksum[7] = ' ';

    fwrite(&h, 1, sizeof(h), tw->f);

    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, got, tw->f);
    }
    fclose(in);

    long padded = ((size + BLOCK - 1) / BLOCK) * BLOCK;
    long pad = padded - size;
    unsigned char zero[BLOCK] = {0};
    if (pad > 0) fwrite(zero, 1, pad, tw->f);
    return 0;
}

int tar_writer_close(tar_writer *tw) {
    unsigned char zero[BLOCK] = {0};
    fwrite(zero, 1, BLOCK, tw->f);
    fwrite(zero, 1, BLOCK, tw->f);
    fclose(tw->f);
    free(tw);
    return 0;
}
