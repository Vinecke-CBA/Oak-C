#include "oak.h"

#ifdef _WIN32
#define DEFAULT_OUT "a.exe"
#else
#define DEFAULT_OUT "a.out"
#endif

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
    fprintf(stderr, "usage: oakc <file.oak> [-o binary] [--emit-c file.c] [--keep-c] [-- <cc flags...>]\n");
}

static char *dir_of(const char *path) {
    const char *s1 = strrchr(path, '/');
    const char *s2 = strrchr(path, '\\');
    const char *s = s1 > s2 ? s1 : s2;
    if (!s) {
        return xstrdup(".");
    }
    size_t n = (size_t)(s - path);
    if (n == 0) {
        n = 1;
    }
    char *d = xmalloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

static bool already_loaded(Comp *c, const char *canon) {
    for (int i = 0; i < c->nloaded; i++) {
        if (strcmp(c->loaded[i], canon) == 0) {
            return true;
        }
    }
    return false;
}

static char *join_path(const char *dir, const char *rel) {
    size_t a = strlen(dir), b = strlen(rel);
    char *o = xmalloc(a + b + 2);
    memcpy(o, dir, a);
    o[a] = '/';
    memcpy(o + a + 1, rel, b + 1);
    return o;
}

static bool file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

static void parse_file(Comp *c, const char *path, const char *referrer_dir) {
    char *cand = NULL;
    if (file_exists(path)) {
        cand = xstrdup(path);
    } else if (referrer_dir) {
        cand = join_path(referrer_dir, path);
        if (!file_exists(cand)) {
            fprintf(stderr, "cannot open '%s' (imported)\n", path);
            exit(1);
        }
    } else {
        fprintf(stderr, "cannot open '%s'\n", path);
        exit(1);
    }
    if (already_loaded(c, cand)) {
        free(cand);
        return;
    }
    ptrlist_push((void ***)&c->loaded, &c->nloaded, &c->caploaded, (void *)cand);
    size_t flen = 0;
    char *src = read_file(cand, &flen);
    const char *save_src = c->src;
    size_t save_len = c->len;
    const char *save_name = c->filename;
    c->src = src;
    c->len = flen;
    c->filename = cand;
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
    tmp.src = src;
    tmp.len = flen;
    tmp.filename = cand;
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
    char *dir = dir_of(cand);
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
    const char *out_bin = DEFAULT_OUT;
    const char *emit_c = NULL;
    bool keep_c = false;
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

    Comp c;
    memset(&c, 0, sizeof(c));
    c.filename = input;
    arena_init(&c.arena);
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

    char cmd[8192];
    int w = snprintf(cmd, sizeof(cmd), "gcc -std=c99 -O2 -o \"%s\" \"%s\"", out_bin, cpath);
    for (int i = 0; i < ncc && w > 0 && (size_t)w < sizeof(cmd) - 2; i++) {
        w += snprintf(cmd + w, sizeof(cmd) - (size_t)w, " %s", ccflags[i]);
    }
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "gcc failed\n");
        return 1;
    }
    if (!emit_c && !keep_c) {
        remove(cpath);
    }
    arena_free(&c.arena);
    return 0;
}
