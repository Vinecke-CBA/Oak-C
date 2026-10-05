#include "oak.h"

static FnDef *find_fn(Comp *c, const char *name) {
    for (int i = 0; i < c->prog.nfns; i++) {
        if (c->prog.fns[i]->name == name) {
            return c->prog.fns[i];
        }
    }
    return NULL;
}

static const char *sym_name(Comp *c, int id) {
    if (id < 0 || id >= c->nsyms) {
        return "?";
    }
    return c->syms[id].name;
}

static void walk_expr(Comp *c, Expr *e);
static void walk_stmt(Comp *c, Stmt *s);

static void check_call_alias(Comp *c, Expr *e) {
    if (e->kind != EX_CALL) {
        return;
    }
    if (strcmp(e->call.name, "print") == 0) {
        for (int i = 0; i < e->call.nargs; i++) {
            walk_expr(c, e->call.args[i]);
        }
        return;
    }
    FnDef *fn = find_fn(c, e->call.name);
    int *used = NULL;
    int nused = 0, cap = 0;
    int n = e->call.nargs;
    if (fn && fn->nparams < n) {
        n = fn->nparams;
    }
    for (int i = 0; i < e->call.nargs; i++) {
        walk_expr(c, e->call.args[i]);
        if (!fn || i >= n) {
            continue;
        }
        if (!type_is_struct(fn->params[i].type)) {
            continue;
        }
        int root = e->call.args[i]->root_sym;
        if (root < 0) {
            continue;
        }
        for (int j = 0; j < nused; j++) {
            if (used[j] == root) {
                const char *nm = c->syms[root].name;
                comp_error(c, e->line, e->col,
                           "%s cannot be used more than once in the same call", nm);
                break;
            }
        }
        if (nused == cap) {
            cap = cap ? cap * 2 : 4;
            used = xrealloc(used, (size_t)cap * sizeof(int));
        }
        used[nused++] = root;
    }
    free(used);
    (void)sym_name;
}

static void walk_expr(Comp *c, Expr *e) {
    switch (e->kind) {
    case EX_INT:
    case EX_FLOAT:
    case EX_BOOL:
    case EX_NULL:
    case EX_STRING:
    case EX_VAR:
        break;
    case EX_FIELD:
        walk_expr(c, e->field.base);
        break;
    case EX_ARRAY:
        for (int i = 0; i < e->arr.n; i++) {
            walk_expr(c, e->arr.elems[i]);
        }
        break;
    case EX_INDEX:
        walk_expr(c, e->index.base);
        walk_expr(c, e->index.idx);
        break;
    case EX_SLICE:
        walk_expr(c, e->slice.base);
        if (e->slice.lo) {
            walk_expr(c, e->slice.lo);
        }
        if (e->slice.hi) {
            walk_expr(c, e->slice.hi);
        }
        break;
    case EX_STRUCT:
        for (int i = 0; i < e->slit.nfields; i++) {
            walk_expr(c, e->slit.fvals[i]);
        }
        break;
    case EX_CALL:
        check_call_alias(c, e);
        break;
    case EX_BIN:
        walk_expr(c, e->bin.l);
        walk_expr(c, e->bin.r);
        break;
    case EX_UNARY:
        walk_expr(c, e->un.e);
        break;
    case EX_ADDR:
        walk_expr(c, e->addr.e);
        break;
    case EX_DEREF:
        walk_expr(c, e->deref.p);
        break;
    }
}

static void walk_stmt(Comp *c, Stmt *s) {
    switch (s->kind) {
    case ST_VAR:
        walk_expr(c, s->var.init);
        break;
    case ST_ASSIGN:
        walk_expr(c, s->assign.place);
        walk_expr(c, s->assign.value);
        break;
    case ST_IF:
        walk_expr(c, s->ifs.cond);
        walk_stmt(c, s->ifs.then_b);
        if (s->ifs.else_b) {
            walk_stmt(c, s->ifs.else_b);
        }
        break;
    case ST_WHILE:
        walk_expr(c, s->wh.cond);
        walk_stmt(c, s->wh.body);
        break;
    case ST_FORIN:
        if (s->forin.is_range) {
            walk_expr(c, s->forin.iter);
            walk_expr(c, s->forin.hi);
        } else {
            walk_expr(c, s->forin.iter);
        }
        walk_stmt(c, s->forin.body);
        break;
    case ST_RETURN:
        if (s->ret.value) {
            walk_expr(c, s->ret.value);
        }
        break;
    case ST_EXPR:
        walk_expr(c, s->expr);
        break;
    case ST_BLOCK:
        for (int i = 0; i < s->block.n; i++) {
            walk_stmt(c, s->block.stmts[i]);
        }
        break;
    }
}

void aliascheck(Comp *c) {
    /* typecheck keeps one growing symbol table for the whole program instead
       of resetting nsyms per function, so the root_sym ids stored on
       expressions stay unique and can be compared directly here. */
    for (int i = 0; i < c->prog.nfns; i++) {
        if (c->prog.fns[i]->is_extern) {
            continue;
        }
        walk_stmt(c, c->prog.fns[i]->body);
    }
}
