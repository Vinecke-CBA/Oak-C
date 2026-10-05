#include "oak.h"
#include <sys/stat.h>

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return q;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

void arena_init(Arena *a) {
    a->cap = 64 * 1024;
    a->used = 0;
    a->mem = xmalloc(a->cap);
}

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7u) & ~7u;
    if (a->used + n > a->cap) {
        while (a->used + n > a->cap) {
            a->cap *= 2;
        }
        char *neu = xmalloc(a->cap);
        memcpy(neu, a->mem, a->used);
        free(a->mem);
        a->mem = neu;
    }
    void *p = a->mem + a->used;
    a->used += n;
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *d = arena_alloc(a, n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

void arena_free(Arena *a) {
    free(a->mem);
    a->mem = NULL;
}

const char *intern(Comp *c, const char *s, size_t n) {
    for (int i = 0; i < c->ninterns; i++) {
        if (strlen(c->interns[i]) == n && memcmp(c->interns[i], s, n) == 0) {
            return c->interns[i];
        }
    }
    if (c->ninterns == c->capinterns) {
        c->capinterns = c->capinterns ? c->capinterns * 2 : 64;
        c->interns = xrealloc(c->interns, (size_t)c->capinterns * sizeof(char *));
    }
    const char *d = arena_strndup(&c->arena, s, n);
    c->interns[c->ninterns++] = d;
    return d;
}

void comp_error(Comp *c, int line, int col, const char *fmt, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", c->filename, line, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    c->errors++;
}

Type type_i8(void) {
    Type t = {TY_I8, -1, NULL, NULL};
    return t;
}
Type type_u8(void) {
    Type t = {TY_U8, -1, NULL, NULL};
    return t;
}
Type type_i16(void) {
    Type t = {TY_I16, -1, NULL, NULL};
    return t;
}
Type type_u16(void) {
    Type t = {TY_U16, -1, NULL, NULL};
    return t;
}
Type type_i32(void) {
    Type t = {TY_I32, -1, NULL, NULL};
    return t;
}
Type type_u32(void) {
    Type t = {TY_U32, -1, NULL, NULL};
    return t;
}
Type type_i64(void) {
    Type t = {TY_I64, -1, NULL, NULL};
    return t;
}
Type type_u64(void) {
    Type t = {TY_U64, -1, NULL, NULL};
    return t;
}
Type type_f64(void) {
    Type t = {TY_F64, -1, NULL, NULL};
    return t;
}
Type type_bool(void) {
    Type t = {TY_BOOL, -1, NULL, NULL};
    return t;
}
Type type_unit(void) {
    Type t = {TY_UNIT, -1, NULL, NULL};
    return t;
}
Type type_struct(int id) {
    Type t = {TY_STRUCT, id, NULL, NULL};
    return t;
}
Type type_string(void) {
    Type t = {TY_STRING, -1, NULL, NULL};
    return t;
}
Type type_ptr(void) {
    Type t = {TY_PTR, -1, NULL, NULL};
    return t;
}
/* A pointer with a known pointee: `ptr<i32>`. Codegen still emits `void *` for
   every pointer (C converts void* to and from any object pointer implicitly),
   but the pointee lets `defer(p)` know what type it loads and stores. */
Type type_ptr_to(Type *elem) {
    Type t = {TY_PTR, -1, elem, NULL};
    return t;
}
Type type_raw(const char *text) {
    Type t = {TY_RAW, -1, NULL, NULL};
    t.raw = text;
    return t;
}
Type type_array(Type *elem) {
    Type t = {TY_ARRAY, -1, elem, NULL};
    return t;
}
bool type_eq(Type a, Type b) {
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == TY_STRUCT) {
        return a.struct_id == b.struct_id;
    }
    if (a.kind == TY_ARRAY) {
        return type_eq(*a.elem, *b.elem);
    }
    if (a.kind == TY_PTR) {
        /* `ptr` on its own is a void*: it matches a pointer to anything, in
           either direction, exactly like C converts void* to and from T*. Two
           typed pointers must agree on what they point at. */
        if (!a.elem || !b.elem) {
            return true;
        }
        return type_eq(*a.elem, *b.elem);
    }
    if (a.kind == TY_RAW) {
        return a.raw && b.raw && strcmp(a.raw, b.raw) == 0;
    }
    return true;
}
bool type_is_struct(Type t) {
    return t.kind == TY_STRUCT;
}
bool type_is_float(Type t) {
    return t.kind == TY_F64;
}
bool type_is_int(Type t) {
    return t.kind >= TY_I8 && t.kind <= TY_U64;
}

/* Signed and unsigned bounds for an integer type, as far as an int64 literal
   can express them: u64 reports INT64_MAX because Oak literals are int64 and
   can never be larger. Non-integer kinds report false. */
bool type_int_bounds(Type t, int64_t *lo, int64_t *hi) {
    switch (t.kind) {
    case TY_I8:
        *lo = -128;
        *hi = 127;
        return true;
    case TY_U8:
        *lo = 0;
        *hi = 255;
        return true;
    case TY_I16:
        *lo = -32768;
        *hi = 32767;
        return true;
    case TY_U16:
        *lo = 0;
        *hi = 65535;
        return true;
    case TY_I32:
        *lo = INT32_MIN;
        *hi = INT32_MAX;
        return true;
    case TY_U32:
        *lo = 0;
        *hi = UINT32_MAX;
        return true;
    case TY_I64:
        *lo = INT64_MIN;
        *hi = INT64_MAX;
        return true;
    case TY_U64:
        *lo = 0;
        *hi = INT64_MAX;
        return true;
    default:
        return false;
    }
}

const char *type_name(Comp *c, Type t) {
    switch (t.kind) {
    case TY_I8:
        return "i8";
    case TY_U8:
        return "u8";
    case TY_I16:
        return "i16";
    case TY_U16:
        return "u16";
    case TY_I32:
        return "i32";
    case TY_U32:
        return "u32";
    case TY_I64:
        return "i64";
    case TY_U64:
        return "u64";
    case TY_F64:
        return "f64";
    case TY_BOOL:
        return "bool";
    case TY_UNIT:
        return "()";
    case TY_STRING:
        return "string";
    case TY_PTR: {
        static char buf[256];
        if (!t.elem) {
            return "ptr";
        }
        snprintf(buf, sizeof(buf), "ptr<%s>", type_name(c, *t.elem));
        return buf;
    }
    case TY_RAW: {
        static char buf[256];
        snprintf(buf, sizeof(buf), "C\"%s\"", t.raw ? t.raw : "?");
        return buf;
    }
    case TY_ARRAY: {
        static char buf[512];
        const char *inner = type_name(c, *t.elem);
        snprintf(buf, sizeof(buf), "%s[]", inner);
        return buf;
    }
    case TY_STRUCT:
        if (t.struct_id >= 0 && t.struct_id < c->prog.nstructs) {
            return c->prog.structs[t.struct_id]->name;
        }
        return "<struct>";
    }
    return "?";
}

void ptrlist_push(void ***arr, int *n, int *cap, void *p) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *arr = xrealloc(*arr, (size_t)(*cap) * sizeof(void *));
    }
    (*arr)[(*n)++] = p;
}

/* -------------------------------------------------------------- paths --
   Shared by the driver (import/`as Oak` resolution) and the parser (which
   resolves `include "x.c" as extern C` relative to the including file). */

char *path_dir(const char *path) {
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

char *path_join(const char *dir, const char *rel) {
    size_t a = strlen(dir), b = strlen(rel);
    char *o = xmalloc(a + b + 2);
    memcpy(o, dir, a);
    o[a] = '/';
    memcpy(o + a + 1, rel, b + 1);
    return o;
}

bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

char *path_canonical(const char *path) {
#ifdef _WIN32
    char buf[4096];
    if (_fullpath(buf, path, sizeof(buf))) {
        for (char *p = buf; *p; p++) {
            if (*p == '\\') {
                *p = '/';
            }
        }
        return xstrdup(buf);
    }
    return xstrdup(path);
#else
    char buf[4096];
    if (realpath(path, buf)) {
        return xstrdup(buf);
    }
    return xstrdup(path);
#endif
}

/* True for an existing directory. `file_exists` cannot be used here: fopen
   on a directory fails on Windows but succeeds on Linux, so the two
   platforms would disagree about whether include/ should be searched. */
bool dir_exists(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IFDIR) != 0;
}

/* Last-resort lookup used by both file loaders: the parser for
   `include "x.c" as extern C` / `as Oak`, and the driver for `import`.
   Only consulted after the historical lookups (next to the including file,
   then the working directory) have failed, so adding an include/ folder can
   never change the meaning of a program that already resolves. */
char *comp_find_include(Comp *c, const char *spec) {
    if (!c) {
        return NULL;
    }
    for (int i = 0; i < c->nincdirs; i++) {
        char *p = path_join(c->incdirs[i], spec);
        if (file_exists(p)) {
            return p;
        }
        free(p);
    }
    return NULL;
}
