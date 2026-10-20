#define _POSIX_C_SOURCE 200809L
#include "sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>

#define MAX_ARGS 256

static volatile pid_t g_child = 0;

/* bwrap runs in its own session (--new-session, which blocks the TIOCSTI
 * keystroke-injection escape), so Ctrl+C no longer reaches it from the
 * terminal. We stay in the terminal's session and forward the signal. */
static void forward_signal(int sig) {
    if (g_child > 0) kill(g_child, sig);
}

int sandbox_available(void) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    char *copy = strdup(path);
    if (!copy) return 0;
    int found = 0;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s/bwrap", dir);
        if (access(cand, X_OK) == 0) { found = 1; break; }
    }
    free(copy);
    return found;
}

int sandbox_run(const sandbox_opts *o, char *const cmd[]) {
    char *a[MAX_ARGS];
    int n = 0, overflow = 0;
#define ADD(s) do { if (n < MAX_ARGS - 1) a[n++] = (char *)(s); else overflow = 1; } while (0)
#define BIND_TRY(p) do { ADD("--ro-bind-try"); ADD(p); ADD(p); } while (0)

    const char *term = getenv("TERM");
    const char *lang = getenv("LANG");

    ADD("bwrap");
    ADD("--unshare-all");                 /* user, ipc, pid, net, uts, cgroup */
    if (o->network) ADD("--share-net");
    ADD("--die-with-parent");
    ADD("--new-session");
    ADD("--clearenv");                    /* host env (tokens, keys...) stays out */
    ADD("--setenv"); ADD("PATH"); ADD("/usr/local/bin:/usr/bin:/bin");
    ADD("--setenv"); ADD("HOME"); ADD(o->data_dir ? "/data" : "/tmp");
    ADD("--setenv"); ADD("TERM"); ADD(term ? term : "xterm");
    ADD("--setenv"); ADD("LANG"); ADD(lang ? lang : "C.UTF-8");
    if (o->language) { ADD("--setenv"); ADD("KAYA_LANGUAGE"); ADD(o->language); }
    if (o->language_version) { ADD("--setenv"); ADD("KAYA_LANGUAGE_VERSION"); ADD(o->language_version); }
    if (o->runtime) { ADD("--setenv"); ADD("KAYA_RUNTIME"); ADD(o->runtime); }
    if (o->runtime_version) { ADD("--setenv"); ADD("KAYA_RUNTIME_VERSION"); ADD(o->runtime_version); }

    /* minimal read-only system: just enough to run python/node/sh/native */
    ADD("--ro-bind"); ADD("/usr"); ADD("/usr");
    BIND_TRY("/bin"); BIND_TRY("/sbin"); BIND_TRY("/lib"); BIND_TRY("/lib64");
    BIND_TRY("/etc/ld.so.cache");
    BIND_TRY("/etc/alternatives");  /* Debian/Ubuntu: /usr/bin/python3 etc. can be symlinks into here */
    ADD("--proc"); ADD("/proc");
    ADD("--dev"); ADD("/dev");
    ADD("--tmpfs"); ADD("/tmp");

    if (o->network) {
        BIND_TRY("/etc/resolv.conf"); BIND_TRY("/etc/hosts");
        BIND_TRY("/etc/nsswitch.conf"); BIND_TRY("/etc/ssl");
        BIND_TRY("/etc/ca-certificates");
    }

    char wl_sock[PATH_MAX];
    if (o->display) {
        BIND_TRY("/tmp/.X11-unix");
        ADD("--dev-bind-try"); ADD("/dev/dri"); ADD("/dev/dri");   /* GPU */
        ADD("--dev-bind-try"); ADD("/dev/dxg"); ADD("/dev/dxg");   /* WSL GPU */
        const char *disp = getenv("DISPLAY");
        if (disp) { ADD("--setenv"); ADD("DISPLAY"); ADD(disp); }
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        const char *wl = getenv("WAYLAND_DISPLAY");
        if (xdg && wl) {
            snprintf(wl_sock, sizeof(wl_sock), "%s/%s", xdg, wl);
            BIND_TRY(wl_sock);
            ADD("--setenv"); ADD("XDG_RUNTIME_DIR"); ADD(xdg);
            ADD("--setenv"); ADD("WAYLAND_DISPLAY"); ADD(wl);
        }
    }

    if (o->data_dir) {
        ADD("--bind"); ADD(o->data_dir); ADD("/data");
        ADD("--setenv"); ADD("KAYA_DATA_DIR"); ADD("/data");
    } else {
        ADD("--unsetenv"); ADD("KAYA_DATA_DIR");
    }

    ADD("--ro-bind"); ADD(o->app_dir); ADD(o->app_dir);
    if (o->plugin_dir) { ADD("--ro-bind"); ADD(o->plugin_dir); ADD(o->plugin_dir); }
    ADD("--chdir"); ADD(o->app_dir);
    ADD("--");
    for (int i = 0; cmd[i]; i++) ADD(cmd[i]);
    a[n] = NULL;

    if (overflow) {
        fprintf(stderr, "kaya: sandbox komutu çok uzun (çok fazla argüman)\n");
        return 1;
    }
    if (o->fs_read) {
        fprintf(stderr,
            "kaya: filesystem.read izni şimdilik kapalı; tüm çalışma dizinini açmak "
            "private/setup.json dosyasını riske atar.\n");
        return 1;
    }
    if (!sandbox_available()) {
        fprintf(stderr,
            "kaya: --sandbox için 'bwrap' (bubblewrap) gerekli, bulunamadı.\n"
            "      kurmak için: sudo apt install bubblewrap\n"
            "      (güvenlik için sandbox'sız çalıştırmaya geçilmedi)\n");
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = forward_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    pid_t pid = fork();
    if (pid < 0) { perror("kaya: fork"); return 1; }
    if (pid == 0) {
        execvp("bwrap", a);
        perror("kaya: bwrap çalıştırılamadı");
        _exit(127);
    }
    g_child = pid;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { perror("kaya: waitpid"); return 1; }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#undef ADD
#undef BIND_TRY
}
