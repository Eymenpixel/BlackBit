#ifndef KAYA_SANDBOX_H
#define KAYA_SANDBOX_H

/* What a sandboxed app is allowed to touch. Everything not listed here is
 * invisible to it: no home directory, no other apps, no network, no host
 * environment variables. */
typedef struct {
    const char *app_dir;     /* absolute; mounted read-only at the same path   */
    const char *plugin_dir;  /* absolute or NULL; mounted read-only            */
    const char *data_dir;    /* absolute or NULL; private read-write, as /data */
    const char *language;
    const char *language_version;
    const char *runtime;
    const char *runtime_version;
    int network;             /* "network"          : share the host network    */
    int display;             /* "display"          : X11 / Wayland / WSLg      */
    int fs_read;             /* Deprecated: broad cwd access is deliberately denied. */
} sandbox_opts;

/* 1 if a 'bwrap' (bubblewrap) binary is on PATH. */
int sandbox_available(void);

/* Runs cmd (NULL-terminated argv, absolute paths) under bubblewrap with
 * the restrictions in o. Waits for it and returns its exit status.
 * Fails closed: it never falls back to running the command unsandboxed. */
int sandbox_run(const sandbox_opts *o, char *const cmd[]);

#endif
