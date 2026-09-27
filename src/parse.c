#include "oak.h"

static const char **g_tnames;
static int g_ntnames, g_captnames;

const char *unresolved_type_name(Type t) {
    if (t.kind != TY_STRUCT || t.struct_id >= 0) {
        return NULL;
    }
    int id = -t.struct_id - 1;
    if (id < 0 || id >= g_ntnames) {
        return NULL;
    }
    return g_tnames[id];
}

static Type named_unresolved(const char *name) {
    if (g_ntnames == g_captnames) {
        g_captnames = g_captnames ? g_captnames * 2 : 32;
        g_tnames = xrealloc(g_tnames, (size_t)g_captnames * sizeof(char *));
    }
    int id = g_ntnames;
    g_tnames[g_ntnames++] = name;
    return type_struct(-(id + 1));
}

static Expr *parse_expr(Lexer *l);
static Stmt *parse_block(Lexer *l);
static Stmt *parse_stmt(Lexer *l);

/* `Name { ... }` is a struct literal, but `if cond { ... }` / `while cond { ... }`
   end a condition with an identifier too. Inside a condition we therefore treat
   `{` as the start of a block, exactly like Rust does. The restriction is lifted
   again inside parentheses, call arguments and struct literal fields, where a
   block cannot possibly follow. */
static bool g_no_struct_lit;

static bool at(Lexer *l, TokKind k) {
    return l->tok.kind == k;
}

static Token take(Lexer *l) {
    Token t = l->tok;
    lex_next(l);
    return t;
}

static Token expect(Lexer *l, TokKind k, const char *what) {
    if (!at(l, k)) {
        comp_error(l->c, l->tok.line, l->tok.col, "expected %s", what);
        return take(l);
    }
    return take(l);
}

static Expr *new_expr(Lexer *l, ExprKind k, int line, int col) {
    Expr *e = arena_alloc(&l->c->arena, sizeof(Expr));
    memset(e, 0, sizeof(Expr));
    e->kind = k;
    e->line = line;
    e->col = col;
    e->root_sym = -1;
    e->type = type_unit();
    return e;
}

static Stmt *new_stmt(Lexer *l, StmtKind k, int line, int col) {
    Stmt *s = arena_alloc(&l->c->arena, sizeof(Stmt));
    memset(s, 0, sizeof(Stmt));
    s->kind = k;
    s->line = line;
    s->col = col;
    return s;
}

static Type parse_type_atom(Lexer *l) {
    if (at(l, TOK_I32)) {
        take(l);
        return type_i32();
    }
    if (at(l, TOK_F64)) {
        take(l);
        return type_f64();
    }
    if (at(l, TOK_BOOL)) {
        take(l);
        return type_bool();
    }
    if (at(l, TOK_STRTYPE)) {
        take(l);
        return type_string();
    }
    if (at(l, TOK_PTR)) {
        take(l);
        return type_ptr();
    }
    if (at(l, TOK_IDENT)) {
        Token t = take(l);
        if (strcmp(t.ident, "C") == 0 && at(l, TOK_STR)) {
            Token s = take(l);
            return type_raw(s.sval);
        }
        return named_unresolved(t.ident);
    }
    comp_error(l->c, l->tok.line, l->tok.col, "expected type");
    return type_unit();
}

static Type parse_type(Lexer *l) {
    Type base = parse_type_atom(l);
    while (at(l, TOK_LBRACKET)) {
        take(l);
        expect(l, TOK_RBRACKET, "']'");
        Type *e = arena_alloc(&l->c->arena, sizeof(Type));
        *e = base;
        base = type_array(e);
    }
    return base;
}

static Expr *parse_struct_lit(Lexer *l, const char *name, int line, int col) {
    expect(l, TOK_LBRACE, "'{'");
    const char **fnames = NULL;
    Expr **fvals = NULL;
    int n = 0, capn = 0, capv = 0;
    if (!at(l, TOK_RBRACE)) {
        for (;;) {
            Token fn = expect(l, TOK_IDENT, "field name");
            expect(l, TOK_COLON, "':'");
            bool saved = g_no_struct_lit;
            g_no_struct_lit = false;
            Expr *v = parse_expr(l);
            g_no_struct_lit = saved;
            if (n == capn) {
                capn = capn ? capn * 2 : 4;
                fnames = xrealloc(fnames, (size_t)capn * sizeof(char *));
            }
            fnames[n] = fn.ident;
            ptrlist_push((void ***)&fvals, &n, &capv, v);
            if (!at(l, TOK_COMMA)) {
                break;
            }
            take(l);
            if (at(l, TOK_RBRACE)) {
                break;
            }
        }
    }
    expect(l, TOK_RBRACE, "'}'");
    Expr *e = new_expr(l, EX_STRUCT, line, col);
    e->slit.name = name;
    e->slit.fnames = fnames;
    e->slit.fvals = fvals;
    e->slit.nfields = n;
    e->slit.struct_id = -1;
    return e;
}

static Expr *parse_primary(Lexer *l) {
    Token t = l->tok;
    if (at(l, TOK_INT)) {
        take(l);
        Expr *e = new_expr(l, EX_INT, t.line, t.col);
        e->ival = t.ival;
        return e;
    }
    if (at(l, TOK_FLOAT)) {
        take(l);
        Expr *e = new_expr(l, EX_FLOAT, t.line, t.col);
        e->fval = t.fval;
        return e;
    }
    if (at(l, TOK_NULL)) {
        take(l);
        return new_expr(l, EX_NULL, t.line, t.col);
    }
    if (at(l, TOK_LBRACKET)) {
        Token lb = take(l);
        Expr **elems = NULL;
        int n = 0, cap = 0;
        bool saved = g_no_struct_lit;
        g_no_struct_lit = false;
        if (!at(l, TOK_RBRACKET)) {
            for (;;) {
                ptrlist_push((void ***)&elems, &n, &cap, parse_expr(l));
                if (!at(l, TOK_COMMA)) {
                    break;
                }
                take(l);
                if (at(l, TOK_RBRACKET)) {
                    break;
                }
            }
        }
        g_no_struct_lit = saved;
        expect(l, TOK_RBRACKET, "']'");
        Expr *e = new_expr(l, EX_ARRAY, lb.line, lb.col);
        e->arr.elems = elems;
        e->arr.n = n;
        return e;
    }
    if (at(l, TOK_TRUE) || at(l, TOK_FALSE)) {
        take(l);
        Expr *e = new_expr(l, EX_BOOL, t.line, t.col);
        e->bval = t.kind == TOK_TRUE;
        return e;
    }
    if (at(l, TOK_STR)) {
        take(l);
        Expr *e = new_expr(l, EX_STRING, t.line, t.col);
        e->sval = t.sval;
        return e;
    }
    if (at(l, TOK_IDENT)) {
        Token id = take(l);
        if (!g_no_struct_lit && at(l, TOK_LBRACE)) {
            return parse_struct_lit(l, id.ident, id.line, id.col);
        }
        if (at(l, TOK_LPAREN)) {
            take(l);
            bool saved = g_no_struct_lit;
            g_no_struct_lit = false;
            Expr **args = NULL;
            int nargs = 0, cap = 0;
            if (!at(l, TOK_RPAREN)) {
                for (;;) {
                    ptrlist_push((void ***)&args, &nargs, &cap, parse_expr(l));
                    if (!at(l, TOK_COMMA)) {
                        break;
                    }
                    take(l);
                }
            }
            g_no_struct_lit = saved;
            expect(l, TOK_RPAREN, "')'");
            Expr *c = new_expr(l, EX_CALL, id.line, id.col);
            c->call.name = id.ident;
            c->call.args = args;
            c->call.nargs = nargs;
            return c;
        }
        Expr *e = new_expr(l, EX_VAR, id.line, id.col);
        e->var.name = id.ident;
        e->var.sym = -1;
        return e;
    }
    if (at(l, TOK_LPAREN)) {
        take(l);
        bool saved = g_no_struct_lit;
        g_no_struct_lit = false;
        Expr *e = parse_expr(l);
        g_no_struct_lit = saved;
        expect(l, TOK_RPAREN, "')'");
        return e;
    }
    comp_error(l->c, t.line, t.col, "expected expression");
    take(l);
    return new_expr(l, EX_INT, t.line, t.col);
}

static Expr *parse_unary(Lexer *l) {
    if (at(l, TOK_NOT) || at(l, TOK_MINUS)) {
        Token op = take(l);
        Expr *e = new_expr(l, EX_UNARY, op.line, op.col);
        e->un.op = op.kind;
        e->un.e = parse_unary(l);
        return e;
    }
    Expr *e = parse_primary(l);
    for (;;) {
        if (at(l, TOK_DOT)) {
            take(l);
            Token f = expect(l, TOK_IDENT, "field name");
            Expr *n = new_expr(l, EX_FIELD, f.line, f.col);
            n->field.base = e;
            n->field.field = f.ident;
            e = n;
            continue;
        }
        if (at(l, TOK_LBRACKET)) {
            Token lb = take(l);
            bool saved = g_no_struct_lit;
            g_no_struct_lit = false;
            Expr *first = NULL;
            if (!at(l, TOK_COLON)) {
                first = parse_expr(l);
            }
            if (at(l, TOK_COLON)) {
                take(l);
                Expr *hi = NULL;
                if (!at(l, TOK_RBRACKET)) {
                    hi = parse_expr(l);
                }
                g_no_struct_lit = saved;
                expect(l, TOK_RBRACKET, "']'");
                Expr *n = new_expr(l, EX_SLICE, lb.line, lb.col);
                n->slice.base = e;
                n->slice.lo = first;
                n->slice.hi = hi;
                e = n;
                continue;
            }
            g_no_struct_lit = saved;
            expect(l, TOK_RBRACKET, "']'");
            Expr *n = new_expr(l, EX_INDEX, lb.line, lb.col);
            n->index.base = e;
            n->index.idx = first;
            e = n;
            continue;
        }
        break;
    }
    return e;
}

static Expr *parse_bin(Lexer *l, int prec) {
    Expr *left = (prec == 6) ? parse_unary(l) : parse_bin(l, prec + 1);
    for (;;) {
        TokKind k = l->tok.kind;
        int p = 0;
        if (k == TOK_OROR) {
            p = 1;
        } else if (k == TOK_ANDAND) {
            p = 2;
        } else if (k == TOK_EQEQ || k == TOK_NEQ) {
            p = 3;
        } else if (k == TOK_LT || k == TOK_GT || k == TOK_LE || k == TOK_GE) {
            p = 4;
        } else if (k == TOK_PLUS || k == TOK_MINUS) {
            p = 5;
        } else if (k == TOK_STAR || k == TOK_SLASH || k == TOK_PERCENT) {
            p = 6;
        }
        if (p != prec) {
            break;
        }
        Token op = take(l);
        Expr *right = (prec == 6) ? parse_unary(l) : parse_bin(l, prec + 1);
        Expr *n = new_expr(l, EX_BIN, op.line, op.col);
        n->bin.op = op.kind;
        n->bin.l = left;
        n->bin.r = right;
        left = n;
    }
    return left;
}

static Expr *parse_expr(Lexer *l) {
    return parse_bin(l, 1);
}

static bool is_place(Expr *e) {
    if (e->kind == EX_VAR) {
        return true;
    }
    if (e->kind == EX_FIELD) {
        return is_place(e->field.base);
    }
    if (e->kind == EX_INDEX) {
        return true; /* typechecker rejects string element stores */
    }
    return false;
}

static Stmt *parse_if(Lexer *l) {
    Token ift = expect(l, TOK_IF, "'if'");
    bool saved = g_no_struct_lit;
    g_no_struct_lit = true;
    Expr *cond = parse_expr(l);
    g_no_struct_lit = saved;
    Stmt *then_b = parse_block(l);
    Stmt *else_b = NULL;
    if (at(l, TOK_ELSE)) {
        take(l);
        if (at(l, TOK_IF)) {
            else_b = parse_if(l);
        } else {
            else_b = parse_block(l);
        }
    }
    Stmt *s = new_stmt(l, ST_IF, ift.line, ift.col);
    s->ifs.cond = cond;
    s->ifs.then_b = then_b;
    s->ifs.else_b = else_b;
    return s;
}

static Stmt *parse_block(Lexer *l) {
    Token b = expect(l, TOK_LBRACE, "'{'");
    Stmt **stmts = NULL;
    int n = 0, cap = 0;
    while (!at(l, TOK_RBRACE) && !at(l, TOK_EOF)) {
        ptrlist_push((void ***)&stmts, &n, &cap, parse_stmt(l));
    }
    expect(l, TOK_RBRACE, "'}'");
    Stmt *s = new_stmt(l, ST_BLOCK, b.line, b.col);
    s->block.stmts = stmts;
    s->block.n = n;
    return s;
}

static Stmt *parse_stmt(Lexer *l) {
    if (at(l, TOK_VAR)) {
        Token v = take(l);
        Token name = expect(l, TOK_IDENT, "variable name");
        bool has_ann = false;
        Type ann = type_unit();
        if (at(l, TOK_COLON)) {
            take(l);
            ann = parse_type(l);
            has_ann = true;
        }
        expect(l, TOK_EQ, "'='");
        Expr *init = parse_expr(l);
        expect(l, TOK_SEMI, "';'");
        Stmt *s = new_stmt(l, ST_VAR, v.line, v.col);
        s->var.name = name.ident;
        s->var.sym = -1;
        s->var.init = init;
        s->var.has_ann = has_ann;
        s->var.ann = ann;
        return s;
    }
    if (at(l, TOK_IF)) {
        return parse_if(l);
    }
    if (at(l, TOK_WHILE)) {
        Token w = take(l);
        bool saved_cond = g_no_struct_lit;
        g_no_struct_lit = true;
        Expr *cond = parse_expr(l);
        g_no_struct_lit = saved_cond;
        Stmt *body = parse_block(l);
        Stmt *s = new_stmt(l, ST_WHILE, w.line, w.col);
        s->wh.cond = cond;
        s->wh.body = body;
        return s;
    }
    if (at(l, TOK_RETURN)) {
        Token r = take(l);
        Expr *val = NULL;
        if (!at(l, TOK_SEMI)) {
            val = parse_expr(l);
        }
        expect(l, TOK_SEMI, "';'");
        Stmt *s = new_stmt(l, ST_RETURN, r.line, r.col);
        s->ret.value = val;
        return s;
    }
    if (at(l, TOK_LBRACE)) {
        return parse_block(l);
    }
    Expr *e = parse_expr(l);
    if (at(l, TOK_EQ)) {
        if (!is_place(e)) {
            comp_error(l->c, e->line, e->col, "invalid assignment target");
        }
        take(l);
        Expr *val = parse_expr(l);
        expect(l, TOK_SEMI, "';'");
        Stmt *s = new_stmt(l, ST_ASSIGN, e->line, e->col);
        s->assign.place = e;
        s->assign.value = val;
        return s;
    }
    expect(l, TOK_SEMI, "';'");
    Stmt *s = new_stmt(l, ST_EXPR, e->line, e->col);
    s->expr = e;
    return s;
}

static StructDef *parse_struct(Lexer *l) {
    Token st = expect(l, TOK_STRUCT, "'struct'");
    Token name = expect(l, TOK_IDENT, "struct name");
    expect(l, TOK_LBRACE, "'{'");
    Field *fields = NULL;
    int n = 0, cap = 0;
    while (!at(l, TOK_RBRACE) && !at(l, TOK_EOF)) {
        Token fn = expect(l, TOK_IDENT, "field name");
        expect(l, TOK_COLON, "':'");
        Type ty = parse_type(l);
        if (at(l, TOK_COMMA)) {
            take(l);
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 4;
            fields = xrealloc(fields, (size_t)cap * sizeof(Field));
        }
        fields[n].name = fn.ident;
        fields[n].type = ty;
        n++;
        if (at(l, TOK_RBRACE)) {
            break;
        }
    }
    expect(l, TOK_RBRACE, "'}'");
    StructDef *d = arena_alloc(&l->c->arena, sizeof(StructDef));
    d->name = name.ident;
    d->fields = fields;
    d->nfields = n;
    d->line = st.line;
    d->col = st.col;
    return d;
}

static FnDef *parse_fn(Lexer *l, bool is_extern) {
    Token fn = expect(l, TOK_FN, "'fn'");
    Token name = expect(l, TOK_IDENT, "function name");
    expect(l, TOK_LPAREN, "'('");
    Param *params = NULL;
    int n = 0, cap = 0;
    bool is_vararg = false;
    if (!at(l, TOK_RPAREN)) {
        for (;;) {
            if (at(l, TOK_ELLIPSIS)) {
                take(l);
                is_vararg = true;
                break;
            }
            Token pn = expect(l, TOK_IDENT, "parameter name");
            expect(l, TOK_COLON, "':'");
            Type ty = parse_type(l);
            if (n == cap) {
                cap = cap ? cap * 2 : 4;
                params = xrealloc(params, (size_t)cap * sizeof(Param));
            }
            params[n].name = pn.ident;
            params[n].type = ty;
            n++;
            if (!at(l, TOK_COMMA)) {
                break;
            }
            take(l);
        }
    }
    expect(l, TOK_RPAREN, "')'");
    Type ret = type_unit();
    if (at(l, TOK_ARROW)) {
        take(l);
        ret = parse_type(l);
    }
    Stmt *body = NULL;
    if (is_extern) {
        expect(l, TOK_SEMI, "';'");
    } else {
        if (is_vararg) {
            comp_error(l->c, fn.line, fn.col, "'...' is only allowed on extern fn");
        }
        body = parse_block(l);
    }
    FnDef *f = arena_alloc(&l->c->arena, sizeof(FnDef));
    f->name = name.ident;
    f->params = params;
    f->nparams = n;
    f->ret = ret;
    f->body = body;
    f->line = fn.line;
    f->col = fn.col;
    f->is_main = !is_extern && strcmp(name.ident, "main") == 0;
    f->is_extern = is_extern;
    f->is_vararg = is_vararg;
    return f;
}

static void add_include(Comp *c, const char *header) {
    Program *p = &c->prog;
    for (int i = 0; i < p->nincludes; i++) {
        if (strcmp(p->includes[i], header) == 0) {
            return;
        }
    }
    ptrlist_push((void ***)&p->includes, &p->nincludes, &p->capincludes, (void *)header);
}

Program *parse_program(Lexer *l) {
    Program *p = &l->c->prog;
    while (!at(l, TOK_EOF)) {
        if (at(l, TOK_STRUCT)) {
            StructDef *s = parse_struct(l);
            ptrlist_push((void ***)&p->structs, &p->nstructs, &p->capstructs, s);
        } else if (at(l, TOK_FN)) {
            FnDef *f = parse_fn(l, false);
            ptrlist_push((void ***)&p->fns, &p->nfns, &p->capfns, f);
        } else if (at(l, TOK_EXTERN)) {
            Token ex = take(l);
            FnDef *f = parse_fn(l, true);
            f->line = ex.line;
            f->col = ex.col;
            ptrlist_push((void ***)&p->fns, &p->nfns, &p->capfns, f);
        } else if (at(l, TOK_IMPORT)) {
            take(l);
            Token s = expect(l, TOK_STR, "module file name");
            expect(l, TOK_SEMI, "';'");
            const char *path = s.sval;
            ptrlist_push((void ***)&l->c->imports, &l->c->nimports, &l->c->capimports,
                         (void *)path);
        } else if (at(l, TOK_INCLUDE)) {
            take(l);
            Token s = expect(l, TOK_STR, "C header name");
            expect(l, TOK_SEMI, "';'");
            add_include(l->c, s.sval);
        } else {
            comp_error(l->c, l->tok.line, l->tok.col,
                       "expected struct, fn, extern fn, import or include");
            take(l);
            if (at(l, TOK_EOF)) {
                break;
            }
        }
    }
    return p;
}
