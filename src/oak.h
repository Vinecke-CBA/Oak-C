#ifndef OAK_H
#define OAK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct Arena {
    char *mem;
    size_t used, cap;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t n);
char *arena_strndup(Arena *a, const char *s, size_t n);
void arena_free(Arena *a);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_FN,
    TOK_STRUCT,
    TOK_VAR,
    TOK_IF,
    TOK_ELSE,
    TOK_FOR,
    TOK_IN,
    TOK_WHILE,
    TOK_RETURN,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_I8,
    TOK_U8,
    TOK_I16,
    TOK_U16,
    TOK_I32,
    TOK_U32,
    TOK_I64,
    TOK_U64,
    TOK_F64,
    TOK_BOOL,
    TOK_STRTYPE,
    TOK_PTR,
    TOK_DEREF,
    TOK_EXTERN,
    TOK_IMPORT,
    TOK_INCLUDE,
    TOK_STR,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMI,
    TOK_DOT,
    TOK_DOTDOT,
    TOK_ELLIPSIS,
    TOK_EQ,
    TOK_ARROW,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQEQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_ANDAND,
    TOK_OROR,
    TOK_NOT
} TokKind;

typedef struct {
    TokKind kind;
    const char *ident;
    const char *sval;
    int64_t ival;
    uint64_t uval; /* integer literal, exact for anything up to 2^64-1 */
    double fval;
    int line, col;
} Token;

/* Integer kinds are kept contiguous (TY_I8 .. TY_U64) so type_is_int() and the
   numeric-conversion rules can walk them as a block. */
typedef enum {
    TY_I8,
    TY_U8,
    TY_I16,
    TY_U16,
    TY_I32,
    TY_U32,
    TY_I64,
    TY_U64,
    TY_F64,
    TY_BOOL,
    TY_UNIT,
    TY_STRUCT,
    TY_STRING,
    TY_PTR,
    TY_RAW,
    TY_ARRAY
} TypeKind;

typedef struct Type {
    TypeKind kind;
    int struct_id;
    struct Type *elem; /* TY_ARRAY element type, TY_PTR pointee (arena allocated) */
    const char *raw;   /* TY_RAW: C type text, emitted verbatim */
} Type;

typedef enum {
    EX_INT,
    EX_FLOAT,
    EX_BOOL,
    EX_NULL,
    EX_STRING,
    EX_VAR,
    EX_FIELD,
    EX_STRUCT,
    EX_CALL,
    EX_BIN,
    EX_UNARY,
    EX_ADDR,
    EX_DEREF,
    EX_ARRAY,
    EX_INDEX,
    EX_SLICE
} ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    int line, col;
    Type type;
    int root_sym;
    union {
        int64_t ival;
        uint64_t uval; /* literals above INT64_MAX live here, not in ival */
        double fval;
        bool bval;
        const char *sval;
        struct {
            const char *name;
            int sym;
        } var;
        struct {
            Expr *base;
            const char *field;
        } field;
        struct {
            const char *name;
            int struct_id;
            const char **fnames;
            Expr **fvals;
            int nfields;
        } slit;
        struct {
            const char *name;
            Expr **args;
            int nargs;
        } call;
        struct {
            TokKind op;
            Expr *l, *r;
        } bin;
        struct {
            TokKind op;
            Expr *e;
        } un;
        struct {
            Expr *e; /* ptr(x): address of a place expression */
        } addr;
        struct {
            Expr *p; /* defer(p): value stored at a pointer */
        } deref;
        struct {
            Expr **elems;
            int n;
        } arr;
        struct {
            Expr *base;
            Expr *idx;
        } index;
        struct {
            Expr *base;
            Expr *lo; /* NULL means from the start */
            Expr *hi; /* NULL means to the end */
        } slice;
    };
};

typedef enum {
    ST_VAR,
    ST_ASSIGN,
    ST_IF,
    ST_WHILE,
    ST_FORIN,
    ST_RETURN,
    ST_EXPR,
    ST_BLOCK
} StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    int line, col;
    union {
        struct {
            const char *name;
            int sym;
            Expr *init;
            bool has_ann;
            Type ann;
        } var;
        struct {
            Expr *place;
            Expr *value;
        } assign;
        struct {
            Expr *cond;
            Stmt *then_b;
            Stmt *else_b;
        } ifs;
        struct {
            Expr *cond;
            Stmt *body;
        } wh;
        struct {
            const char **names; /* binding pattern: 1 name, or N field names */
            int nnames;
            Expr *iter;  /* array expr, or range lo when is_range */
            Expr *hi;    /* range end (is_range only), NULL for arrays */
            Stmt *body;
            bool is_range;
        } forin;
        struct {
            Expr *value;
        } ret;
        Expr *expr;
        struct {
            Stmt **stmts;
            int n;
        } block;
    };
};

typedef struct {
    const char *name;
    Type type;
} Field;

typedef struct {
    const char *name;
    Field *fields;
    int nfields;
    int line, col;
} StructDef;

typedef struct {
    const char *name;
    Type type;
} Param;

typedef struct {
    const char *name;
    Param *params;
    int nparams;
    Type ret;
    Stmt *body;
    int line, col;
    bool is_main;
    bool is_extern; /* declared with `extern fn`, defined by a C library */
    bool is_vararg; /* extern fn with `...`, extra args pass through as-is */
} FnDef;

typedef struct {
    StructDef **structs;
    int nstructs, capstructs;
    FnDef **fns;
    int nfns, capfns;
    const char **includes; /* C headers requested with include "..." */
    int nincludes, capincludes;
    const char **csrcs; /* .c files from `include "x.c" as extern C`, linked */
    int ncsrcs, capcsrcs;
} Program;

typedef struct {
    const char *name;
    Type type;
    int depth;
    bool is_struct_param;
} Sym;

typedef struct Comp {
    const char *filename;
    const char *src;
    size_t len;
    Arena arena;
    int errors;
    const char **interns;
    int ninterns, capinterns;
    Program prog;
    Sym *syms;
    int nsyms, capsyms;
    int scope;
    int func_sym_base;
    const char **imports; /* .oak files requested by the file being parsed */
    int nimports, capimports;
    const char **loaded; /* files already parsed (absolute-ish paths) */
    int nloaded, caploaded;
    const char **incdirs; /* directories searched for include/import names */
    int nincdirs, capincdirs;
    bool no_protos; /* skip emitted C prototypes for extern functions */
} Comp;

const char *intern(Comp *c, const char *s, size_t n);
void comp_error(Comp *c, int line, int col, const char *fmt, ...);
Type type_i8(void);
Type type_u8(void);
Type type_i16(void);
Type type_u16(void);
Type type_i32(void);
Type type_u32(void);
Type type_i64(void);
Type type_u64(void);
Type type_f64(void);
Type type_bool(void);
Type type_unit(void);
Type type_struct(int id);
Type type_string(void);
Type type_ptr(void);
Type type_ptr_to(Type *elem);
Type type_raw(const char *text);
Type type_array(Type *elem);
bool type_eq(Type a, Type b);
bool type_is_struct(Type t);
bool type_is_float(Type t);
bool type_is_int(Type t);
/* Width C gives an integer literal of this kind, and whether `v` fits in it.
   Used for literal typing and for rejecting `var x: u8 = 300;` style mistakes. */
bool type_int_bounds(Type t, int64_t *lo, int64_t *hi);
const char *type_name(Comp *c, Type t);

void ptrlist_push(void ***arr, int *n, int *cap, void *p);

/* Path helpers, shared by the driver and the parser (which resolves
   `include "x.c"` relative to the file containing it). Both return
   malloc'd strings. */
char *path_dir(const char *path);
char *path_join(const char *dir, const char *rel);
char *path_canonical(const char *path);
bool file_exists(const char *path);
bool dir_exists(const char *path);
/* Last-resort lookup of an include/import name in `c->incdirs`
   (command-line -I paths plus the default include/ folders).
   Returns a malloc'd path, or NULL when no directory holds it. */
char *comp_find_include(Comp *c, const char *spec);
const char *unresolved_type_name(Type t);

typedef struct {
    Comp *c;
    size_t pos;
    int line, col;
    Token tok;
} Lexer;

void lex_init(Lexer *l, Comp *c);
void lex_next(Lexer *l);

Program *parse_program(Lexer *l);
void typecheck(Comp *c);
void aliascheck(Comp *c);
void codegen(Comp *c, FILE *out);

#endif
