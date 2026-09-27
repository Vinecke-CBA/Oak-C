#include "oak.h"

static FnDef *g_cur_fn;

static int find_struct(Comp *c, const char *name) {
    for (int i = 0; i < c->prog.nstructs; i++) {
        if (c->prog.structs[i]->name == name) {
            return i;
        }
    }
    return -1;
}

static FnDef *find_fn(Comp *c, const char *name) {
    for (int i = 0; i < c->prog.nfns; i++) {
        if (c->prog.fns[i]->name == name) {
            return c->prog.fns[i];
        }
    }
    return NULL;
}

static Type resolve_type(Comp *c, Type t, int line, int col) {
    if (t.kind == TY_ARRAY) {
        *t.elem = resolve_type(c, *t.elem, line, col);
        return t;
    }
    const char *n = unresolved_type_name(t);
    if (!n) {
        if (t.kind == TY_STRUCT && t.struct_id >= 0) {
            return t;
        }
        return t;
    }
    int id = find_struct(c, n);
    if (id < 0) {
        comp_error(c, line, col, "unknown type '%s'", n);
        return type_unit();
    }
    return type_struct(id);
}

static bool is_num(Type t) {
    return t.kind == TY_I32 || t.kind == TY_F64;
}

static bool is_scalarish(Type t) {
    return t.kind == TY_I32 || t.kind == TY_F64 || t.kind == TY_BOOL || t.kind == TY_STRING ||
           t.kind == TY_PTR || t.kind == TY_RAW;
}

/* An i32 literal is accepted wherever an f64 is expected; C does the
   conversion for us. */
static bool coerce_int_literal(Expr *e, Type want) {
    if (want.kind == TY_F64 && e->kind == EX_INT) {
        e->type = type_f64();
        return true;
    }
    return false;
}

static int add_sym(Comp *c, const char *name, Type type, bool is_struct_param, int line, int col) {
    for (int i = c->nsyms - 1; i >= c->func_sym_base; i--) {
        if (c->syms[i].depth > c->scope) {
            continue;
        }
        if (c->syms[i].depth < c->scope) {
            break;
        }
        if (c->syms[i].name == name) {
            comp_error(c, line, col, "duplicate name '%s'", name);
            break;
        }
    }
    if (c->nsyms == c->capsyms) {
        c->capsyms = c->capsyms ? c->capsyms * 2 : 32;
        c->syms = xrealloc(c->syms, (size_t)c->capsyms * sizeof(Sym));
    }
    int id = c->nsyms;
    c->syms[id].name = name;
    c->syms[id].type = type;
    c->syms[id].depth = c->scope;
    c->syms[id].is_struct_param = is_struct_param;
    c->nsyms++;
    return id;
}

static int lookup_sym(Comp *c, const char *name) {
    for (int i = c->nsyms - 1; i >= c->func_sym_base; i--) {
        if (c->syms[i].depth > c->scope) {
            continue;
        }
        if (c->syms[i].name == name) {
            return i;
        }
    }
    return -1;
}

static void check_expr(Comp *c, Expr *e);
static void check_stmt(Comp *c, Stmt *s);

static bool is_place(Expr *e) {
    if (e->kind == EX_VAR) {
        return true;
    }
    if (e->kind == EX_FIELD) {
        return is_place(e->field.base);
    }
    if (e->kind == EX_INDEX) {
        /* Array elements are writable; string characters are not. */
        return e->index.base && e->index.base->type.kind == TY_ARRAY;
    }
    return false;
}

static void check_expr(Comp *c, Expr *e) {
    e->root_sym = -1;
    switch (e->kind) {
    case EX_INT:
        e->type = type_i32();
        break;
    case EX_FLOAT:
        e->type = type_f64();
        break;
    case EX_NULL:
        e->type = type_ptr();
        break;
    case EX_BOOL:
        e->type = type_bool();
        break;
    case EX_STRING:
        e->type = type_string();
        break;
    case EX_ARRAY: {
        Type expected = e->type; /* a `var a: T[] = ...` annotation may have set this */
        bool have_expected = expected.kind == TY_ARRAY;
        Type elem = have_expected ? *expected.elem : type_unit();
        bool have_elem = have_expected;
        for (int i = 0; i < e->arr.n; i++) {
            Expr *el = e->arr.elems[i];
            check_expr(c, el);
            if (el->type.kind == TY_UNIT) {
                comp_error(c, el->line, el->col, "cannot store () in an array");
            }
            if (!have_elem) {
                elem = el->type;
                have_elem = true;
                continue;
            }
            if (!type_eq(el->type, elem) && !coerce_int_literal(el, elem)) {
                comp_error(c, el->line, el->col, "array elements must all be %s, got %s",
                           type_name(c, elem), type_name(c, el->type));
            }
        }
        if (!have_elem) {
            comp_error(c, e->line, e->col,
                       "cannot infer the element type of an empty array literal "
                       "(write e.g. var a: i32[] = [];)");
            elem = type_i32();
        }
        if (!have_expected) {
            Type *et = arena_alloc(&c->arena, sizeof(Type));
            *et = elem;
            e->type = type_array(et);
        }
        break;
    }
    case EX_INDEX: {
        check_expr(c, e->index.base);
        check_expr(c, e->index.idx);
        e->root_sym = e->index.base->root_sym;
        if (!type_eq(e->index.idx->type, type_i32())) {
            comp_error(c, e->index.idx->line, e->index.idx->col, "index must be i32, got %s",
                       type_name(c, e->index.idx->type));
        }
        Type bt = e->index.base->type;
        if (bt.kind == TY_ARRAY) {
            e->type = *bt.elem;
        } else if (bt.kind == TY_STRING) {
            e->type = type_string();
        } else {
            comp_error(c, e->line, e->col, "cannot index %s", type_name(c, bt));
            e->type = type_unit();
        }
        break;
    }
    case EX_SLICE: {
        check_expr(c, e->slice.base);
        if (e->slice.lo) {
            check_expr(c, e->slice.lo);
            if (!type_eq(e->slice.lo->type, type_i32())) {
                comp_error(c, e->slice.lo->line, e->slice.lo->col, "slice bound must be i32, got %s",
                           type_name(c, e->slice.lo->type));
            }
        }
        if (e->slice.hi) {
            check_expr(c, e->slice.hi);
            if (!type_eq(e->slice.hi->type, type_i32())) {
                comp_error(c, e->slice.hi->line, e->slice.hi->col, "slice bound must be i32, got %s",
                           type_name(c, e->slice.hi->type));
            }
        }
        if (e->slice.base->type.kind != TY_STRING) {
            comp_error(c, e->line, e->col, "slicing is only supported on strings, got %s",
                       type_name(c, e->slice.base->type));
            e->type = type_unit();
        } else {
            e->type = type_string();
        }
        break;
    }
    case EX_VAR: {
        int id = lookup_sym(c, e->var.name);
        if (id < 0) {
            comp_error(c, e->line, e->col, "unknown name '%s'", e->var.name);
            e->type = type_unit();
            e->var.sym = -1;
            break;
        }
        e->var.sym = id;
        e->type = c->syms[id].type;
        e->root_sym = id;
        break;
    }
    case EX_FIELD: {
        check_expr(c, e->field.base);
        e->root_sym = e->field.base->root_sym;
        if (!type_is_struct(e->field.base->type)) {
            if (c->errors == 0 || e->field.base->type.kind != TY_UNIT) {
                comp_error(c, e->line, e->col, "field access on non-struct");
            }
            e->type = type_unit();
            break;
        }
        StructDef *st = c->prog.structs[e->field.base->type.struct_id];
        Field *f = NULL;
        for (int i = 0; i < st->nfields; i++) {
            if (st->fields[i].name == e->field.field) {
                f = &st->fields[i];
                break;
            }
        }
        if (!f) {
            comp_error(c, e->line, e->col, "no field '%s' on %s", e->field.field, st->name);
            e->type = type_unit();
            break;
        }
        e->type = f->type;
        break;
    }
    case EX_STRUCT: {
        int id = find_struct(c, e->slit.name);
        if (id < 0) {
            comp_error(c, e->line, e->col, "unknown struct '%s'", e->slit.name);
            e->type = type_unit();
            break;
        }
        e->slit.struct_id = id;
        e->type = type_struct(id);
        StructDef *st = c->prog.structs[id];
        bool *seen = xmalloc((size_t)st->nfields);
        memset(seen, 0, (size_t)st->nfields);
        for (int i = 0; i < e->slit.nfields; i++) {
            check_expr(c, e->slit.fvals[i]);
            int fi = -1;
            for (int j = 0; j < st->nfields; j++) {
                if (st->fields[j].name == e->slit.fnames[i]) {
                    fi = j;
                    break;
                }
            }
            if (fi < 0) {
                comp_error(c, e->slit.fvals[i]->line, e->slit.fvals[i]->col,
                           "no field '%s' on %s", e->slit.fnames[i], st->name);
                continue;
            }
            if (seen[fi]) {
                comp_error(c, e->slit.fvals[i]->line, e->slit.fvals[i]->col,
                           "duplicate field '%s'", e->slit.fnames[i]);
            }
            seen[fi] = true;
            if (!type_eq(e->slit.fvals[i]->type, st->fields[fi].type) &&
                !coerce_int_literal(e->slit.fvals[i], st->fields[fi].type)) {
                comp_error(c, e->slit.fvals[i]->line, e->slit.fvals[i]->col,
                           "type mismatch for field '%s': expected %s, got %s",
                           e->slit.fnames[i], type_name(c, st->fields[fi].type),
                           type_name(c, e->slit.fvals[i]->type));
            }
        }
        for (int j = 0; j < st->nfields; j++) {
            if (!seen[j]) {
                comp_error(c, e->line, e->col, "missing field '%s' in %s literal",
                           st->fields[j].name, st->name);
            }
        }
        free(seen);
        break;
    }
    case EX_CALL: {
        if (strcmp(e->call.name, "print") == 0) {
            if (e->call.nargs != 1) {
                comp_error(c, e->line, e->col, "print takes exactly one argument");
            } else {
                check_expr(c, e->call.args[0]);
                Type at = e->call.args[0]->type;
                if (!is_scalarish(at)) {
                    comp_error(c, e->call.args[0]->line, e->call.args[0]->col,
                               "print cannot print %s", type_name(c, at));
                }
            }
            e->type = type_unit();
            break;
        }
        if (strcmp(e->call.name, "len") == 0) {
            if (e->call.nargs != 1) {
                comp_error(c, e->line, e->col, "len takes exactly one argument");
            } else {
                check_expr(c, e->call.args[0]);
                Type at = e->call.args[0]->type;
                if (at.kind != TY_STRING && at.kind != TY_ARRAY) {
                    comp_error(c, e->call.args[0]->line, e->call.args[0]->col,
                               "len expects a string or an array, got %s", type_name(c, at));
                }
            }
            e->type = type_i32();
            break;
        }
        if (strcmp(e->call.name, "push") == 0) {
            if (e->call.nargs != 2) {
                comp_error(c, e->line, e->col, "push takes an array and a value");
            } else {
                check_expr(c, e->call.args[0]);
                check_expr(c, e->call.args[1]);
                Type at = e->call.args[0]->type;
                Type vt = e->call.args[1]->type;
                if (at.kind != TY_ARRAY) {
                    comp_error(c, e->call.args[0]->line, e->call.args[0]->col,
                               "push expects an array, got %s", type_name(c, at));
                } else {
                    if (vt.kind == TY_UNIT) {
                        comp_error(c, e->call.args[1]->line, e->call.args[1]->col,
                                   "cannot push ()");
                    } else if (!type_eq(vt, *at.elem) && !coerce_int_literal(e->call.args[1], *at.elem)) {
                        comp_error(c, e->call.args[1]->line, e->call.args[1]->col,
                                   "cannot push %s into %s", type_name(c, vt),
                                   type_name(c, at));
                    }
                }
            }
            e->type = type_unit();
            break;
        }
        FnDef *fn = find_fn(c, e->call.name);
        if (!fn) {
            comp_error(c, e->line, e->col, "unknown function '%s'", e->call.name);
            e->type = type_unit();
            for (int i = 0; i < e->call.nargs; i++) {
                check_expr(c, e->call.args[i]);
            }
            break;
        }
        bool too_few = e->call.nargs < fn->nparams;
        bool too_many = !fn->is_vararg && e->call.nargs > fn->nparams;
        if (too_few || too_many) {
            if (fn->is_vararg) {
                comp_error(c, e->line, e->col, "'%s' expects at least %d argument(s), got %d",
                           fn->name, fn->nparams, e->call.nargs);
            } else {
                comp_error(c, e->line, e->col, "'%s' expects %d argument(s), got %d", fn->name,
                           fn->nparams, e->call.nargs);
            }
        }
        int n = e->call.nargs < fn->nparams ? e->call.nargs : fn->nparams;
        for (int i = 0; i < e->call.nargs; i++) {
            check_expr(c, e->call.args[i]);
            if (i < n) {
                if (!type_eq(e->call.args[i]->type, fn->params[i].type) &&
                    !coerce_int_literal(e->call.args[i], fn->params[i].type)) {
                    comp_error(c, e->call.args[i]->line, e->call.args[i]->col,
                               "argument type mismatch: expected %s, got %s",
                               type_name(c, fn->params[i].type),
                               type_name(c, e->call.args[i]->type));
                }
            } else if (!is_scalarish(e->call.args[i]->type)) {
                comp_error(c, e->call.args[i]->line, e->call.args[i]->col,
                           "these extra arguments must be scalars, got %s",
                           type_name(c, e->call.args[i]->type));
            }
        }
        e->type = fn->ret;
        break;
    }
    case EX_BIN: {
        check_expr(c, e->bin.l);
        check_expr(c, e->bin.r);
        TokKind op = e->bin.op;
        if (op == TOK_ANDAND || op == TOK_OROR) {
            if (!type_eq(e->bin.l->type, type_bool()) || !type_eq(e->bin.r->type, type_bool())) {
                comp_error(c, e->line, e->col, "&& and || require bool");
            }
            e->type = type_bool();
            break;
        }
        if (op == TOK_EQEQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT || op == TOK_LE ||
            op == TOK_GE) {
            Type lt = e->bin.l->type;
            Type rt = e->bin.r->type;
            bool ordered = op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
            if (ordered) {
                bool nums = is_num(lt) && is_num(rt);
                bool strs = lt.kind == TY_STRING && rt.kind == TY_STRING;
                if (!nums && !strs) {
                    comp_error(c, e->line, e->col, "cannot order %s and %s", type_name(c, lt),
                               type_name(c, rt));
                }
            } else if (is_num(lt) && is_num(rt)) {
                /* i32/f64 compare fine, C promotes */
            } else if (!type_eq(lt, rt)) {
                comp_error(c, e->line, e->col, "cannot compare %s and %s", type_name(c, lt),
                           type_name(c, rt));
            } else if (lt.kind == TY_UNIT || lt.kind == TY_ARRAY || lt.kind == TY_RAW) {
                comp_error(c, e->line, e->col, "cannot compare %s values", type_name(c, lt));
            }
            e->type = type_bool();
            break;
        }
        if (op == TOK_PLUS && type_eq(e->bin.l->type, type_string()) &&
            type_eq(e->bin.r->type, type_string())) {
            e->type = type_string();
            break;
        }
        if (op == TOK_PERCENT) {
            if (!type_eq(e->bin.l->type, type_i32()) || !type_eq(e->bin.r->type, type_i32())) {
                comp_error(c, e->line, e->col, "'%%' requires i32");
            }
            e->type = type_i32();
            break;
        }
        if (op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR || op == TOK_SLASH) {
            if (!is_num(e->bin.l->type) || !is_num(e->bin.r->type)) {
                comp_error(c, e->line, e->col, "arithmetic needs i32 or f64, got %s and %s",
                           type_name(c, e->bin.l->type), type_name(c, e->bin.r->type));
                e->type = type_i32();
                break;
            }
            e->type = (e->bin.l->type.kind == TY_F64 || e->bin.r->type.kind == TY_F64)
                          ? type_f64()
                          : type_i32();
            break;
        }
        comp_error(c, e->line, e->col, "unsupported operator");
        e->type = type_i32();
        break;
    }
    case EX_UNARY: {
        check_expr(c, e->un.e);
        if (e->un.op == TOK_NOT) {
            if (!type_eq(e->un.e->type, type_bool())) {
                comp_error(c, e->line, e->col, "! requires bool");
            }
            e->type = type_bool();
        } else {
            if (!is_num(e->un.e->type)) {
                comp_error(c, e->line, e->col, "unary - requires i32 or f64");
                e->type = type_i32();
            } else {
                e->type = e->un.e->type;
            }
        }
        break;
    }
    }
}

static void check_stmt(Comp *c, Stmt *s) {
    switch (s->kind) {
    case ST_VAR: {
        bool has_ann = s->var.has_ann;
        Type want = type_unit();
        if (has_ann) {
            s->var.ann = resolve_type(c, s->var.ann, s->line, s->col);
            want = s->var.ann;
            /* An array literal (notably the empty one) takes its element type
               from the annotation. */
            if (s->var.init->kind == EX_ARRAY) {
                s->var.init->type = want;
            }
        }
        check_expr(c, s->var.init);
        Type t = s->var.init->type;
        if (has_ann) {
            if (!type_eq(t, want) && !coerce_int_literal(s->var.init, want)) {
                comp_error(c, s->line, s->col, "variable '%s' is %s but initialised with %s",
                           s->var.name, type_name(c, want), type_name(c, t));
            }
            t = want;
        }
        if (t.kind == TY_UNIT) {
            comp_error(c, s->line, s->col, "cannot store () in a variable");
        }
        s->var.sym = add_sym(c, s->var.name, t, false, s->line, s->col);
        break;
    }
    case ST_ASSIGN:
        check_expr(c, s->assign.place);
        check_expr(c, s->assign.value);
        if (!is_place(s->assign.place)) {
            comp_error(c, s->line, s->col, "invalid assignment target");
        }
        if (!type_eq(s->assign.place->type, s->assign.value->type) &&
            !coerce_int_literal(s->assign.value, s->assign.place->type)) {
            comp_error(c, s->line, s->col, "type mismatch in assignment: %s vs %s",
                       type_name(c, s->assign.place->type),
                       type_name(c, s->assign.value->type));
        }
        break;
    case ST_IF:
        check_expr(c, s->ifs.cond);
        if (!type_eq(s->ifs.cond->type, type_bool())) {
            comp_error(c, s->ifs.cond->line, s->ifs.cond->col, "if condition must be bool");
        }
        check_stmt(c, s->ifs.then_b);
        if (s->ifs.else_b) {
            check_stmt(c, s->ifs.else_b);
        }
        break;
    case ST_WHILE:
        check_expr(c, s->wh.cond);
        if (!type_eq(s->wh.cond->type, type_bool())) {
            comp_error(c, s->wh.cond->line, s->wh.cond->col, "while condition must be bool");
        }
        check_stmt(c, s->wh.body);
        break;
    case ST_RETURN:
        if (!g_cur_fn) {
            break;
        }
        if (s->ret.value) {
            check_expr(c, s->ret.value);
            if (!type_eq(s->ret.value->type, g_cur_fn->ret) &&
                !coerce_int_literal(s->ret.value, g_cur_fn->ret)) {
                comp_error(c, s->line, s->col, "return type mismatch: expected %s, got %s",
                           type_name(c, g_cur_fn->ret), type_name(c, s->ret.value->type));
            }
        } else if (g_cur_fn->ret.kind != TY_UNIT) {
            comp_error(c, s->line, s->col, "function must return %s", type_name(c, g_cur_fn->ret));
        }
        break;
    case ST_EXPR:
        check_expr(c, s->expr);
        break;
    case ST_BLOCK: {
        int saved = c->scope;
        c->scope++;
        for (int i = 0; i < s->block.n; i++) {
            check_stmt(c, s->block.stmts[i]);
        }
        c->scope = saved;
        break;
    }
    }
}

static bool stmt_has_return(Stmt *s) {
    if (!s) {
        return false;
    }
    switch (s->kind) {
    case ST_RETURN:
        return true;
    case ST_IF:
        return stmt_has_return(s->ifs.then_b) ||
               (s->ifs.else_b && stmt_has_return(s->ifs.else_b));
    case ST_WHILE:
        return stmt_has_return(s->wh.body);
    case ST_BLOCK:
        for (int i = 0; i < s->block.n; i++) {
            if (stmt_has_return(s->block.stmts[i])) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static int struct_state(Comp *c, int id, char *state) {
    if (state[id] == 1) {
        return 1; /* cycle */
    }
    if (state[id] == 2) {
        return 0;
    }
    state[id] = 1;
    StructDef *st = c->prog.structs[id];
    for (int i = 0; i < st->nfields; i++) {
        if (type_is_struct(st->fields[i].type)) {
            if (struct_state(c, st->fields[i].type.struct_id, state)) {
                return 1;
            }
        }
    }
    state[id] = 2;
    return 0;
}

void typecheck(Comp *c) {
    for (int i = 0; i < c->prog.nstructs; i++) {
        for (int j = 0; j < i; j++) {
            if (c->prog.structs[i]->name == c->prog.structs[j]->name) {
                comp_error(c, c->prog.structs[i]->line, c->prog.structs[i]->col,
                           "duplicate struct '%s'", c->prog.structs[i]->name);
            }
        }
    }
    for (int i = 0; i < c->prog.nstructs; i++) {
        StructDef *st = c->prog.structs[i];
        for (int f = 0; f < st->nfields; f++) {
            st->fields[f].type = resolve_type(c, st->fields[f].type, st->line, st->col);
            for (int g = 0; g < f; g++) {
                if (st->fields[f].name == st->fields[g].name) {
                    comp_error(c, st->line, st->col, "duplicate field '%s'", st->fields[f].name);
                }
            }
        }
    }
    if (c->prog.nstructs > 0) {
        char *state = xmalloc((size_t)c->prog.nstructs);
        memset(state, 0, (size_t)c->prog.nstructs);
        for (int i = 0; i < c->prog.nstructs; i++) {
            if (struct_state(c, i, state)) {
                comp_error(c, c->prog.structs[i]->line, c->prog.structs[i]->col,
                           "recursive struct '%s'", c->prog.structs[i]->name);
                break;
            }
        }
        free(state);
    }

    for (int i = 0; i < c->prog.nfns; i++) {
        FnDef *fn = c->prog.fns[i];
        if (strcmp(fn->name, "print") == 0 || strcmp(fn->name, "len") == 0 ||
            strcmp(fn->name, "push") == 0) {
            comp_error(c, fn->line, fn->col, "'%s' is a builtin", fn->name);
        }
        for (int j = 0; j < i; j++) {
            if (c->prog.fns[j]->name == fn->name) {
                comp_error(c, fn->line, fn->col, "duplicate function '%s'", fn->name);
            }
        }
        for (int p = 0; p < fn->nparams; p++) {
            fn->params[p].type = resolve_type(c, fn->params[p].type, fn->line, fn->col);
        }
        fn->ret = resolve_type(c, fn->ret, fn->line, fn->col);
        if (fn->is_extern) {
            for (int p = 0; p < fn->nparams; p++) {
                if (!is_scalarish(fn->params[p].type)) {
                    comp_error(c, fn->line, fn->col,
                               "extern fn parameters must be scalars, but '%s' is %s",
                               fn->params[p].name, type_name(c, fn->params[p].type));
                }
            }
            if (!is_scalarish(fn->ret) && fn->ret.kind != TY_UNIT) {
                comp_error(c, fn->line, fn->col, "extern fn cannot return %s",
                           type_name(c, fn->ret));
            }
            continue;
        }
        if (fn->ret.kind != TY_UNIT && !stmt_has_return(fn->body)) {
            comp_error(c, fn->line, fn->col, "function '%s' must return %s", fn->name,
                       type_name(c, fn->ret));
        }
        if (fn->is_main) {
            if (fn->nparams != 0) {
                comp_error(c, fn->line, fn->col, "main takes no arguments");
            }
            if (fn->ret.kind != TY_UNIT) {
                comp_error(c, fn->line, fn->col, "main cannot return a value");
            }
        }
    }

    bool has_main = false;
    for (int i = 0; i < c->prog.nfns; i++) {
        FnDef *fn = c->prog.fns[i];
        if (fn->is_extern) {
            continue;
        }
        if (fn->is_main) {
            has_main = true;
        }
        g_cur_fn = fn;
        c->func_sym_base = c->nsyms;
        c->scope = 1;
        for (int p = 0; p < fn->nparams; p++) {
            bool sp = type_is_struct(fn->params[p].type);
            add_sym(c, fn->params[p].name, fn->params[p].type, sp, fn->line, fn->col);
        }
        check_stmt(c, fn->body);
    }
    g_cur_fn = NULL;
    if (!has_main) {
        comp_error(c, 1, 1, "missing fn main()");
    }
}
