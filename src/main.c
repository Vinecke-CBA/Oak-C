#include "oak.h"

#ifdef _WIN32
#include <windows.h>
#define DEFAULT_OUT "a.exe"
#else
#include <unistd.h>
#define DEFAULT_OUT "a.out"
#endif

/* Directory containing the oakc executable itself. argv[0] alone is not
   enough: a shell finds `oakc` on PATH and passes it with no directory, so
   path_dir(argv[0]) would be "." - the caller's folder - and the documented
   "include/ next to the oakc executable" search would silently resolve into
   the user's project instead of the installation. Returns NULL when the
   platform cannot tell us, so callers can fall back to argv[0]. */
static char *exe_dir(void) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof buf);
    if (n > 0 && n < (DWORD)sizeof buf) {
        return path_dir(buf);
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return path_dir(buf);
    }
#endif
    return NULL;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        fprintf(stderr, "cannot read '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (out_len) {
        *out_len = got;
    }
    return buf;
}

static void usage(void) {
    fprintf(stderr,
            "usage: oakc <file.oak> [-o binary] [--cc name|path] [--config file]\n"
            "            [--emit-c file.c] [--keep-c] [--verbose] [-- <cc flags...>]\n"
            "\n"
            "  -o binary        output executable (default a.exe on Windows, a.out elsewhere)\n"
            "  --cc name|path   C compiler: gcc (default), tcc, clang, cc, or a path.\n"
            "                   `tcc` uses dependencies/tcc/tcc.exe when it is present.\n"
            "  --config file    read settings from `file` instead of oak.cfg\n"
            "  --emit-c file.c  write the generated C here and keep it\n"
            "  --keep-c         keep the temporary generated C file\n"
            "  --verbose        print the resolved settings and the compiler command\n"
            "  -- <flags...>    pass everything after -- straight to the C compiler\n"
            "\n"
            "Settings can also live in oak.cfg (next to the input file, or in the\n"
            "working directory). Keys: cc, std, opt, out, keep_c, flags.\n");
}

static bool already_loaded(Comp *c, const char *canon) {
    for (int i = 0; i < c->nloaded; i++) {
        if (strcmp(c->loaded[i], canon) == 0) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------- config --
   Optional per-project settings, read from an `oak.cfg` text file of
   `key = value` lines (`#` or `;` starts a comment, blank lines ignored).

       cc     = tcc                compiler name or path       (default gcc)
       std    = c99                passed as -std=c99
       opt    = 2                  -O level; skip the flag with `opt = -`
       out    = build/app          default output when -o is absent
       keep_c = true               keep the generated C file
       flags  = -lraylib -Imylib   extra flags appended verbatim

   Lookup order: --config <file>, then oak.cfg next to the input file, then
   oak.cfg in the working directory. Command-line flags always win. */
typedef struct {
    const char *cc;
    const char *std;
    int opt;
    const char *out;
    bool keep_c;
    const char **flags;
    int nflags, capflags;
} Cfg;

static void cfg_init(Cfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->cc = "gcc";
    cfg->std = "c99";
    cfg->opt = 2;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        *--e = 0;
    }
    return s;
}

/* `flags = -lm -Iexamples` -> two separate gcc arguments. */
static void cfg_add_flags(Cfg *cfg, char *val) {
    char *p = val;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        char save = *p;
        *p = 0;
        ptrlist_push((void ***)&cfg->flags, &cfg->nflags, &cfg->capflags, xstrdup(start));
        if (save) {
            p++;
        }
    }
}

static void cfg_set(Cfg *cfg, const char *key, char *val, const char *where) {
    if (strcmp(key, "cc") == 0) {
        cfg->cc = xstrdup(val);
    } else if (strcmp(key, "std") == 0) {
        cfg->std = (strcmp(val, "-") == 0 || strcmp(val, "none") == 0) ? NULL : xstrdup(val);
    } else if (strcmp(key, "opt") == 0) {
        cfg->opt = (strcmp(val, "-") == 0 || strcmp(val, "none") == 0) ? -1 : atoi(val);
    } else if (strcmp(key, "out") == 0) {
        cfg->out = xstrdup(val);
    } else if (strcmp(key, "keep_c") == 0) {
        cfg->keep_c = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0 ||
                       strcmp(val, "yes") == 0 || strcmp(val, "on") == 0);
    } else if (strcmp(key, "flags") == 0) {
        cfg_add_flags(cfg, val);
    } else {
        fprintf(stderr, "oak: warning: %s: unknown key '%s'\n", where, key);
    }
}

static void cfg_load(Cfg *cfg, const char *path, bool required) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (required) {
            fprintf(stderr, "cannot open config '%s'\n", path);
            exit(1);
        }
        return;
    }
    static char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') {
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "oak: warning: %s: ignoring line without '=': %s\n", path, s);
            continue;
        }
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);
        cfg_set(cfg, key, val, path);
    }
    fclose(f);
    if (getenv("OAK_DEBUG_CONFIG")) {
        fprintf(stderr, "oak: read config %s\n", path);
    }
}

/* Which compiler to run. A name containing a path separator is used as-is;
 * `tcc` prefers the bundled copy next to the oakc executable (or the working
 * directory) because TCC needs its own include/ and lib/ beside it. */
static const char *base_name(const char *p) {
    const char *b = p + strlen(p);
    while (b > p && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    return b;
}

static bool is_tcc(const char *cc) {
    return strstr(base_name(cc), "tcc") != NULL;
}

static char *resolve_cc(const char *cc, const char *argv0) {
    if (strchr(cc, '/') || strchr(cc, '\\')) {
        return xstrdup(cc); /* explicit path */
    }
    if (is_tcc(cc)) {
        if (argv0) {
            char *d = path_dir(argv0);
            char *p = path_join(d, "dependencies/tcc/tcc.exe");
            bool ok = file_exists(p);
            free(d);
            if (ok) {
                return p;
            }
            free(p);
        }
        if (file_exists("dependencies/tcc/tcc.exe")) {
            return xstrdup("dependencies/tcc/tcc.exe");
        }
    }
    return xstrdup(cc); /* let the OS search PATH */
}

static void to_backslash(char *p) {
    for (; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
}

/* Append to the compiler command line, never overrunning the buffer. */
static void cmd_append(char *cmd, size_t cap, int *w, const char *fmt, ...) {
    if (*w < 0) {
        *w = 0;
    }
    if ((size_t)*w >= cap - 1) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(cmd + *w, cap - (size_t)*w, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    *w += n;
    if ((size_t)*w > cap - 1) {
        *w = (int)(cap - 1); /* truncated: keep the pointer in range */
    }
}

/* Record an include search directory: only if it exists (so a project
   without an include/ folder behaves exactly as before) and only once
   (the same folder can be reachable three ways: -I, next to the input
   file, and next to the working directory). */
static void add_incdir(Comp *c, const char *dir) {
    if (!dir || !*dir || !dir_exists(dir)) {
        return;
    }
    char *canon = path_canonical(dir);
    for (int i = 0; i < c->nincdirs; i++) {
        char *have = path_canonical(c->incdirs[i]);
        bool same = strcmp(have, canon) == 0;
        free(have);
        if (same) {
            free(canon);
            return;
        }
    }
    free(canon);
    ptrlist_push((void ***)&c->incdirs, &c->nincdirs, &c->capincdirs, (void *)xstrdup(dir));
}

static void parse_file(Comp *c, const char *path, const char *referrer_dir) {
    char *cand = NULL;
    if (referrer_dir) {
        char *joined = path_join(referrer_dir, path);
        if (file_exists(joined)) {
            cand = joined;
        } else {
            free(joined);
        }
    }
    if (!cand) {
        if (file_exists(path)) {
            cand = xstrdup(path);
        } else if (referrer_dir && (cand = comp_find_include(c, path)) != NULL) {
            /* found in an include search directory (-I / include/) */
        } else {
            fprintf(stderr, "cannot open '%s'%s\n", path, referrer_dir ? " (imported)" : "");
            exit(1);
        }
    }
    char *canon = path_canonical(cand);
    free(cand);
    if (already_loaded(c, canon)) {
        free(canon);
        return;
    }
    ptrlist_push((void ***)&c->loaded, &c->nloaded, &c->caploaded, (void *)canon);
    size_t flen = 0;
    char *src = read_file(canon, &flen);
    const char *save_src = c->src;
    size_t save_len = c->len;
    const char *save_name = c->filename;
    c->src = src;
    c->len = flen;
    c->filename = canon;
    int start = c->nimports;
    Lexer lx;
    Comp tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.arena = c->arena; /* struct copy shares bump pointer; synced back */
    tmp.interns = c->interns;
    tmp.ninterns = c->ninterns;
    tmp.capinterns = c->capinterns;
    tmp.prog = c->prog; /* shares fn/struct arrays; counts synced back */
    tmp.imports = c->imports;
    tmp.nimports = c->nimports;
    tmp.capimports = c->capimports;
    tmp.loaded = c->loaded;
    tmp.nloaded = c->nloaded;
    tmp.caploaded = c->caploaded;
    tmp.incdirs = c->incdirs;
    tmp.nincdirs = c->nincdirs;
    tmp.capincdirs = c->capincdirs;
    tmp.src = src;
    tmp.len = flen;
    tmp.filename = canon;
    tmp.errors = c->errors;
    lex_init(&lx, &tmp);
    parse_program(&lx);
    /* sync back everything the parse may have grown */
    c->arena = tmp.arena;
    c->interns = tmp.interns;
    c->ninterns = tmp.ninterns;
    c->capinterns = tmp.capinterns;
    c->prog = tmp.prog;
    c->imports = tmp.imports;
    c->nimports = tmp.nimports;
    c->capimports = tmp.capimports;
    c->loaded = tmp.loaded;
    c->nloaded = tmp.nloaded;
    c->caploaded = tmp.caploaded;
    c->errors = tmp.errors;
    int m = c->nimports - start;
    const char **mine = NULL;
    if (m > 0) {
        mine = xmalloc((size_t)m * sizeof(char *));
        for (int i = 0; i < m; i++) {
            mine[i] = c->imports[start + i];
        }
    }
    char *dir = path_dir(canon);
    c->src = save_src;
    c->len = save_len;
    c->filename = save_name;
    free(src);
    if (c->errors) {
        free(dir);
        free(mine);
        return;
    }
    for (int i = 0; i < m; i++) {
        parse_file(c, mine[i], dir);
        if (c->errors) {
            break;
        }
    }
    free(dir);
    free(mine);
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *out_bin = NULL; /* -o, else config `out`, else DEFAULT_OUT */
    const char *emit_c = NULL;
    const char *cl_cc = NULL;      /* --cc */
    const char *config_path = NULL; /* --config */
    bool keep_c = false;
    bool verbose = false;
    const char **ccflags = NULL;
    int ncc = 0, capcc = 0;
    bool passthru = false;
    for (int i = 1; i < argc; i++) {
        if (passthru) {
            ptrlist_push((void ***)&ccflags, &ncc, &capcc, argv[i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_bin = argv[++i];
        } else if (strcmp(argv[i], "--emit-c") == 0 && i + 1 < argc) {
            emit_c = argv[++i];
        } else if (strcmp(argv[i], "--keep-c") == 0) {
            keep_c = true;
        } else if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) {
            cl_cc = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--") == 0) {
            passthru = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-' && input) {
            ptrlist_push((void ***)&ccflags, &ncc, &capcc, argv[i]);
        } else if (argv[i][0] == '-' && !input) {
            const char *a = argv[i];
            if (a[1] == 'l' || a[1] == 'L' || a[1] == 'I' || a[1] == 'D' ||
                a[1] == 'O' || a[1] == 'W' || a[1] == 'f' || a[1] == 'm' ||
                strncmp(a, "-std", 4) == 0) {
                ptrlist_push((void ***)&ccflags, &ncc, &capcc, argv[i]);
            } else {
                fprintf(stderr, "unknown option %s\n", argv[i]);
                usage();
                return 1;
            }
        } else if (!input) {
            input = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (!input) {
        usage();
        return 1;
    }

    /* ---- settings: oak.cfg, overridden by command-line flags ---- */
    Cfg cfg;
    cfg_init(&cfg);
    const char *cfg_used = NULL;
    char *cfg_near = NULL;
    if (config_path) {
        cfg_load(&cfg, config_path, true);
        cfg_used = config_path;
    } else {
        char *dir = path_dir(input);
        cfg_near = path_join(dir, "oak.cfg");
        if (file_exists(cfg_near)) {
            cfg_load(&cfg, cfg_near, false);
            cfg_used = cfg_near;
        } else if (file_exists("oak.cfg")) {
            cfg_load(&cfg, "oak.cfg", false);
            cfg_used = "oak.cfg";
        }
        free(dir);
    }
    const char *cc_want = cl_cc ? cl_cc : cfg.cc;
    char *cc_path = resolve_cc(cc_want, argv[0]);
#ifdef _WIN32
    to_backslash(cc_path);
#endif
    if (!out_bin) {
        out_bin = cfg.out ? cfg.out : DEFAULT_OUT;
    }
#ifdef _WIN32
    /* gcc appends .exe on Windows, TCC does not — and a binary without the
       extension cannot be run from cmd.exe. Match gcc so `-o app` works the
       same with every compiler. */
    char out_buf[1024];
    if (out_bin && is_tcc(cc_path) && !strchr(base_name(out_bin), '.')) {
        snprintf(out_buf, sizeof(out_buf), "%s.exe", out_bin);
        out_bin = out_buf;
    }
#endif
    if (cfg.keep_c) {
        keep_c = true;
    }

    Comp c;
    memset(&c, 0, sizeof(c));
    c.filename = input;
    arena_init(&c.arena);

    /* Include search path, consulted after "next to the including file" and
       "next to the working directory" have failed:
         1. every -I<dir> already being forwarded to the C compiler
         2. include/ next to the input file   (a per-project folder)
         3. include/ in the working directory
         4. include/ next to the oakc executable
       Directories that do not exist are skipped, so projects that have no
       include/ folder see no difference at all. */
    for (int i = 0; i < ncc; i++) {
        if (ccflags[i][0] == '-' && ccflags[i][1] == 'I' && ccflags[i][2]) {
            add_incdir(&c, ccflags[i] + 2);
        }
    }
    for (int i = 0; i < cfg.nflags; i++) {
        if (cfg.flags[i][0] == '-' && cfg.flags[i][1] == 'I' && cfg.flags[i][2]) {
            add_incdir(&c, cfg.flags[i] + 2);
        }
    }
    /* Entries added so far came from -I flags, which are already forwarded
       verbatim below - only the default include/ folders need -I added. */
    int incdirs_from_flags = c.nincdirs;
    {
        char *idir = path_dir(input);
        char *p = path_join(idir, "include");
        add_incdir(&c, p);
        free(p);
        free(idir);
    }
    add_incdir(&c, "include");
    {
        char *exedir = exe_dir();
        if (!exedir && argv[0]) {
            exedir = path_dir(argv[0]); /* invoked with a path: already the exe dir */
        }
        if (exedir) {
            char *p = path_join(exedir, "include");
            add_incdir(&c, p);
            free(p);
            free(exedir);
        }
    }

    parse_file(&c, input, NULL);
    if (c.errors) {
        return 1;
    }
    typecheck(&c);
    if (c.errors) {
        return 1;
    }
    aliascheck(&c);
    if (c.errors) {
        return 1;
    }

    char tmpc[1024];
    const char *cpath = emit_c;
    if (!cpath) {
        snprintf(tmpc, sizeof(tmpc), "%s.c", input);
        cpath = tmpc;
    }
    FILE *out = fopen(cpath, "w");
    if (!out) {
        fprintf(stderr, "cannot write '%s'\n", cpath);
        return 1;
    }
    codegen(&c, out);
    fclose(out);

    /* cmd.exe strips the first and last quote of a command line that starts
       with one, so the compiler path is left unquoted unless it contains
       whitespace — in which case the whole line gets an extra outer pair. */
    const char *cc_tok = cc_path;
    bool cc_quoted = false;
    char cc_buf[1024];
    if (strpbrk(cc_path, " \t")) {
        snprintf(cc_buf, sizeof(cc_buf), "\"%s\"", cc_path);
        cc_tok = cc_buf;
        cc_quoted = true;
    }

    char cmd[8192];
    int w = 0;
    cmd_append(cmd, sizeof(cmd), &w, "%s", cc_tok);
    if (cfg.std) {
        cmd_append(cmd, sizeof(cmd), &w, " -std=%s", cfg.std);
    }
    if (cfg.opt >= 0) {
        cmd_append(cmd, sizeof(cmd), &w, " -O%d", cfg.opt);
    }
    cmd_append(cmd, sizeof(cmd), &w, " -o \"%s\" \"%s\"", out_bin, cpath);
    /* `.c` files pulled in by `include "x.c" as extern C` - compiled and
       linked automatically, so the user never passes them on the command
       line. Objects come before the libraries in flags/config below. */
    for (int i = 0; i < c.prog.ncsrcs; i++) {
        cmd_append(cmd, sizeof(cmd), &w, " \"%s\"", c.prog.csrcs[i]);
    }
    /* The same include search path, so `#include <x.h>` headers living in
       include/ are found by the C compiler too (the generated file itself
       sits next to the input, never inside include/). */
    for (int i = incdirs_from_flags; i < c.nincdirs; i++) {
        cmd_append(cmd, sizeof(cmd), &w, " -I\"%s\"", c.incdirs[i]);
    }
    for (int i = 0; i < cfg.nflags; i++) {
        cmd_append(cmd, sizeof(cmd), &w, " %s", cfg.flags[i]);
    }
    for (int i = 0; i < ncc; i++) {
        cmd_append(cmd, sizeof(cmd), &w, " %s", ccflags[i]);
    }
    if (verbose) {
        if (cfg_used) {
            fprintf(stderr, "oak: config %s\n", cfg_used);
        }
        fprintf(stderr, "oak: cc     %s\n", cc_path);
        fprintf(stderr, "oak: %s\n", cmd);
    }
    int rc;
    if (cc_quoted) {
        char wrapped[sizeof(cmd) + 8];
        snprintf(wrapped, sizeof(wrapped), "\"%s\"", cmd);
        rc = system(wrapped);
    } else {
        rc = system(cmd);
    }
    if (rc != 0) {
        fprintf(stderr, "%s failed\n", base_name(cc_path));
        return 1;
    }
    if (!emit_c && !keep_c) {
        remove(cpath);
    }
    if (cfg_near) {
        free(cfg_near);
    }
    for (int i = 0; i < c.nincdirs; i++) {
        free((void *)c.incdirs[i]);
    }
    free(c.incdirs);
    arena_free(&c.arena);
    return 0;
}
