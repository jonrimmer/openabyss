/* SPDX-License-Identifier: MIT */
/* See uw_gamedir.h. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L        /* opendir, S_ISDIR under -std=c11 */
#endif
#include "uw_gamedir.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

/* ---- the file system ---------------------------------------------------- */

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int uw_is_dir(const char *path) {
    char real[1024];
    struct stat st;
    if (!uw_path_resolve(path, real, sizeof real)) return 0;
    return stat(real, &st) == 0 && S_ISDIR(st.st_mode);
}

int uw_mkdir(const char *path) {
    char real[1024];
    if (uw_path_resolve(path, real, sizeof real)) return uw_is_dir(real) ? 0 : -1;
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static int same_ignoring_case(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

#ifdef _WIN32

int uw_path_resolve(const char *path, char *out, size_t cap) {
    if (strlen(path) + 1 > cap) return 0;
    if (out != path) strcpy(out, path);    /* callers resolve a path in place */
    return exists(path);
}

#else

/* The entry of `dir` named `name` ignoring case, into `out`: 1, or 0. */
static int match_in(const char *dir, const char *name, char *out, size_t cap) {
    DIR *d = opendir(*dir ? dir : ".");
    struct dirent *e;
    int found = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL)
        if (same_ignoring_case(e->d_name, name) && strlen(e->d_name) < cap) {
            strcpy(out, e->d_name);
            found = 1;
            break;
        }
    closedir(d);
    return found;
}

int uw_path_resolve(const char *path, char *out, size_t cap) {
    char built[1024], part[256], real[256];
    const char *p = path;
    size_t n;
    if (exists(path)) {
        if (strlen(path) + 1 > cap) return 0;
        if (out != path) strcpy(out, path);
        return 1;
    }
    built[0] = 0;
    if (*p == '/') { strcpy(built, "/"); while (*p == '/') p++; }
    while (*p) {
        const char *slash = strchr(p, '/');
        n = slash ? (size_t)(slash - p) : strlen(p);
        if (n >= sizeof part) return 0;
        memcpy(part, p, n);
        part[n] = 0;
        p += n;
        while (*p == '/') p++;
        if (!strcmp(part, ".") || !strcmp(part, "..")) strcpy(real, part);
        else {
            char probe[1300];
            snprintf(probe, sizeof probe, "%s%s%s", built, *built && built[strlen(built) - 1] != '/' ? "/" : "", part);
            if (exists(probe)) strcpy(real, part);
            else if (!match_in(built, part, real, sizeof real)) return 0;
        }
        if (strlen(built) + strlen(real) + 2 > sizeof built) return 0;
        if (*built && built[strlen(built) - 1] != '/') strcat(built, "/");
        strcat(built, real);
    }
    if (strlen(built) + 1 > cap) return 0;
    strcpy(out, built);
    return 1;
}

#endif

FILE *uw_fopen(const char *path, const char *mode) {
    char real[1024], dir[1024];
    const char *slash;
    if (uw_path_resolve(path, real, sizeof real)) return fopen(real, mode);
    if (mode[0] == 'r') return NULL;
    /* a new file: its directory as it is on disk, its own name as given */
    slash = strrchr(path, '/');
    if (!slash) return fopen(path, mode);
    if ((size_t)(slash - path) >= sizeof dir) return NULL;
    memcpy(dir, path, (size_t)(slash - path));
    dir[slash - path] = 0;
    if (!uw_path_resolve(dir, real, sizeof real) || strlen(real) + strlen(slash) + 1 > sizeof real) return fopen(path, mode);
    strcat(real, slash);
    return fopen(real, mode);
}

/* ---- the game's directory ----------------------------------------------- */

int uw_game_check(const char *dir, char *why, size_t cap) {
    static const char *const need[3] = { "DATA", "CRIT", "CUTS" };
    char path[1100];
    int k;
    snprintf(path, sizeof path, "%s/UW.EXE", dir);
    if (!uw_path_resolve(path, path, sizeof path) || uw_is_dir(path)) {
        if (why) snprintf(why, cap, "no UW.EXE");
        return 0;
    }
    for (k = 0; k < 3; k++) {
        snprintf(path, sizeof path, "%s/%s", dir, need[k]);
        if (!uw_is_dir(path)) {
            if (why) snprintf(why, cap, "no %s/", need[k]);
            return 0;
        }
    }
    return 1;
}

int uw_game_find_in(const char *dir, char *out, size_t cap) {
    char sub[1100];
    if (uw_game_check(dir, NULL, 0)) {
        if (strlen(dir) + 1 > cap) return 0;
        strcpy(out, dir);
        return 1;
    }
    snprintf(sub, sizeof sub, "%s/UW", dir);
    if (uw_game_check(sub, NULL, 0) && uw_path_resolve(sub, out, cap)) return 1;
    return 0;
}

/* The directories under `base` whose names hold "underworld": the first
 * that is (or holds, in UW) the game's directory, or holds its CD image
 * (*image set). */
static int search_under(const char *base, char *out, size_t cap, int *image);

static int take_child(const char *child, char *out, size_t cap, int *image) {
    if (uw_game_find_in(child, out, cap)) { *image = 0; return 1; }
    if (uw_game_image_in(child, out, cap)) { *image = 1; return 1; }
    return 0;
}

#ifdef _WIN32

static int search_under(const char *base, char *out, size_t cap, int *image) {
    char pattern[1100], child[1100];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s/*", base);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char lower[MAX_PATH];
        size_t i;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        for (i = 0; fd.cFileName[i] && i + 1 < sizeof lower; i++) lower[i] = (char)tolower((unsigned char)fd.cFileName[i]);
        lower[i] = 0;
        if (!strstr(lower, "underworld")) continue;
        snprintf(child, sizeof child, "%s/%s", base, fd.cFileName);
        if (take_child(child, out, cap, image)) { FindClose(h); return 1; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

#else

static int search_under(const char *base, char *out, size_t cap, int *image) {
    DIR *d = opendir(base);
    struct dirent *e;
    char child[1100];
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        char lower[256];
        size_t i;
        if (e->d_name[0] == '.') continue;
        for (i = 0; e->d_name[i] && i + 1 < sizeof lower; i++) lower[i] = (char)tolower((unsigned char)e->d_name[i]);
        lower[i] = 0;
        if (!strstr(lower, "underworld")) continue;
        snprintf(child, sizeof child, "%s/%s", base, e->d_name);
        if (uw_is_dir(child) && take_child(child, out, cap, image)) { closedir(d); return 1; }
    }
    closedir(d);
    return 0;
}

#endif

int uw_game_detect(const char *home, char *out, size_t cap, int *image) {
#ifdef _WIN32
    static const char *const fixed[] = {
        "C:/GOG Games", "C:/Program Files (x86)/GOG Galaxy/Games", "C:/Program Files/GOG Galaxy/Games",
        "C:/GOG Galaxy/Games",
    };
    static const char *const in_home[] = { "GOG Games", "Games" };
#else
    static const char *const fixed[] = { "/opt" };
    static const char *const in_home[] = {
        "GOG Games", "Games", "Games/Heroic", "Games/GOG", ".wine/drive_c/GOG Games",
        ".wine/drive_c/Program Files (x86)/GOG Galaxy/Games",
    };
#endif
    char base[1100];
    size_t i;
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
        if (search_under(fixed[i], out, cap, image)) return 1;
    if (home && *home)
        for (i = 0; i < sizeof in_home / sizeof in_home[0]; i++) {
            snprintf(base, sizeof base, "%s/%s", home, in_home[i]);
            if (search_under(base, out, cap, image)) return 1;
        }
    return 0;
}

uint32_t uw_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* The releases whose UW.EXE is known. */
static const struct { uint32_t crc; long size; const char *name; int supported; } releases[] = {
    { 0xf2bf2527u, 547248, "GOG", 1 },
};

int uw_game_release_of(const char *dir, uw_game_release *r) {
    char path[1100];
    uw_blob exe;
    size_t i;
    memset(r, 0, sizeof *r);
    snprintf(path, sizeof path, "%s/UW.EXE", dir);
    if (!uw_path_resolve(path, path, sizeof path)) return 0;
    exe = uw_read_file(path);
    if (!exe.data) return 0;
    r->crc = uw_crc32(exe.data, exe.size);
    r->size = (long)exe.size;
    uw_free(&exe);
    for (i = 0; i < sizeof releases / sizeof releases[0]; i++)
        if (releases[i].crc == r->crc && releases[i].size == r->size) {
            r->name = releases[i].name;
            r->supported = releases[i].supported;
        }
    return 1;
}

/* ---- the configuration -------------------------------------------------- */

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    while (isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
}

int uw_config_get(const char *path, const char *key, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    char line[1200];
    int found = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *hash = strchr(line, '#');
        if (hash && (!eq || hash < eq)) continue;
        if (!eq) continue;
        *eq = 0;
        trim(line);
        if (strcmp(line, key)) continue;
        trim(eq + 1);
        if (strlen(eq + 1) + 1 <= cap) { strcpy(out, eq + 1); found = 1; }
    }
    fclose(f);
    return found;
}

int uw_config_set(const char *path, const char *key, const char *value) {
    FILE *f = fopen(path, "r");
    char *text = NULL, line[1200], name[1200];
    size_t len = 0, cap = 0;
    int done = 0;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *eq = strchr(line, '=');
            const char *keep = line;
            char repl[1300];
            size_t n;
            if (eq && line[0] != '#') {
                memcpy(name, line, (size_t)(eq - line));
                name[eq - line] = 0;
                trim(name);
                if (!strcmp(name, key)) {
                    if (done) continue;
                    snprintf(repl, sizeof repl, "%s = %s\n", key, value);
                    keep = repl;
                    done = 1;
                }
            }
            n = strlen(keep);
            if (len + n + 1 > cap) {
                char *t = realloc(text, cap = (len + n + 1) * 2);
                if (!t) { free(text); fclose(f); return 0; }
                text = t;
            }
            memcpy(text + len, keep, n + 1);
            len += n;
        }
        fclose(f);
    }
    f = fopen(path, "w");
    if (!f) { free(text); return 0; }
    if (len) fwrite(text, 1, len, f);
    if (!done) fprintf(f, "%s = %s\n", key, value);
    free(text);
    return fclose(f) == 0;
}

/* ---- the CD image ------------------------------------------------------- */

/* ISO 9660: 2048-byte logical blocks (the volume says), the primary volume
 * descriptor in block 16 on, its root directory record at +156; a
 * directory record is a length, the extent's block (+2) and length (+10),
 * the flags (+25, bit 1 a directory) and the name (+32, +33), which may end
 * in ";1" and, without an extension, in a dot. A record never crosses a
 * block; a zero length ends the block's records. */
typedef struct {
    FILE    *f;
    uint32_t bs;
    uint32_t root_lba, root_len;
} iso_image;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int iso_open(iso_image *im, const char *path) {
    uint8_t pvd[2048];
    int s;
    memset(im, 0, sizeof *im);
    if (!(im->f = uw_fopen(path, "rb"))) return 0;
    for (s = 16; s < 32; s++) {
        if (fseek(im->f, (long)s * 2048, SEEK_SET) || fread(pvd, 1, sizeof pvd, im->f) != sizeof pvd) break;
        if (memcmp(pvd + 1, "CD001", 5)) break;
        if (pvd[0] == 255) break;
        if (pvd[0] != 1) continue;
        im->bs = (uint32_t)(pvd[128] | pvd[129] << 8);
        im->root_lba = le32(pvd + 156 + 2);
        im->root_len = le32(pvd + 156 + 10);
        if (im->bs >= 512 && im->bs <= 2048 && im->root_len) return 1;
        break;
    }
    fclose(im->f);
    im->f = NULL;
    return 0;
}

static void iso_close(iso_image *im) {
    if (im->f) fclose(im->f);
    im->f = NULL;
}

/* A directory's records, read whole: NULL when it will not read. */
static uint8_t *iso_dir(iso_image *im, uint32_t lba, uint32_t len) {
    uint8_t *d;
    if (len == 0 || len > (1u << 24)) return NULL;
    if (!(d = malloc(len))) return NULL;
    if (fseek(im->f, (long)lba * (long)im->bs, SEEK_SET) || fread(d, 1, len, im->f) != len) { free(d); return NULL; }
    return d;
}

/* The record at d[*at] (the zero lengths skipped to the next block): 1 with
 * its name, extent and flags, 0 at the directory's end. "." and ".." are
 * the names "\0" and "\1". */
static int iso_next(const iso_image *im, const uint8_t *d, uint32_t len, uint32_t *at,
                    char *name, size_t cap, uint32_t *lba, uint32_t *size, int *dir) {
    while (*at < len) {
        const uint8_t *r = d + *at;
        uint32_t rl = r[0], nl, k;
        if (rl == 0) { *at = (*at / im->bs + 1) * im->bs; continue; }
        if (rl < 34 || *at + rl > len) return 0;
        nl = r[32];
        if (33 + nl > rl) return 0;
        *at += rl;
        for (k = 0; k < nl && k + 1 < cap && r[33 + k] != ';'; k++) name[k] = (char)r[33 + k];
        name[k] = 0;
        if (k > 1 && name[k - 1] == '.') name[k - 1] = 0;
        *lba = le32(r + 2);
        *size = le32(r + 10);
        *dir = (r[25] & 2) != 0;
        return 1;
    }
    return 0;
}

/* The entry `want` (ignoring case) of the directory at lba/len. */
static int iso_find(iso_image *im, uint32_t lba, uint32_t len, const char *want, int dir,
                    uint32_t *out_lba, uint32_t *out_len) {
    uint8_t *d = iso_dir(im, lba, len);
    uint32_t at = 0, l, z;
    char name[64];
    int isdir, found = 0;
    if (!d) return 0;
    while (iso_next(im, d, len, &at, name, sizeof name, &l, &z, &isdir))
        if (isdir == dir && same_ignoring_case(name, want)) { *out_lba = l; *out_len = z; found = 1; break; }
    free(d);
    return found;
}

static int iso_copy_file(iso_image *im, uint32_t lba, uint32_t len, const char *dst) {
    uint8_t buf[65536];
    FILE *o;
    int ok = 1;
    if (fseek(im->f, (long)lba * (long)im->bs, SEEK_SET)) return 0;
    if (!(o = fopen(dst, "wb"))) return 0;
    while (len && ok) {
        size_t n = len < sizeof buf ? len : sizeof buf;
        ok = fread(buf, 1, n, im->f) == n && fwrite(buf, 1, n, o) == n;
        len -= (uint32_t)n;
    }
    if (fclose(o)) ok = 0;
    return ok;
}

static int iso_copy_dir(iso_image *im, uint32_t lba, uint32_t len, const char *dst, int depth,
                        char *why, size_t cap) {
    uint8_t *d;
    uint32_t at = 0, l, z;
    char name[64], path[1200];
    int isdir, ok = 1;
    if (depth > 8) { snprintf(why, cap, "the image's folders nest too deep"); return 0; }
    if (uw_mkdir(dst)) { snprintf(why, cap, "%s cannot be made", dst); return 0; }
    if (!(d = iso_dir(im, lba, len))) { snprintf(why, cap, "the image's folder will not read"); return 0; }
    while (ok && iso_next(im, d, len, &at, name, sizeof name, &l, &z, &isdir)) {
        if ((unsigned char)name[0] <= 1 || !strcmp(name, ".") || !strcmp(name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dst, name);
        if (isdir) ok = iso_copy_dir(im, l, z, path, depth + 1, why, cap);
        else if (!iso_copy_file(im, l, z, path)) {
            snprintf(why, cap, "%s will not write", path);
            ok = 0;
        }
    }
    free(d);
    return ok;
}

int uw_iso_holds_game(const char *path) {
    iso_image im;
    uint32_t lba, len, l2, z2;
    int ok;
    if (!iso_open(&im, path)) return 0;
    ok = iso_find(&im, im.root_lba, im.root_len, "UW", 1, &lba, &len)
         && iso_find(&im, lba, len, "UW.EXE", 0, &l2, &z2)
         && iso_find(&im, lba, len, "DATA", 1, &l2, &z2)
         && iso_find(&im, lba, len, "CRIT", 1, &l2, &z2)
         && iso_find(&im, lba, len, "CUTS", 1, &l2, &z2);
    iso_close(&im);
    return ok;
}

int uw_iso_extract_game(const char *path, const char *dst, char *why, size_t cap) {
    iso_image im;
    uint32_t lba, len;
    int ok;
    if (!iso_open(&im, path)) { snprintf(why, cap, "%s is not a CD image", path); return 0; }
    if (!iso_find(&im, im.root_lba, im.root_len, "UW", 1, &lba, &len)) {
        iso_close(&im);
        snprintf(why, cap, "%s has no UW folder", path);
        return 0;
    }
    ok = iso_copy_dir(&im, lba, len, dst, 0, why, cap);
    iso_close(&im);
    return ok;
}

int uw_game_image_in(const char *dir, char *out, size_t cap) {
    char path[1200];
    snprintf(path, sizeof path, "%s/game.gog", dir);
    if (!uw_path_resolve(path, out, cap)) return 0;
    return uw_iso_holds_game(out);
}
