#include "oak.h"

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

Type type_i32(void) {
    Type t = {TY_I32, -1, NULL, NULL};
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

const char *type_name(Comp *c, Type t) {
    switch (t.kind) {
    case TY_I32:
        return "i32";
    case TY_F64:
        return "f64";
    case TY_BOOL:
        return "bool";
    case TY_UNIT:
        return "()";
    case TY_STRING:
        return "string";
    case TY_PTR:
        return "ptr";
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
