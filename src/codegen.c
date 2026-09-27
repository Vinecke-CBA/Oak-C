#include "oak.h"

typedef struct {
    Comp *c;
    FILE *out;
    int indent;
    int tmp;
} CG;

static FnDef *find_fn(Comp *c, const char *name) {
    for (int i = 0; i < c->prog.nfns; i++) {
        if (c->prog.fns[i]->name == name) {
            return c->prog.fns[i];
        }
    }
    return NULL;
}

static char *strf(const char *fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    return xstrdup(buf);
}

/* Turn raw string bytes back into a C string literal body. */
static char *escape_c_string(const char *s) {
    size_t n = strlen(s);
    char *out = xmalloc(n * 4 + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (ch == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else if (ch == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (ch == '\\') {
            out[j++] = '\\';
            out[j++] = '\\';
        } else if (ch == '"') {
            out[j++] = '\\';
            out[j++] = '"';
        } else if (ch < 32 || ch >= 127) {
            j += (size_t)sprintf(out + j, "\\%03o", ch);
        } else {
            out[j++] = (char)ch;
        }
    }
    out[j] = 0;
    return out;
}

static void ind(CG *g) {
    for (int i = 0; i < g->indent; i++) {
        fputs("    ", g->out);
    }
}

static const char *ctype(Comp *c, Type t) {
    switch (t.kind) {
    case TY_I32:
        return "int32_t";
    case TY_F64:
        return "double";
    case TY_BOOL:
        return "bool";
    case TY_UNIT:
        return "void";
    case TY_STRING:
        return "OakStr";
    case TY_PTR:
        return "void *";
    case TY_RAW:
        return t.raw ? t.raw : "int32_t";
    case TY_ARRAY:
        return "OakArr *";
    case TY_STRUCT:
        return c->prog.structs[t.struct_id]->name;
    }
    return "void";
}

/* Which runtime helpers does this program need? */
#define F_STR 1       /* some string value exists */
#define F_STRCAT 2    /* string + string */
#define F_STREQ 4     /* string == / != */
#define F_STRCMP 8    /* string < > <= >= */
#define F_STRLEN 16   /* len(string) */
#define F_STRAT 32    /* s[i] */
#define F_STRSLICE 64 /* s[a:b] */
#define F_ARR 128     /* some array value exists */
#define F_ARRGET 256  /* a[i], len(a), push() */
#define F_IDX 512     /* bounds / negative index helper */

static int expr_flags(Expr *e) {
    if (!e) {
        return 0;
    }
    int f = 0;
    if (e->type.kind == TY_STRING) {
        f |= F_STR;
    }
    if (e->type.kind == TY_ARRAY) {
        f |= F_ARR;
    }
    switch (e->kind) {
    case EX_STRING:
        f |= F_STR;
        break;
    case EX_FIELD:
        f |= expr_flags(e->field.base);
        break;
    case EX_ARRAY:
        f |= F_ARR;
        for (int i = 0; i < e->arr.n; i++) {
            f |= expr_flags(e->arr.elems[i]);
        }
        break;
    case EX_INDEX:
        f |= expr_flags(e->index.base) | expr_flags(e->index.idx);
        if (e->index.base->type.kind == TY_STRING) {
            f |= F_STR | F_STRAT | F_IDX;
        } else {
            f |= F_ARR | F_ARRGET | F_IDX;
        }
        break;
    case EX_SLICE:
        f |= F_STR | F_STRSLICE | F_IDX | expr_flags(e->slice.base);
        f |= expr_flags(e->slice.lo) | expr_flags(e->slice.hi);
        break;
    case EX_STRUCT:
        for (int i = 0; i < e->slit.nfields; i++) {
            f |= expr_flags(e->slit.fvals[i]);
        }
        break;
    case EX_CALL: {
        if (strcmp(e->call.name, "push") == 0) {
            f |= F_ARR | F_ARRGET;
        } else if (strcmp(e->call.name, "len") == 0 && e->call.nargs == 1) {
            Type at = e->call.args[0]->type;
            if (at.kind == TY_STRING) {
                f |= F_STR | F_STRLEN;
            } else if (at.kind == TY_ARRAY) {
                f |= F_ARR | F_ARRGET;
            }
        }
        for (int i = 0; i < e->call.nargs; i++) {
            f |= expr_flags(e->call.args[i]);
        }
        break;
    }
    case EX_BIN:
        f |= expr_flags(e->bin.l) | expr_flags(e->bin.r);
        if (e->bin.l->type.kind == TY_STRING && e->bin.r->type.kind == TY_STRING) {
            f |= F_STR;
            if (e->bin.op == TOK_PLUS) {
                f |= F_STRCAT;
            } else if (e->bin.op == TOK_EQEQ || e->bin.op == TOK_NEQ) {
                f |= F_STREQ;
            } else {
                f |= F_STRCMP;
            }
        }
        break;
    case EX_UNARY:
        f |= expr_flags(e->un.e);
        break;
    default:
        break;
    }
    return f;
}

static int stmt_flags(Stmt *s) {
    if (!s) {
        return 0;
    }
    int f = 0;
    switch (s->kind) {
    case ST_VAR:
        f |= expr_flags(s->var.init);
        break;
    case ST_ASSIGN:
        f |= expr_flags(s->assign.place) | expr_flags(s->assign.value);
        break;
    case ST_IF:
        f |= expr_flags(s->ifs.cond) | stmt_flags(s->ifs.then_b) | stmt_flags(s->ifs.else_b);
        break;
    case ST_WHILE:
        f |= expr_flags(s->wh.cond) | stmt_flags(s->wh.body);
        break;
    case ST_RETURN:
        f |= expr_flags(s->ret.value);
        break;
    case ST_EXPR:
        f |= expr_flags(s->expr);
        break;
    case ST_BLOCK:
        for (int i = 0; i < s->block.n; i++) {
            f |= stmt_flags(s->block.stmts[i]);
        }
        break;
    }
    return f;
}

static int type_flags(Type t) {
    int f = 0;
    if (t.kind == TY_STRING) {
        f |= F_STR;
    }
    if (t.kind == TY_ARRAY) {
        f |= F_ARR;
    }
    return f;
}

static int prog_flags(Comp *c) {
    int f = 0;
    for (int i = 0; i < c->prog.nfns; i++) {
        FnDef *fn = c->prog.fns[i];
        f |= type_flags(fn->ret);
        for (int p = 0; p < fn->nparams; p++) {
            f |= type_flags(fn->params[p].type);
        }
        if (!fn->is_extern) {
            f |= stmt_flags(fn->body);
        }
    }
    for (int i = 0; i < c->prog.nstructs; i++) {
        StructDef *st = c->prog.structs[i];
        for (int j = 0; j < st->nfields; j++) {
            f |= type_flags(st->fields[j].type);
        }
    }
    return f;
}

static void emit_runtime(CG *g) {
    int f = prog_flags(g->c);
    FILE *out = g->out;
    if (!(f & (F_STR | F_ARR))) {
        return;
    }
    if (f & F_STR) {
        fputs("typedef const char *OakStr;\n", out);
    }
    if (f & F_ARR) {
        fputs("typedef struct OakArr {\n"
              "    int64_t len;\n"
              "    int64_t cap;\n"
              "    size_t esize;\n"
              "    void *data;\n"
              "} OakArr;\n",
              out);
    }
    fputs("static void *oak_alloc(size_t n) {\n"
          "    void *p = malloc(n ? n : 1);\n"
          "    if (!p) { fprintf(stderr, \"oak: out of memory\\n\"); exit(1); }\n"
          "    return p;\n"
          "}\n",
          out);
    if (f & (F_IDX | F_ARRGET | F_STRAT | F_STRSLICE)) {
        fputs("static int64_t oak_idx(int64_t i, int64_t len) {\n"
              "    if (i < 0) i += len;\n"
              "    if (i < 0 || i >= len) {\n"
              "        fprintf(stderr, \"oak: index %lld out of range for length %lld\\n\",\n"
              "                (long long)i, (long long)len);\n"
              "        exit(1);\n"
              "    }\n"
              "    return i;\n"
              "}\n",
              out);
    }
    if (f & F_ARR) {
        fputs("static OakArr *oak_arr_new(int64_t cap, size_t esize) {\n"
              "    OakArr *a = (OakArr *)oak_alloc(sizeof(OakArr));\n"
              "    if (cap < 1) cap = 1;\n"
              "    a->len = 0;\n"
              "    a->cap = cap;\n"
              "    a->esize = esize;\n"
              "    a->data = oak_alloc((size_t)cap * esize);\n"
              "    return a;\n"
              "}\n"
              "static OakArr *oak_arr_from(int64_t n, size_t esize, const void *src) {\n"
              "    OakArr *a = oak_arr_new(n, esize);\n"
              "    a->len = n;\n"
              "    if (n > 0) memcpy(a->data, src, (size_t)n * esize);\n"
              "    return a;\n"
              "}\n"
              "static void oak_arr_push(OakArr *a, const void *v) {\n"
              "    if (a->len == a->cap) {\n"
              "        a->cap = a->cap ? a->cap * 2 : 4;\n"
              "        a->data = realloc(a->data, (size_t)a->cap * a->esize);\n"
              "        if (!a->data) { fprintf(stderr, \"oak: out of memory\\n\"); exit(1); }\n"
              "    }\n"
              "    memcpy((char *)a->data + (size_t)a->len * a->esize, v, a->esize);\n"
              "    a->len++;\n"
              "}\n",
              out);
    }
    if (f & F_ARRGET) {
        fputs("static void *oak_arr_slot(OakArr *a, int64_t i) {\n"
              "    return (char *)a->data + (size_t)oak_idx(i, a->len) * a->esize;\n"
              "}\n"
              "static int64_t oak_arr_len(const OakArr *a) { return a->len; }\n",
              out);
    }
    fputc('\n', out);
}

static void emit_string_runtime(CG *g) {
    int f = prog_flags(g->c);
    if (f & F_STRCAT) {
        fputs("static OakStr oak_str_concat(OakStr a, OakStr b) {\n"
              "    size_t la = strlen(a), lb = strlen(b);\n"
              "    char *out = (char *)oak_alloc(la + lb + 1);\n"
              "    memcpy(out, a, la);\n"
              "    memcpy(out + la, b, lb);\n"
              "    out[la + lb] = '\\0';\n"
              "    return out;\n"
              "}\n",
              g->out);
    }
    if (f & F_STREQ) {
        fputs("static bool oak_str_eq(OakStr a, OakStr b) { return strcmp(a, b) == 0; }\n",
              g->out);
    }
    if (f & F_STRCMP) {
        fputs("static int oak_str_cmp(OakStr a, OakStr b) { return strcmp(a, b); }\n", g->out);
    }
    if (f & F_STRLEN) {
        fputs("static int64_t oak_str_len(OakStr s) { return (int64_t)strlen(s); }\n", g->out);
    }
    if (f & F_STRAT) {
        fputs("static OakStr oak_str_at(OakStr s, int64_t i) {\n"
              "    int64_t n = (int64_t)strlen(s);\n"
              "    char *out = (char *)oak_alloc(2);\n"
              "    out[0] = s[oak_idx(i, n)];\n"
              "    out[1] = '\\0';\n"
              "    return out;\n"
              "}\n",
              g->out);
    }
    if (f & F_STRSLICE) {
        fputs("static OakStr oak_str_slice(OakStr s, int64_t a, int64_t b) {\n"
              "    int64_t n = (int64_t)strlen(s);\n"
              "    if (a < 0) a += n;\n"
              "    if (a < 0) a = 0;\n"
              "    if (a > n) a = n;\n"
              "    if (b < 0) b += n;\n"
              "    if (b < 0) b = 0;\n"
              "    if (b > n) b = n;\n"
              "    if (b < a) b = a;\n"
              "    char *out = (char *)oak_alloc((size_t)(b - a) + 1);\n"
              "    memcpy(out, s + a, (size_t)(b - a));\n"
              "    out[b - a] = '\\0';\n"
              "    return out;\n"
              "}\n",
              g->out);
    }
    fputc('\n', g->out);
}

static const char *cop(TokKind k) {
    switch (k) {
    case TOK_PLUS:
        return "+";
    case TOK_MINUS:
        return "-";
    case TOK_STAR:
        return "*";
    case TOK_SLASH:
        return "/";
    case TOK_PERCENT:
        return "%";
    case TOK_EQEQ:
        return "==";
    case TOK_NEQ:
        return "!=";
    case TOK_LT:
        return "<";
    case TOK_GT:
        return ">";
    case TOK_LE:
        return "<=";
    case TOK_GE:
        return ">=";
    case TOK_ANDAND:
        return "&&";
    case TOK_OROR:
        return "||";
    default:
        return "?";
    }
}

static bool is_place(Expr *e) {
    if (e->kind == EX_VAR) {
        return true;
    }
    if (e->kind == EX_FIELD) {
        return is_place(e->field.base);
    }
    if (e->kind == EX_INDEX) {
        return e->index.base->type.kind == TY_ARRAY;
    }
    return false;
}

static char *rval(CG *g, Expr *e);

static char *place(CG *g, Expr *e) {
    if (e->kind == EX_VAR) {
        if (e->var.sym >= 0 && g->c->syms[e->var.sym].is_struct_param) {
            return strf("(*%s)", e->var.name);
        }
        return strf("%s", e->var.name);
    }
    if (e->kind == EX_FIELD) {
        /* A field of a temporary, e.g. (Point{...}).x or f().x, is readable but
           not addressable; inline the base value instead of a bare place. */
        char *b = is_place(e->field.base) ? place(g, e->field.base)
                                          : rval(g, e->field.base);
        char *r = strf("(%s).%s", b, e->field.field);
        free(b);
        return r;
    }
    if (e->kind == EX_INDEX) {
        /* Array elements are addressable; the element type is known here. */
        const char *et = ctype(g->c, *e->index.base->type.elem);
        char *b = rval(g, e->index.base);
        char *i = rval(g, e->index.idx);
        char *r = strf("(*(%s *)oak_arr_slot(%s, %s))", et, b, i);
        free(b);
        free(i);
        return r;
    }
    return strf("0");
}

static char *rval(CG *g, Expr *e);

static char *emit_call(CG *g, Expr *e) {
    if (strcmp(e->call.name, "print") == 0) {
        char *a = e->call.nargs ? rval(g, e->call.args[0]) : xstrdup("0");
        Type at = e->call.nargs ? e->call.args[0]->type : type_i32();
        char *s;
        if (at.kind == TY_F64) {
            s = strf("printf(\"%%g\\n\", (double)(%s))", a);
        } else if (at.kind == TY_STRING) {
            s = strf("printf(\"%%s\\n\", (const char *)(%s))", a);
        } else if (at.kind == TY_BOOL) {
            s = strf("printf(\"%%s\\n\", (%s) ? \"true\" : \"false\")", a);
        } else if (at.kind == TY_PTR) {
            s = strf("printf(\"%%p\\n\", (void *)(%s))", a);
        } else {
            s = strf("printf(\"%%d\\n\", (int)(%s))", a);
        }
        free(a);
        return s;
    }
    if (strcmp(e->call.name, "len") == 0) {
        char *a = e->call.nargs ? rval(g, e->call.args[0]) : xstrdup("0");
        Type at = e->call.nargs ? e->call.args[0]->type : type_i32();
        char *s;
        if (at.kind == TY_STRING) {
            s = strf("((int32_t)oak_str_len(%s))", a);
        } else {
            s = strf("((int32_t)oak_arr_len(%s))", a);
        }
        free(a);
        return s;
    }
    if (strcmp(e->call.name, "push") == 0) {
        char *b = rval(g, e->call.args[0]);
        Type at = e->call.args[0]->type;
        const char *et = ctype(g->c, *at.elem);
        char *v = rval(g, e->call.args[1]);
        char *s = strf("oak_arr_push(%s, &(%s){ %s })", b, et, v);
        free(b);
        free(v);
        return s;
    }
    FnDef *fn = find_fn(g->c, e->call.name);
    const char *cname = e->call.name;
    char **parts = NULL;
    if (e->call.nargs) {
        parts = xmalloc((size_t)e->call.nargs * sizeof(char *));
    }
    for (int i = 0; i < e->call.nargs; i++) {
        bool as_ptr = fn && i < fn->nparams && type_is_struct(fn->params[i].type);
        if (as_ptr) {
            if (is_place(e->call.args[i])) {
                char *p = place(g, e->call.args[i]);
                parts[i] = strf("&(%s)", p);
                free(p);
            } else {
                int t = g->tmp++;
                const char *ty = ctype(g->c, e->call.args[i]->type);
                char *r = rval(g, e->call.args[i]);
                ind(g);
                fprintf(g->out, "%s _oak_t%d = %s;\n", ty, t, r);
                free(r);
                parts[i] = strf("&_oak_t%d", t);
            }
        } else {
            parts[i] = rval(g, e->call.args[i]);
        }
    }
    size_t len = strlen(cname) + 3;
    for (int i = 0; i < e->call.nargs; i++) {
        len += strlen(parts[i]) + 2;
    }
    char *s = xmalloc(len + 8);
    strcpy(s, cname);
    strcat(s, "(");
    for (int i = 0; i < e->call.nargs; i++) {
        if (i) {
            strcat(s, ", ");
        }
        strcat(s, parts[i]);
        free(parts[i]);
    }
    strcat(s, ")");
    free(parts);
    return s;
}

static char *rval(CG *g, Expr *e) {
    switch (e->kind) {
    case EX_INT:
        return strf("%lld", (long long)e->ival);
    case EX_FLOAT: {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", e->fval);
        if (!strpbrk(buf, ".eE")) {
            strcat(buf, ".0"); /* keep C from treating it as an int */
        }
        return xstrdup(buf);
    }
    case EX_NULL:
        return xstrdup("NULL");
    case EX_BOOL:
        return xstrdup(e->bval ? "true" : "false");
    case EX_STRING: {
        char *esc = escape_c_string(e->sval);
        char *s = strf("\"%s\"", esc);
        free(esc);
        return s;
    }
    case EX_VAR:
    case EX_FIELD:
        return place(g, e);
    case EX_INDEX:
        if (e->index.base->type.kind == TY_STRING) {
            char *b = rval(g, e->index.base);
            char *i = rval(g, e->index.idx);
            char *s = strf("oak_str_at(%s, (int64_t)(%s))", b, i);
            free(b);
            free(i);
            return s;
        }
        return place(g, e);
    case EX_SLICE: {
        char *b = rval(g, e->slice.base);
        char *lo = e->slice.lo ? rval(g, e->slice.lo) : xstrdup("0");
        char *hi = e->slice.hi ? rval(g, e->slice.hi) : xstrdup("INT64_MAX");
        char *s = strf("oak_str_slice(%s, (int64_t)(%s), (int64_t)(%s))", b, lo, hi);
        free(b);
        free(lo);
        free(hi);
        return s;
    }
    case EX_ARRAY: {
        const char *et = ctype(g->c, *e->type.elem);
        char *body = xstrdup("0");
        for (int i = 0; i < e->arr.n; i++) {
            char *v = rval(g, e->arr.elems[i]);
            char *n = i == 0 ? strf("%s", v) : strf("%s, %s", body, v);
            free(v);
            if (i != 0) {
                free(body);
            }
            body = n;
        }
        char *s = strf("oak_arr_from(%d, sizeof(%s), (%s[]){ %s })", e->arr.n, et, et, body);
        free(body);
        return s;
    }
    case EX_STRUCT: {
        StructDef *st = g->c->prog.structs[e->slit.struct_id];
        char *s = strf("((%s){", st->name);
        for (int i = 0; i < e->slit.nfields; i++) {
            char *v = rval(g, e->slit.fvals[i]);
            char *n = strf("%s.%s = %s%s", s, e->slit.fnames[i], v,
                           i + 1 < e->slit.nfields ? ", " : "})");
            free(s);
            free(v);
            s = n;
        }
        if (e->slit.nfields == 0) {
            char *n = strf("%s})", s);
            free(s);
            s = n;
        }
        return s;
    }
    case EX_CALL:
        return emit_call(g, e);
    case EX_BIN: {
        bool ls = e->bin.l->type.kind == TY_STRING;
        if (ls && e->bin.r->type.kind == TY_STRING) {
            char *l = rval(g, e->bin.l);
            char *r = rval(g, e->bin.r);
            char *s;
            if (e->bin.op == TOK_PLUS) {
                s = strf("oak_str_concat((OakStr)(%s), (OakStr)(%s))", l, r);
            } else if (e->bin.op == TOK_EQEQ || e->bin.op == TOK_NEQ) {
                s = strf("%soak_str_eq((OakStr)(%s), (OakStr)(%s))",
                         e->bin.op == TOK_NEQ ? "!" : "", l, r);
            } else if (e->bin.op == TOK_LT || e->bin.op == TOK_GT || e->bin.op == TOK_LE ||
                       e->bin.op == TOK_GE) {
                s = strf("(oak_str_cmp((OakStr)(%s), (OakStr)(%s)) %s 0)", l, r,
                         cop(e->bin.op));
            } else {
                s = strf("(%s %s %s)", l, cop(e->bin.op), r);
            }
            free(l);
            free(r);
            return s;
        }
        char *l = rval(g, e->bin.l);
        char *r = rval(g, e->bin.r);
        char *s = strf("(%s %s %s)", l, cop(e->bin.op), r);
        free(l);
        free(r);
        return s;
    }
    case EX_UNARY: {
        char *a = rval(g, e->un.e);
        char *s = strf("(%s%s)", e->un.op == TOK_NOT ? "!" : "-", a);
        free(a);
        return s;
    }
    }
    return xstrdup("0");
}

static void emit_stmt(CG *g, Stmt *s);

static void emit_block_inner(CG *g, Stmt *s) {
    if (s->kind == ST_BLOCK) {
        for (int i = 0; i < s->block.n; i++) {
            emit_stmt(g, s->block.stmts[i]);
        }
    } else {
        emit_stmt(g, s);
    }
}

static void emit_stmt(CG *g, Stmt *s) {
    switch (s->kind) {
    case ST_VAR: {
        char *r = rval(g, s->var.init);
        ind(g);
        fprintf(g->out, "%s %s = %s;\n", ctype(g->c, s->var.init->type), s->var.name, r);
        free(r);
        break;
    }
    case ST_ASSIGN: {
        char *p = place(g, s->assign.place);
        char *r = rval(g, s->assign.value);
        ind(g);
        fprintf(g->out, "%s = %s;\n", p, r);
        free(p);
        free(r);
        break;
    }
    case ST_IF: {
        char *cond = rval(g, s->ifs.cond);
        ind(g);
        fprintf(g->out, "if (%s) {\n", cond);
        free(cond);
        g->indent++;
        emit_block_inner(g, s->ifs.then_b);
        g->indent--;
        if (s->ifs.else_b) {
            ind(g);
            fputs("} else {\n", g->out);
            g->indent++;
            emit_block_inner(g, s->ifs.else_b);
            g->indent--;
            ind(g);
            fputs("}\n", g->out);
        } else {
            ind(g);
            fputs("}\n", g->out);
        }
        break;
    }
    case ST_WHILE: {
        char *cond = rval(g, s->wh.cond);
        ind(g);
        fprintf(g->out, "while (%s) {\n", cond);
        free(cond);
        g->indent++;
        emit_block_inner(g, s->wh.body);
        g->indent--;
        ind(g);
        fputs("}\n", g->out);
        break;
    }
    case ST_RETURN: {
        if (!s->ret.value) {
            ind(g);
            fputs("return;\n", g->out);
            break;
        }
        char *r = rval(g, s->ret.value);
        ind(g);
        fprintf(g->out, "return %s;\n", r);
        free(r);
        break;
    }
    case ST_EXPR: {
        char *r = rval(g, s->expr);
        ind(g);
        fprintf(g->out, "%s;\n", r);
        free(r);
        break;
    }
    case ST_BLOCK:
        ind(g);
        fputs("{\n", g->out);
        g->indent++;
        emit_block_inner(g, s);
        g->indent--;
        ind(g);
        fputs("}\n", g->out);
        break;
    }
}

static void emit_structs(CG *g) {
    int n = g->c->prog.nstructs;
    if (n == 0) {
        return;
    }
    char *done = xmalloc((size_t)n);
    memset(done, 0, (size_t)n);
    int left = n;
    while (left > 0) {
        bool progress = false;
        for (int i = 0; i < n; i++) {
            if (done[i]) {
                continue;
            }
            StructDef *st = g->c->prog.structs[i];
            bool ok = true;
            for (int f = 0; f < st->nfields; f++) {
                if (type_is_struct(st->fields[f].type) && !done[st->fields[f].type.struct_id]) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                continue;
            }
            fprintf(g->out, "typedef struct {\n");
            for (int f = 0; f < st->nfields; f++) {
                fprintf(g->out, "    %s %s;\n", ctype(g->c, st->fields[f].type), st->fields[f].name);
            }
            fprintf(g->out, "} %s;\n\n", st->name);
            done[i] = 1;
            left--;
            progress = true;
        }
        if (!progress) {
            break;
        }
    }
    free(done);
}

static void emit_sig(CG *g, FnDef *fn, bool semi) {
    if (fn->is_main) {
        fprintf(g->out, "int main(void)");
    } else {
        fprintf(g->out, "static %s %s(", ctype(g->c, fn->ret), fn->name);
        if (fn->nparams == 0) {
            fputs("void", g->out);
        }
        for (int i = 0; i < fn->nparams; i++) {
            if (i) {
                fputs(", ", g->out);
            }
            if (type_is_struct(fn->params[i].type)) {
                fprintf(g->out, "%s *%s", ctype(g->c, fn->params[i].type), fn->params[i].name);
            } else {
                fprintf(g->out, "%s %s", ctype(g->c, fn->params[i].type), fn->params[i].name);
            }
        }
        fputc(')', g->out);
    }
    fputs(semi ? ";\n" : " {\n", g->out);
}

static void emit_proto(CG *g, FnDef *fn) {
    Comp *c = g->c;
    if (c->no_protos) {
        return;
    }
    fprintf(g->out, "extern %s %s(", ctype(c, fn->ret), fn->name);
    if (fn->nparams == 0 && !fn->is_vararg) {
        fputs("void", g->out);
    }
    for (int i = 0; i < fn->nparams; i++) {
        if (i) {
            fputs(", ", g->out);
        }
        fprintf(g->out, "%s %s", ctype(c, fn->params[i].type), fn->params[i].name);
    }
    if (fn->is_vararg) {
        if (fn->nparams) {
            fputs(", ", g->out);
        }
        fputs("...", g->out);
    }
    fputs(");\n", g->out);
}

void codegen(Comp *c, FILE *out) {
    CG g;
    g.c = c;
    g.out = out;
    g.indent = 0;
    g.tmp = 0;
    fputs("#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\n"
          "#include <string.h>\n#include <limits.h>\n\n",
          out);
    for (int i = 0; i < c->prog.nincludes; i++) {
        const char *h = c->prog.includes[i];
        bool sys = h[0] != '.' && !strchr(h, '/') && !strchr(h, '\\');
        fprintf(out, "#include %c%s%c\n", sys ? '<' : '"', h, sys ? '>' : '"');
    }
    if (c->prog.nincludes) {
        fputc('\n', out);
    }
    emit_runtime(&g);
    emit_string_runtime(&g);
    emit_structs(&g);
    for (int i = 0; i < c->prog.nfns; i++) {
        FnDef *fn = c->prog.fns[i];
        if (fn->is_extern) {
            emit_proto(&g, fn);
        } else if (!fn->is_main) {
            emit_sig(&g, fn, true);
        }
    }
    if (c->prog.nfns) {
        fputc('\n', out);
    }
    for (int i = 0; i < c->prog.nfns; i++) {
        FnDef *fn = c->prog.fns[i];
        if (fn->is_extern) {
            continue;
        }
        emit_sig(&g, fn, false);
        g.indent = 1;
        g.tmp = 0;
        emit_block_inner(&g, fn->body);
        if (fn->is_main) {
            ind(&g);
            fputs("return 0;\n", out);
        }
        fputs("}\n\n", out);
    }
}
