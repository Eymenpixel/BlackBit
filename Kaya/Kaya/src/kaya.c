#define _XOPEN_SOURCE 700  /* realpath() needs XSI, _POSIX_C_SOURCE alone is not enough */
/* kaya - package manager for .kay packages (BlackBit-style)
 *
 * Usage:
 *   kaya --install <isim>                    repodan paket kurar
 *   kaya --install -file <dosya.kay>         yerel .kay dosyasını kurar
 *   kaya --install -needed <dosya.kay>       kurar; eksik bağımlılık/plugin'leri
 *                                            (Y/N sorarak) depodan indirir
 *   kaya --install -plugin <isim>            depodan plugin kurar
 *   kaya --install -fileplugin <dosya.kyp>   yerel .kyp dosyasını kurar
 *   kaya --remove <isim>
 *   kaya --remove -plugin <isim>
 *   kaya --upgrade
 *   kaya --relook
 *   kaya list
 *   kaya info <isim>
 *   kaya run [--sandbox] <isim> [-- args...]
 *
 * All commands accept --root DIR to pick where apps get installed
 * (default: ./kaya_root). Layout:
 *   <root>/apps/<name>/manifest.json
 *   <root>/apps/<name>/<...package files...>
 *   <root>/plugins/<name>/<...plugin files...>
 *   <root>/data/<name>/       private writable dir ("filesystem.write")
 *   <root>/cache/             lists written by --relook
 *
 * Runtimes: native, python, node, sh/shell and html are built in. Every
 * other runtime is run by a plugin: a plugin directory may contain an
 * executable file named "run" at its root, and kaya calls
 *     <plugins>/<plugin>/run <entry-abspath> [args...]
 * (no shell, execvp directly). "sb3" is run by the "scratch" plugin
 * (falling back to pystage when that plugin is absent), "wasm" by the
 * "wasm" plugin, and any other runtime X by a plugin called X.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "tar.h"
#include "manifest.h"
#include "json.h"
#include "sandbox.h"
#include "setup.h"

#define DEFAULT_ROOT "./kaya_root"
#define KAYA_VERSION "1.0-beta.3"
#define MAX_DEP_DEPTH 8

/* Repo layout: every package/plugin has its OWN folder, and the file inside
 * can be named anything. We ask GitHub's "contents" API to list the folder,
 * pick whichever file ends in .kay / .kyp, and download its download_url.
 * NOTE: GitHub paths are case-sensitive: the repo has "Packages" (upper
 * case) and "Plugins" (capital P). */
#define PACKAGES_API_BASE "https://api.github.com/repos/Eymenpixel/Blackbit/contents/Kaya/Packages"
#define PLUGINS_API_BASE  "https://api.github.com/repos/Eymenpixel/Blackbit/contents/Kaya/Plugins"
#define RELEASES_API      "https://api.github.com/repos/Eymenpixel/Kaya/releases/latest"
#define RELEASES_URL      "https://github.com/Eymenpixel/Kaya/releases/latest"

static char root_dir[512];
static char browser_override[128] = "";
static int g_sandbox = 1;  /* package code always runs in the fail-closed sandbox */
static int g_needed = 0;   /* -needed   : offer to install missing deps/plugins */
static int g_depth = 0;    /* recursion guard for dependency cycles          */

static void apps_dir(char *out, size_t n) { snprintf(out, n, "%s/apps", root_dir); }
static void app_dir(const char *name, char *out, size_t n) { snprintf(out, n, "%s/apps/%s", root_dir, name); }
static void data_dir(const char *name, char *out, size_t n) { snprintf(out, n, "%s/data/%s", root_dir, name); }
static void manifest_path(const char *name, char *out, size_t n) { snprintf(out, n, "%s/apps/%s/manifest.json", root_dir, name); }
static void plugins_dir(char *out, size_t n) { snprintf(out, n, "%s/plugins", root_dir); }
static void plugin_dir(const char *name, char *out, size_t n) { snprintf(out, n, "%s/plugins/%s", root_dir, name); }

static int cmd_install(const char *kay_file);
static int cmd_install_repo(const char *name);
static int cmd_install_plugin_repo(const char *name);

/* --- validation helpers ----------------------------------------------- */

/* A single path component (manifest "name", plugin names, runtime names
 * used as plugin names): no '/', not "." or "..". */
static int is_safe_component(const char *s) {
    if (!s || !*s) return 0;
    if (strchr(s, '/')) return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return 0;
    return 1;
}

/* A relative path with no traversal (manifest "entry"). Conservative. */
static int is_safe_rel_path(const char *s) {
    if (!s || !*s) return 0;
    if (s[0] == '/') return 0;
    if (strstr(s, "..")) return 0;
    return 1;
}

static int is_builtin_runtime(const char *rt) {
    return strcmp(rt, "native") == 0 || strcmp(rt, "python") == 0 ||
           strcmp(rt, "node") == 0 || strcmp(rt, "sh") == 0 ||
           strcmp(rt, "shell") == 0 || strcmp(rt, "html") == 0;
}

/* Name of the plugin that runs a runtime, NULL for built-in runtimes. */
static const char *runtime_plugin_name(const char *rt) {
    if (is_builtin_runtime(rt)) return NULL;
    if (strcmp(rt, "sb3") == 0) return "scratch";
    return rt;
}

static int has_perm(const manifest_t *m, const char *perm) {
    for (int i = 0; i < m->perm_count; i++)
        if (strcmp(m->permissions[i], perm) == 0) return 1;
    return 0;
}

/* --- subprocess helpers (no shell is ever involved) --------------------- */

static int run_argv(char *const argv[], const char *log_path) {
    pid_t pid = fork();
    if (pid < 0) { perror("kaya: fork"); return -1; }
    if (pid == 0) {
        if (log_path) {
            int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
        execvp(argv[0], argv);
        _exit(127); /* only reached if execvp failed */
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static int download_file(const char *url, const char *dest_path) {
    /* -A: GitHub's API answers 403 to requests without a User-Agent. */
    char *argv[] = { "curl", "-fsSL", "-A", "kaya-package-manager", "-o", (char *)dest_path, (char *)url, NULL };
    int rc = run_argv(argv, NULL);
    if (rc != 0) {
        fprintf(stderr, "kaya: indirme başarısız: %s\n      (adres yok, GitHub API limiti dolmuş ya da curl kurulu değil)\n", url);
        return -1;
    }
    return 0;
}

static int confirm(const char *question) {
    printf("%s [Y/N]: ", question);
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof(line), stdin)) { printf("\n"); return 0; }
    return line[0] == 'y' || line[0] == 'Y' || line[0] == 'e' || line[0] == 'E';
}

/* --- file helpers --------------------------------------------------------- */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* Downloads a GitHub "contents" listing of a folder and returns it as a
 * parsed JSON array (caller json_free()s it). NULL if the folder does not
 * exist, is a file, or on any network/parse error. */
static json_value *github_fetch_listing(const char *api_url) {
    char tmp[] = "/tmp/kaya-listing.XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { perror("kaya: mkstemp"); return NULL; }
    close(fd);
    if (download_file(api_url, tmp) != 0) { unlink(tmp); return NULL; }
    char *text = read_file(tmp);
    unlink(tmp);
    if (!text) return NULL;
    const char *jerr;
    json_value *root = json_parse(text, &jerr);
    free(text);
    if (!root) {
        fprintf(stderr, "kaya: depo yanıtı ayrıştırılamadı: %s\n", jerr ? jerr : "?");
        return NULL;
    }
    if (root->type != JSON_ARRAY) {
        /* GitHub answers a 404 / a plain file with an object, not an array */
        json_free(root);
        return NULL;
    }
    return root;
}

/* malloc'd download_url of the first file in the folder whose name ends in
 * `ext` (".kay" / ".kyp"); NULL if none. Caller free()s. */
static char *github_dir_find_download_url(const char *api_url, const char *ext) {
    json_value *root = github_fetch_listing(api_url);
    if (!root) return NULL;
    char *result = NULL;
    size_t elen = strlen(ext);
    for (size_t i = 0; i < root->u.array.count; i++) {
        json_value *item = root->u.array.items[i];
        const char *name = json_get_string(item, "name", NULL);
        if (!name) continue;
        size_t nlen = strlen(name);
        if (nlen > elen && strcmp(name + nlen - elen, ext) == 0) {
            const char *durl = json_get_string(item, "download_url", NULL);
            if (durl) { result = strdup(durl); break; }
        }
    }
    json_free(root);
    return result;
}

static int load_installed_manifest(const char *name, manifest_t *m) {
    char path[600];
    manifest_path(name, path, sizeof(path));
    char *text = read_file(path);
    if (!text) return -1;
    const char *err;
    int rc = manifest_parse(text, m, &err);
    free(text);
    return rc;
}

static int version_cmp(const char *a, const char *b) {
    while (*a || *b) {
        long na = 0, nb = 0;
        while (*a && *a != '.') { na = na * 10 + (*a - '0'); a++; }
        while (*b && *b != '.') { nb = nb * 10 + (*b - '0'); b++; }
        if (na != nb) return na < nb ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static void parse_dep(const char *spec, char *name, size_t nlen, char *op, size_t olen, char *ver, size_t vlen) {
    const char *ops[] = { ">=", "<=", "==", ">", "<" };
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        const char *pos = strstr(spec, ops[i]);
        if (pos) {
            size_t nl = pos - spec;
            snprintf(name, nlen, "%.*s", (int)nl, spec);
            snprintf(op, olen, "%s", ops[i]);
            snprintf(ver, vlen, "%s", pos + strlen(ops[i]));
            return;
        }
    }
    snprintf(name, nlen, "%s", spec);
    op[0] = '\0';
    ver[0] = '\0';
}

static int dep_satisfied(const char *installed_version, const char *op, const char *required_version) {
    int c = version_cmp(installed_version, required_version);
    if (strcmp(op, ">=") == 0) return c >= 0;
    if (strcmp(op, "<=") == 0) return c <= 0;
    if (strcmp(op, "==") == 0) return c == 0;
    if (strcmp(op, ">") == 0) return c > 0;
    if (strcmp(op, "<") == 0) return c < 0;
    return 1;
}

static int is_installed(const char *name) {
    char path[600];
    manifest_path(name, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_plugin_installed(const char *name) {
    char p[600];
    plugin_dir(name, p, sizeof(p));
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int rm_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *ent;
        int rc = 0;
        while ((ent = readdir(d))) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[1024];
            int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (n < 0 || (size_t)n >= sizeof(child) || rm_recursive(child) != 0)
                rc = -1;
        }
        closedir(d);
        if (rc == 0 && rmdir(path) != 0) rc = -1;
        return rc;
    } else {
        return unlink(path);
    }
}

static void mkdirs(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* --- -needed: fetch missing dependencies / plugins --------------------- */

/* Loads the installed manifest of dependency `dname`. With -needed, asks
 * and installs it from the repo first if it is missing. 0 = loaded. */
static int load_or_fetch_dep(const char *dname, const char *spec, manifest_t *dep_m) {
    if (load_installed_manifest(dname, dep_m) == 0) return 0;
    if (!g_needed || !is_safe_component(dname)) return -1;
    char q[300];
    snprintf(q, sizeof(q), "Bağımlılık '%s' kurulu değil (gerekli: %s). Depodan indirilsin mi?", dname, spec);
    if (!confirm(q)) return -1;
    if (g_depth >= MAX_DEP_DEPTH) {
        fprintf(stderr, "kaya: bağımlılık zinciri çok derin (döngü olabilir): %s\n", dname);
        return -1;
    }
    g_depth++;
    int rc = cmd_install_repo(dname);
    g_depth--;
    if (rc != 0) return -1;
    return load_installed_manifest(dname, dep_m);
}

/* 0 if the plugin is installed (after offering to fetch it with -needed). */
static int ensure_plugin(const char *name) {
    if (is_plugin_installed(name)) return 0;
    if (!g_needed) return 1;
    char q[300];
    snprintf(q, sizeof(q), "'%s' plugini kurulu değil. Depodan indirilsin mi?", name);
    if (!confirm(q)) return 1;
    if (cmd_install_plugin_repo(name) != 0) return 1;
    return is_plugin_installed(name) ? 0 : 1;
}

/* --- install / remove (apps) --------------------------------------------- */

static int cmd_install(const char *kay_file) {
    unsigned long msize;
    unsigned char *manifest_text = tar_read_entry(kay_file, "manifest.json", &msize);
    if (!manifest_text) {
        fprintf(stderr, "kaya: %s içinde manifest.json bulunamadı\n", kay_file);
        return 1;
    }
    manifest_t m;
    const char *err;
    if (manifest_parse((char *)manifest_text, &m, &err) != 0) {
        fprintf(stderr, "kaya: manifest.json geçersiz: %s\n", err);
        free(manifest_text);
        return 1;
    }
    free(manifest_text);

    if (!*m.language) snprintf(m.language, sizeof(m.language), "%s", m.runtime);

    /* A v2 manifest may provide multiple runtimes. Until the setup flow has
     * an interactive runtime inventory, prefer an explicit `default` build,
     * otherwise use its first declared build deterministically. */
    if (m.build_count > 0) {
        int chosen = -1;
        for (int i = 0; i < m.build_count; i++)
            if (strcmp(m.builds[i].id, "default") == 0) { chosen = i; break; }
        if (chosen < 0) chosen = 0;
        if (manifest_select_build(&m, m.builds[chosen].id) != 0) {
            fprintf(stderr, "kaya: manifest build seçilemedi\n");
            manifest_free(&m);
            return 1;
        }
    }

    /* Everything below comes from the package: validate before touching
     * the filesystem. */
    if (!is_safe_component(m.name)) {
        fprintf(stderr, "kaya: manifestteki 'name' alanı geçersiz: %s\n", m.name);
        manifest_free(&m);
        return 1;
    }
    if (!is_safe_rel_path(m.entry)) {
        fprintf(stderr, "kaya: manifestteki 'entry' alanı geçersiz: %s\n", m.entry);
        manifest_free(&m);
        return 1;
    }
    const char *rplugin = runtime_plugin_name(m.runtime);  /* NULL = built-in */
    if (rplugin && !is_safe_component(rplugin)) {
        fprintf(stderr, "kaya: manifestteki 'runtime' alanı geçersiz: %s\n", m.runtime);
        manifest_free(&m);
        return 1;
    }
    for (int i = 0; i < m.plugin_count; i++) {
        if (!is_safe_component(m.plugins[i])) {
            fprintf(stderr, "kaya: manifestteki 'plugins' girdisi geçersiz: %s\n", m.plugins[i]);
            manifest_free(&m);
            return 1;
        }
    }

    printf("Kuruluyor: %s (%s) v%s\n", m.display_name, m.name, m.version);

    for (int i = 0; i < m.dep_count; i++) {
        char dname[64], dop[4], dver[32];
        parse_dep(m.dependencies[i], dname, sizeof(dname), dop, sizeof(dop), dver, sizeof(dver));
        manifest_t dep_m;
        if (load_or_fetch_dep(dname, m.dependencies[i], &dep_m) != 0) {
            fprintf(stderr, "kaya: bağımlılık eksik: %s (gerekli: %s)\n", dname, m.dependencies[i]);
            if (!g_needed)
                fprintf(stderr, "      eksikleri otomatik kurmak için: kaya --install -needed <dosya.kay>\n");
            manifest_free(&m);
            return 1;
        }
        int ok = dep_satisfied(dep_m.version, dop, dver);
        if (!ok) {
            fprintf(stderr, "kaya: bağımlılık uyuşmuyor: %s (gerekli: %s, kurulu: %s)\n",
                    dname, m.dependencies[i], dep_m.version);
            manifest_free(&dep_m);
            manifest_free(&m);
            return 1;
        }
        manifest_free(&dep_m);
    }

    /* plugins the manifest explicitly lists are required, like dependencies */
    for (int i = 0; i < m.plugin_count; i++) {
        if (ensure_plugin(m.plugins[i]) != 0) {
            fprintf(stderr, "kaya: gerekli plugin eksik: %s\n"
                            "      kurmak için: kaya --install -plugin %s  (ya da: kaya --install -needed <dosya.kay>)\n",
                    m.plugins[i], m.plugins[i]);
            manifest_free(&m);
            return 1;
        }
    }

    /* the plugin the runtime itself implies (sb3 -> scratch, wasm -> wasm):
     * with -needed we offer it; otherwise the package still installs and
     * we only warn at the end. */
    int rt_plugin_missing = 0;
    if (rplugin && !is_plugin_installed(rplugin)) {
        if (g_needed) ensure_plugin(rplugin);
        rt_plugin_missing = !is_plugin_installed(rplugin);
    }

    if (kaya_setup_init(root_dir) != 0) {
        fprintf(stderr, "kaya: private setup profili oluşturulamadı\n");
        manifest_free(&m);
        return 1;
    }

    char adir[600];
    app_dir(m.name, adir, sizeof(adir));
    char apps[600];
    apps_dir(apps, sizeof(apps));
    mkdirs(apps);
    if (rm_recursive(adir) != 0) {
        fprintf(stderr, "kaya: eski paket kaldırılamadı: %s\n", adir);
        manifest_free(&m);
        return 1;
    }
    if (tar_extract_all(kay_file, adir) != 0) {
        fprintf(stderr, "kaya: %s açılamadı\n", kay_file);
        manifest_free(&m);
        return 1;
    }

    if (strcmp(m.runtime, "native") == 0) {
        char entry_path[800];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", adir, m.entry);
        chmod(entry_path, 0755);
    }

    kaya_build_selection selected = {0};
    snprintf(selected.build_id, sizeof(selected.build_id), "%s",
             m.build_count > 0 ? m.builds[0].id : "default");
    for (int i = 0; i < m.build_count; i++)
        if (strcmp(m.builds[i].id, "default") == 0)
            snprintf(selected.build_id, sizeof(selected.build_id), "%s", m.builds[i].id);
    snprintf(selected.language, sizeof(selected.language), "%s", m.language);
    snprintf(selected.language_version, sizeof(selected.language_version), "%s", m.language_version);
    snprintf(selected.runtime, sizeof(selected.runtime), "%s", m.runtime);
    snprintf(selected.runtime_version, sizeof(selected.runtime_version), "%s", m.runtime_version);
    if (kaya_setup_set_build(root_dir, m.name, &selected) != 0) {
        fprintf(stderr, "kaya: %s için private runtime seçimi kaydedilemedi\n", m.name);
        rm_recursive(adir);
        manifest_free(&m);
        return 1;
    }

    printf("Kurulum tamamlandı: %s/%s\n", apps, m.name);
    if (rt_plugin_missing) {
        if (strcmp(m.runtime, "sb3") == 0)
            printf("Not: 'scratch' plugini kurulu değil, çalıştırınca pystage'e düşülür.\n"
                   "     TurboWarp için: kaya --install -plugin scratch\n");
        else
            printf("Uyarı: bu paketi çalıştırmak için '%s' plugini gerekli.\n"
                   "       kurmak için: kaya --install -plugin %s\n", rplugin, rplugin);
    }
    manifest_free(&m);
    return 0;
}

static int cmd_install_repo(const char *name) {
    if (!is_safe_component(name)) {
        fprintf(stderr, "kaya: geçersiz paket adı: %s\n", name);
        return 1;
    }
    char api_url[700];
    snprintf(api_url, sizeof(api_url), PACKAGES_API_BASE "/%s", name);
    printf("Aranıyor: Packages/%s\n", name);
    char *download_url = github_dir_find_download_url(api_url, ".kay");
    if (!download_url) {
        fprintf(stderr, "kaya: '%s' depoda bulunamadı (Packages/%s içinde .kay dosyası yok)\n", name, name);
        return 1;
    }
    char tmp[] = "/tmp/kaya-dl.XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { perror("kaya: mkstemp"); free(download_url); return 1; }
    close(fd);
    printf("İndiriliyor: %s\n", download_url);
    int rc;
    if (download_file(download_url, tmp) != 0) rc = 1;
    else rc = cmd_install(tmp);
    free(download_url);
    unlink(tmp);
    return rc;
}

static int cmd_remove(const char *name) {
    if (!is_installed(name)) {
        fprintf(stderr, "kaya: '%s' kurulu değil\n", name);
        return 1;
    }
    char apps[600];
    apps_dir(apps, sizeof(apps));
    DIR *d = opendir(apps);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d))) {
            if (ent->d_name[0] == '.' || strcmp(ent->d_name, name) == 0) continue;
            manifest_t other;
            if (load_installed_manifest(ent->d_name, &other) == 0) {
                for (int i = 0; i < other.dep_count; i++) {
                    char dname[64], dop[4], dver[32];
                    parse_dep(other.dependencies[i], dname, sizeof(dname), dop, sizeof(dop), dver, sizeof(dver));
                    if (strcmp(dname, name) == 0) {
                        fprintf(stderr, "kaya: '%s' kaldırılamıyor, '%s' buna bağımlı\n", name, ent->d_name);
                        manifest_free(&other);
                        closedir(d);
                        return 1;
                    }
                }
                manifest_free(&other);
            }
        }
        closedir(d);
    }
    char adir[600];
    app_dir(name, adir, sizeof(adir));
    if (rm_recursive(adir) != 0) {
        fprintf(stderr, "kaya: paket dosyaları kaldırılamadı: %s\n", adir);
        return 1;
    }
    printf("Kaldırıldı: %s\n", name);
    return 0;
}

/* --- install / remove (plugins) ---------------------------------------------
 * A plugin is a USTAR archive (.kyp) extracted under <root>/plugins/<name>/.
 * No manifest is required. If it contains a file called "run" it is made
 * executable and becomes the runner for the runtime of the same name (see
 * the header comment). */

static int cmd_install_plugin_file(const char *kyp_file, const char *name_hint) {
    char name[64];
    if (name_hint && *name_hint) {
        snprintf(name, sizeof(name), "%s", name_hint);
    } else {
        const char *base = strrchr(kyp_file, '/');
        base = base ? base + 1 : kyp_file;
        snprintf(name, sizeof(name), "%s", base);
        char *dot = strrchr(name, '.');
        if (dot && strcmp(dot, ".kyp") == 0) *dot = '\0';
    }
    if (!is_safe_component(name)) {
        fprintf(stderr, "kaya: geçersiz plugin adı: %s\n", name);
        return 1;
    }
    char pdir[600];
    plugin_dir(name, pdir, sizeof(pdir));
    char pdirs[600];
    plugins_dir(pdirs, sizeof(pdirs));
    mkdirs(pdirs);
    if (rm_recursive(pdir) != 0) {
        fprintf(stderr, "kaya: eski plugin kaldırılamadı: %s\n", pdir);
        return 1;
    }
    if (tar_extract_all(kyp_file, pdir) != 0) {
        fprintf(stderr, "kaya: %s açılamadı (bozuk ya da güvensiz plugin)\n", kyp_file);
        return 1;
    }
    char run_path[700];
    snprintf(run_path, sizeof(run_path), "%s/run", pdir);
    chmod(run_path, 0755); /* no-op if the plugin has no "run" file */
    printf("Plugin kuruldu: %s\n", name);
    return 0;
}

static int cmd_install_plugin_repo(const char *name) {
    if (!is_safe_component(name)) {
        fprintf(stderr, "kaya: geçersiz plugin adı: %s\n", name);
        return 1;
    }
    char api_url[700];
    snprintf(api_url, sizeof(api_url), PLUGINS_API_BASE "/%s", name);
    printf("Aranıyor: Plugins/%s\n", name);
    char *download_url = github_dir_find_download_url(api_url, ".kyp");
    if (!download_url) {
        fprintf(stderr, "kaya: plugin '%s' depoda bulunamadı (Plugins/%s içinde .kyp dosyası yok)\n", name, name);
        return 1;
    }
    char tmp[] = "/tmp/kaya-dl.XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { perror("kaya: mkstemp"); free(download_url); return 1; }
    close(fd);
    printf("İndiriliyor: %s\n", download_url);
    int rc;
    if (download_file(download_url, tmp) != 0) rc = 1;
    else rc = cmd_install_plugin_file(tmp, name);
    free(download_url);
    unlink(tmp);
    return rc;
}

static int cmd_remove_plugin(const char *name) {
    if (!is_safe_component(name) || !is_plugin_installed(name)) {
        fprintf(stderr, "kaya: plugin '%s' kurulu değil\n", name);
        return 1;
    }
    char pdir[600];
    plugin_dir(name, pdir, sizeof(pdir));
    if (rm_recursive(pdir) != 0) {
        fprintf(stderr, "kaya: plugin dosyaları kaldırılamadı: %s\n", pdir);
        return 1;
    }
    printf("Plugin kaldırıldı: %s\n", name);
    return 0;
}

/* --- list / info ----------------------------------------------------------- */

static int cmd_list(void) {
    char apps[600];
    apps_dir(apps, sizeof(apps));
    DIR *d = opendir(apps);
    if (!d) { printf("Kurulu paket yok.\n"); return 0; }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        manifest_t m;
        if (load_installed_manifest(ent->d_name, &m) == 0) {
            printf("%-16s %-10s %s\n", m.name, m.version, m.display_name);
            manifest_free(&m);
            count++;
        }
    }
    closedir(d);
    if (count == 0) printf("Kurulu paket yok.\n");
    return 0;
}

static int cmd_info(const char *name) {
    manifest_t m;
    if (!is_safe_component(name) || load_installed_manifest(name, &m) != 0) {
        fprintf(stderr, "kaya: '%s' kurulu değil\n", name);
        return 1;
    }
    manifest_print(&m);
    manifest_free(&m);
    return 0;
}

/* --- run ------------------------------------------------------------------------
 * Opens `path` in a browser. If browser_override is set (--browser NAME),
 * that binary is used. Otherwise a handful of common openers/browsers are
 * tried in turn. */
static int run_html(const char *path) {
    if (browser_override[0]) {
        char *args[] = { browser_override, (char *)path, NULL };
        execvp(browser_override, args);
        fprintf(stderr, "kaya: '%s' taraycısı bulunamadı/çalıştırılamadı\n", browser_override);
        return 1;
    }
    const char *candidates[] = { "xdg-open", "sensible-browser", "firefox", "chromium", "google-chrome", NULL };
    for (int i = 0; candidates[i]; i++) {
        char *args[] = { (char *)candidates[i], (char *)path, NULL };
        execvp(candidates[i], args);
    }
    fprintf(stderr, "kaya: açılacak bir tarayıcı bulunamadı, --browser <isim> ile belirt\n");
    return 1;
}

/* Fallback for sb3 when the "scratch" plugin is not installed: plays an
 * .sb3 via pystage/pygame (fork+execvp, no shell). */
static int run_sb3(const char *name, const char *sb3_path, int argc, char **argv) {
    char tmpdir[600];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/kaya-sb3-%s", name);
    rm_recursive(tmpdir);

    char abs_sb3[4096];
    if (!realpath(sb3_path, abs_sb3)) {
        fprintf(stderr, "kaya: '%s' bulunamadı\n", sb3_path);
        return 1;
    }

    char log_path[700];
    snprintf(log_path, sizeof(log_path), "/tmp/kaya-sb3-%s.log", name);

    char *conv_argv[] = {
        "python3", "-m", "pystage.convert.sb3",
        abs_sb3, "-l", "en", "-d", tmpdir, "-vv", NULL
    };
    int rc = run_argv(conv_argv, log_path);
    if (rc != 0) {
        fprintf(stderr,
            "kaya: sb3 dönüştürülemedi. 'pystage' kurulu mu? (pip install pystage pygame)\n"
            "      ayrıntı için: %s\n"
            "      daha iyi uyumluluk için: kaya --install -plugin scratch\n", log_path);
        return 1;
    }

    char found[950] = "";
    char candidate[950];
    snprintf(candidate, sizeof(candidate), "%s/main.py", tmpdir);
    struct stat st;
    if (stat(candidate, &st) == 0) {
        snprintf(found, sizeof(found), "%s", candidate);
    } else {
        DIR *d = opendir(tmpdir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d))) {
                size_t len = strlen(ent->d_name);
                if (len > 3 && strcmp(ent->d_name + len - 3, ".py") == 0 &&
                    strcmp(ent->d_name, "__init__.py") != 0) {
                    snprintf(found, sizeof(found), "%s/%s", tmpdir, ent->d_name);
                    break;
                }
            }
            closedir(d);
        }
    }
    if (!found[0]) {
        fprintf(stderr, "kaya: pystage dönüştürmesinden çalıştırılabilir .py bulunamadı (%s)\n", tmpdir);
        return 1;
    }

    const char *base = strrchr(found, '/');
    base = base ? base + 1 : found;
    if (chdir(tmpdir) != 0) {
        perror("kaya: proje dizinine geçilemedi");
        return 1;
    }

    char *args[64];
    int ai = 0;
    args[ai++] = "python3";
    args[ai++] = (char *)base;
    for (int i = 0; i < argc && ai < 62; i++) args[ai++] = argv[i];
    args[ai] = NULL;
    execvp("python3", args);
    perror("kaya: çalıştırılamadı");
    return 1;
}

static int cmd_run(const char *name, int argc, char **argv) {
    if (!is_safe_component(name)) {
        fprintf(stderr, "kaya: geçersiz paket adı: %s\n", name);
        return 1;
    }
    manifest_t m;
    if (load_installed_manifest(name, &m) != 0) {
        fprintf(stderr, "kaya: '%s' kurulu değil\n", name);
        return 1;
    }
    /* the manifest on disk is data too: re-check what becomes a path */
    if (!is_safe_component(m.name)) {
        fprintf(stderr, "kaya: kurulu manifestte geçersiz 'name' alanı\n");
        manifest_free(&m);
        return 1;
    }
    if (!*m.language) snprintf(m.language, sizeof(m.language), "%s", m.runtime);
    kaya_build_selection selected = {0};
    int selected_rc = kaya_setup_get_build(root_dir, name, &selected);
    if (selected_rc < 0) {
        fprintf(stderr, "kaya: private setup profili geçersiz ya da okunamıyor\n");
        manifest_free(&m);
        return 1;
    }
    if (selected_rc == 1) {
        snprintf(selected.build_id, sizeof(selected.build_id), "%s",
                 m.build_count > 0 ? m.builds[0].id : "default");
        for (int i = 0; i < m.build_count; i++)
            if (strcmp(m.builds[i].id, "default") == 0)
                snprintf(selected.build_id, sizeof(selected.build_id), "%s", m.builds[i].id);
        snprintf(selected.language, sizeof(selected.language), "%s", m.language);
        snprintf(selected.language_version, sizeof(selected.language_version), "%s", m.language_version);
        snprintf(selected.runtime, sizeof(selected.runtime), "%s", m.runtime);
        snprintf(selected.runtime_version, sizeof(selected.runtime_version), "%s", m.runtime_version);
        if (kaya_setup_set_build(root_dir, name, &selected) != 0) {
            fprintf(stderr, "kaya: private runtime seçimi oluşturulamadı\n");
            manifest_free(&m);
            return 1;
        }
    }
    if (m.build_count > 0) {
        if (manifest_select_build(&m, selected.build_id) != 0) {
            fprintf(stderr, "kaya: private setup seçimi manifestte bulunamadı: %s\n", selected.build_id);
            manifest_free(&m);
            return 1;
        }
    } else if (strcmp(selected.build_id, "default") != 0) {
        fprintf(stderr, "kaya: manifest build seçimini içermiyor; paketi yeniden kur\n");
        manifest_free(&m);
        return 1;
    }
    if (!is_safe_rel_path(m.entry)) {
        fprintf(stderr, "kaya: kurulu manifestte geçersiz 'entry' alanı\n");
        manifest_free(&m);
        return 1;
    }
    if (strcmp(selected.runtime, m.runtime) != 0 ||
        strcmp(selected.runtime_version, m.runtime_version) != 0 ||
        strcmp(selected.language, m.language) != 0 ||
        strcmp(selected.language_version, m.language_version) != 0) {
        fprintf(stderr, "kaya: kurulu seçim manifest ile uyuşmuyor; paketi yeniden kur\n");
        manifest_free(&m);
        return 1;
    }
    char adir[600];
    app_dir(name, adir, sizeof(adir));
    char entry_path[900];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", adir, m.entry);

    char abs_entry[PATH_MAX], abs_adir[PATH_MAX];
    if (!realpath(entry_path, abs_entry) || !realpath(adir, abs_adir)) {
        fprintf(stderr, "kaya: giriş dosyası bulunamadı: %s\n", entry_path);
        manifest_free(&m);
        return 1;
    }

    if (strcmp(m.runtime, "html") == 0) {
        if (g_sandbox) {
            fprintf(stderr, "kaya: html paketleri --sandbox ile çalıştırılamaz (tarayıcı sandbox dışında açılır)\n");
            manifest_free(&m);
            return 1;
        }
        int rc = run_html(abs_entry);
        manifest_free(&m);
        return rc;
    }

    char *args[72];
    int ai = 0;
    char plugin_run[PATH_MAX + 8];
    char abs_pdir[PATH_MAX];
    const char *used_pdir = NULL;

    if (strcmp(m.runtime, "python") == 0) {
        args[ai++] = "python3";
        args[ai++] = abs_entry;
    } else if (strcmp(m.runtime, "sh") == 0 || strcmp(m.runtime, "shell") == 0) {
        args[ai++] = "sh";
        args[ai++] = abs_entry;
    } else if (strcmp(m.runtime, "node") == 0) {
        args[ai++] = "node";
        args[ai++] = abs_entry;
    } else if (strcmp(m.runtime, "native") == 0) {
        args[ai++] = abs_entry;
    } else {
        /* every other runtime is run by a plugin */
        const char *pn = runtime_plugin_name(m.runtime);
        char pdir[600];
        if (is_safe_component(pn)) {
            plugin_dir(pn, pdir, sizeof(pdir));
            if (realpath(pdir, abs_pdir)) {
                snprintf(plugin_run, sizeof(plugin_run), "%s/run", abs_pdir);
                if (access(plugin_run, X_OK) == 0) used_pdir = abs_pdir;
            }
        }
        if (!used_pdir) {
            if (strcmp(m.runtime, "sb3") == 0) {
                if (g_sandbox) {
                    fprintf(stderr, "kaya: pystage yolu --sandbox ile çalışmaz; sandbox için scratch plugini gerekli:\n"
                                    "      kaya --install -plugin scratch\n");
                    manifest_free(&m);
                    return 1;
                }
                int rc = run_sb3(m.name, abs_entry, argc, argv);
                manifest_free(&m);
                return rc;
            }
            fprintf(stderr, "kaya: '%s' runtime'ı için '%s' plugini gerekli (kurulu değil ya da 'run' dosyası yok)\n"
                            "      kurmak için: kaya --install -plugin %s\n", m.runtime, pn, pn);
            manifest_free(&m);
            return 1;
        }
        args[ai++] = plugin_run;
        args[ai++] = abs_entry;
    }
    for (int i = 0; i < argc && ai < 70; i++) args[ai++] = argv[i];
    args[ai] = NULL;

    if (g_sandbox) {
        sandbox_opts o;
        memset(&o, 0, sizeof(o));
        o.app_dir = abs_adir;
        o.plugin_dir = used_pdir;
        o.language = selected.language;
        o.language_version = selected.language_version;
        o.runtime = selected.runtime;
        o.runtime_version = selected.runtime_version;
        o.network = has_perm(&m, "network");
        o.display = has_perm(&m, "display");
        o.fs_read = has_perm(&m, "filesystem.read");
        char abs_data[PATH_MAX];
        if (has_perm(&m, "filesystem.write")) {
            char ddir[600];
            data_dir(m.name, ddir, sizeof(ddir));
            mkdirs(ddir);
            if (realpath(ddir, abs_data)) o.data_dir = abs_data;
        }
        for (int i = 0; i < m.perm_count; i++) {
            const char *p = m.permissions[i];
            if (strcmp(p, "network") && strcmp(p, "display") &&
                strcmp(p, "filesystem.read") && strcmp(p, "filesystem.write"))
                fprintf(stderr, "kaya: uyarı: bilinmeyen izin yok sayıldı: %s\n", p);
        }
        int rc = sandbox_run(&o, args);
        manifest_free(&m);
        return rc;
    }

    manifest_free(&m);
    execvp(args[0], args);
    perror("kaya: çalıştırılamadı");
    return 1;
}

/* --- upgrade / relook --------------------------------------------------------------
 * NOTE: --upgrade only checks and notifies for now - it does not replace
 * the running binary (release asset naming isn't finalized). */
static int cmd_upgrade(void) {
    char tmp[] = "/tmp/kaya-release.XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) { perror("kaya: mkstemp"); return 1; }
    close(fd);
    if (download_file(RELEASES_API, tmp) != 0) { unlink(tmp); return 1; }
    char *text = read_file(tmp);
    unlink(tmp);
    if (!text) { fprintf(stderr, "kaya: sürüm bilgisi okunamadı\n"); return 1; }
    const char *jerr;
    json_value *root = json_parse(text, &jerr);
    free(text);
    if (!root) { fprintf(stderr, "kaya: GitHub yanıtı ayrıştırılamadı: %s\n", jerr ? jerr : "?"); return 1; }
    const char *tag = json_get_string(root, "tag_name", NULL);
    if (!tag) { fprintf(stderr, "kaya: sürüm bilgisi bulunamadı\n"); json_free(root); return 1; }
    const char *tag_cmp = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;  /* "v1.0" == "1.0" */
    printf("Kurulu sürüm : %s\n", KAYA_VERSION);
    printf("Son sürüm    : %s\n", tag);
    if (strcmp(tag_cmp, KAYA_VERSION) == 0) {
        printf("Kaya güncel.\n");
    } else {
        printf("Yeni sürüm mevcut: %s\n", RELEASES_URL);
    }
    json_free(root);
    return 0;
}

/* Lists the sub-folders of a repo folder (each one is a package/plugin),
 * prints them and caches the names, one per line, in cache_path.
 * Returns the count, or -1 on error. */
static int relook_one(const char *label, const char *api_url, const char *cache_path) {
    json_value *root = github_fetch_listing(api_url);
    if (!root) {
        fprintf(stderr, "kaya: %s listesi alınamadı\n", label);
        return -1;
    }
    FILE *cf = fopen(cache_path, "w");
    printf("%s:\n", label);
    int n = 0;
    for (size_t i = 0; i < root->u.array.count; i++) {
        json_value *item = root->u.array.items[i];
        const char *type = json_get_string(item, "type", "");
        const char *nm = json_get_string(item, "name", NULL);
        if (strcmp(type, "dir") == 0 && nm) {
            printf("  %s\n", nm);
            if (cf) fprintf(cf, "%s\n", nm);
            n++;
        }
    }
    if (n == 0) printf("  (hiç yok)\n");
    if (cf) fclose(cf);
    json_free(root);
    return n;
}

static int cmd_relook(void) {
    char cdir[600];
    snprintf(cdir, sizeof(cdir), "%s/cache", root_dir);
    mkdirs(cdir);
    char pk[700], pl[700];
    snprintf(pk, sizeof(pk), "%s/packages.txt", cdir);
    snprintf(pl, sizeof(pl), "%s/plugins.txt", cdir);
    int a = relook_one("Paketler", PACKAGES_API_BASE, pk);
    int b = relook_one("Pluginler", PLUGINS_API_BASE, pl);
    if (a < 0 || b < 0) return 1;
    printf("Liste güncellendi: %d paket, %d plugin (önbellek: %s)\n", a, b, cdir);
    return 0;
}

static int cmd_setup(void) {
    if (kaya_setup_init(root_dir) != 0) {
        fprintf(stderr, "kaya: private setup profili oluşturulamadı\n");
        return 1;
    }
    printf("Kaya setup profili hazır. Runtime seçimi paket kurulurken kaydedilir.\n");
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "kullanım:\n"
        "  kaya --install <isim>                    depodan paket kurar\n"
        "  kaya --install -file <dosya.kay>         yerel .kay dosyasını kurar\n"
        "  kaya --install -needed <dosya.kay>       kurar, eksik bağımlılık/plugin'leri Y/N sorarak indirir\n"
        "  kaya --install -plugin <isim>            depodan plugin kurar\n"
        "  kaya --install -fileplugin <dosya.kyp>   yerel .kyp dosyasını kurar\n"
        "  kaya --remove <isim>\n"
        "  kaya --remove -plugin <isim>\n"
        "  kaya --upgrade                           yeni sürüm var mı bakar\n"
        "  kaya --relook                            depodaki paket/plugin listesini günceller\n"
        "  kaya list\n"
        "  kaya info <isim>\n"
        "  kaya run <isim> [-- argümanlar...]\n"
        "  kaya setup                               private setup.json profilini hazırlar\n"
        "\n"
        "runtime türleri (manifest.json içindeki \"runtime\" alanı):\n"
        "  native (vars.) doğrudan çalıştırılır\n"
        "  python         python3 <entry>\n"
        "  node           node <entry>\n"
        "  sh / shell     sh <entry>\n"
        "  html           <entry> bir tarayıcıda açılır (--sandbox ile çalışmaz)\n"
        "  sb3            'scratch' plugini ile; yoksa pystage ile\n"
        "  wasm           'wasm' plugini ile\n"
        "  <başka>        aynı adlı plugin ile (plugin içindeki 'run' dosyası çağrılır)\n"
        "\n"
        "manifest alanları: dependencies (başka paketler), plugins (gereken plugin'ler),\n"
        "                   permissions (--sandbox için, aşağıda)\n"
        "\n"
        "--sandbox izinleri (hiçbiri yoksa: ağ yok, dosya yok, ekran yok):\n"
        "  network           ağa erişim\n"
        "  display           X11/Wayland/WSLg penceresi\n"
        "  filesystem.read   çalışma dizinini salt-okunur görür\n"
        "  filesystem.write  <root>/data/<paket> klasörüne yazabilir (/data)\n"
        "\n"
        "seçenekler:\n"
        "  --root DIR      kurulum kök dizini (varsayılan: %s)\n"
        "  --browser NAME  'run' komutunda html paketleri için kullanılacak tarayıcı (ör. firefox)\n"
        "  --sandbox       güvenli çalışma seçeneği (Kaya paketleri zaten sandbox'ta çalışır)\n",
        DEFAULT_ROOT);
}

static int real_main(int argc, char **argv, int *argi, char **rest) {
    strncpy(root_dir, DEFAULT_ROOT, sizeof(root_dir) - 1);

    int argn = 0, after_dd = 0;
    for (int i = 1; i < argc; i++) {
        /* after a literal "--" everything belongs to the app being run */
        if (!after_dd && strcmp(argv[i], "--") == 0) {
            after_dd = 1;
            argi[argn++] = i;
        } else if (!after_dd && strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            strncpy(root_dir, argv[i + 1], sizeof(root_dir) - 1);
            i++;
        } else if (!after_dd && strcmp(argv[i], "--browser") == 0 && i + 1 < argc) {
            strncpy(browser_override, argv[i + 1], sizeof(browser_override) - 1);
            i++;
        } else if (!after_dd && strcmp(argv[i], "--sandbox") == 0) {
            g_sandbox = 1;
        } else {
            argi[argn++] = i;
        }
    }
    if (argn == 0) { usage(); return 1; }

    const char *cmd = argv[argi[0]];

    if (strcmp(cmd, "--install") == 0) {
        if (argn >= 3 && strcmp(argv[argi[1]], "-file") == 0) return cmd_install(argv[argi[2]]);
        if (argn >= 3 && strcmp(argv[argi[1]], "-needed") == 0) { g_needed = 1; return cmd_install(argv[argi[2]]); }
        if (argn >= 3 && strcmp(argv[argi[1]], "-fileplugin") == 0) return cmd_install_plugin_file(argv[argi[2]], NULL);
        if (argn >= 3 && strcmp(argv[argi[1]], "-plugin") == 0) return cmd_install_plugin_repo(argv[argi[2]]);
        if (argn >= 2) return cmd_install_repo(argv[argi[1]]);
        usage(); return 1;
    }
    if (strcmp(cmd, "--remove") == 0) {
        if (argn >= 3 && strcmp(argv[argi[1]], "-plugin") == 0) return cmd_remove_plugin(argv[argi[2]]);
        if (argn >= 2) return cmd_remove(argv[argi[1]]);
        usage(); return 1;
    }
    if (strcmp(cmd, "--upgrade") == 0) return cmd_upgrade();
    if (strcmp(cmd, "--relook") == 0) return cmd_relook();

    if (strcmp(cmd, "list") == 0) return cmd_list();
    if (strcmp(cmd, "setup") == 0) return cmd_setup();
    if (strcmp(cmd, "info") == 0 && argn >= 2) return cmd_info(argv[argi[1]]);
    if (strcmp(cmd, "run") == 0 && argn >= 2) {
        int rn = 0;
        int first = 2;
        if (argn > 2 && strcmp(argv[argi[2]], "--") == 0) first = 3;  /* drop the separator */
        for (int i = first; i < argn; i++) rest[rn++] = argv[argi[i]];
        return cmd_run(argv[argi[1]], rn, rest);
    }
    usage();
    return 1;
}

int main(int argc, char **argv) {
    int *argi = malloc((size_t)argc * sizeof(int));
    char **rest = malloc((size_t)argc * sizeof(char *));
    if (!argi || !rest) { perror("kaya: malloc"); return 1; }
    int rc = real_main(argc, argv, argi, rest);
    free(argi);
    free(rest);
    return rc;
}
