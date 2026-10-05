#include "oak.h"

static void lex_skip(Lexer *l) {
    const char *s = l->c->src;
    size_t n = l->c->len;
    while (l->pos < n) {
        char ch = s[l->pos];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            l->pos++;
            l->col++;
            continue;
        }
        if (ch == '\n') {
            l->pos++;
            l->line++;
            l->col = 1;
            continue;
        }
        if (ch == '/' && l->pos + 1 < n && s[l->pos + 1] == '/') {
            l->pos += 2;
            while (l->pos < n && s[l->pos] != '\n') {
                l->pos++;
            }
            continue;
        }
        break;
    }
}

static Token make_tok(TokKind k, int line, int col) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = k;
    t.line = line;
    t.col = col;
    return t;
}

void lex_init(Lexer *l, Comp *c) {
    l->c = c;
    l->pos = 0;
    l->line = 1;
    l->col = 1;
    lex_next(l);
}

/* Copy a numeric token's text into `buf`, dropping the `_` digit separators, so
   strtod/strtoull see only digits. Truncates rather than overflowing. */
static void copy_number(char *buf, size_t cap, const char *s, size_t len) {
    size_t j = 0;
    for (size_t i = 0; i < len && j + 1 < cap; i++) {
        if (s[i] != '_') {
            buf[j++] = s[i];
        }
    }
    buf[j] = 0;
}

void lex_next(Lexer *l) {
    lex_skip(l);
    Comp *c = l->c;
    const char *s = c->src;
    size_t n = c->len;
    int line = l->line, col = l->col;
    if (l->pos >= n) {
        l->tok = make_tok(TOK_EOF, line, col);
        return;
    }
    char ch = s[l->pos];
    if (isalpha((unsigned char)ch) || ch == '_') {
        size_t start = l->pos;
        l->pos++;
        l->col++;
        while (l->pos < n && (isalnum((unsigned char)s[l->pos]) || s[l->pos] == '_')) {
            l->pos++;
            l->col++;
        }
        size_t len = l->pos - start;
        const char *id = intern(c, s + start, len);
        TokKind k = TOK_IDENT;
        if (strcmp(id, "fn") == 0) {
            k = TOK_FN;
        } else if (strcmp(id, "struct") == 0) {
            k = TOK_STRUCT;
        } else if (strcmp(id, "var") == 0) {
            k = TOK_VAR;
        } else if (strcmp(id, "if") == 0) {
            k = TOK_IF;
        } else if (strcmp(id, "else") == 0) {
            k = TOK_ELSE;
        } else if (strcmp(id, "while") == 0) {
            k = TOK_WHILE;
        } else if (strcmp(id, "for") == 0) {
            k = TOK_FOR;
        } else if (strcmp(id, "in") == 0) {
            k = TOK_IN;
        } else if (strcmp(id, "return") == 0) {
            k = TOK_RETURN;
        } else if (strcmp(id, "true") == 0) {
            k = TOK_TRUE;
        } else if (strcmp(id, "false") == 0) {
            k = TOK_FALSE;
        } else if (strcmp(id, "i8") == 0) {
            k = TOK_I8;
        } else if (strcmp(id, "u8") == 0) {
            k = TOK_U8;
        } else if (strcmp(id, "i16") == 0) {
            k = TOK_I16;
        } else if (strcmp(id, "u16") == 0) {
            k = TOK_U16;
        } else if (strcmp(id, "i32") == 0) {
            k = TOK_I32;
        } else if (strcmp(id, "u32") == 0) {
            k = TOK_U32;
        } else if (strcmp(id, "i64") == 0) {
            k = TOK_I64;
        } else if (strcmp(id, "u64") == 0) {
            k = TOK_U64;
        } else if (strcmp(id, "f64") == 0) {
            k = TOK_F64;
        } else if (strcmp(id, "bool") == 0) {
            k = TOK_BOOL;
        } else if (strcmp(id, "string") == 0) {
            k = TOK_STRTYPE;
        } else if (strcmp(id, "ptr") == 0) {
            k = TOK_PTR;
        } else if (strcmp(id, "defer") == 0) {
            k = TOK_DEREF;
        } else if (strcmp(id, "null") == 0) {
            k = TOK_NULL;
        } else if (strcmp(id, "extern") == 0) {
            k = TOK_EXTERN;
        } else if (strcmp(id, "import") == 0) {
            k = TOK_IMPORT;
        } else if (strcmp(id, "include") == 0) {
            k = TOK_INCLUDE;
        }
        l->tok = make_tok(k, line, col);
        l->tok.ident = id;
        return;
    }
    if (isdigit((unsigned char)ch)) {
        size_t start = l->pos;
        /* 0x / 0b literals: the bases C code reaches for when it means to talk
           about bits and masks. */
        if (ch == '0' && start + 1 < n && (s[start + 1] == 'x' || s[start + 1] == 'X')) {
            l->pos += 2;
            l->col += 2;
            while (l->pos < n && (isxdigit((unsigned char)s[l->pos]) || s[l->pos] == '_')) {
                l->pos++;
                l->col++;
            }
            char buf[128];
            copy_number(buf, sizeof(buf), s + start, l->pos - start);
            l->tok = make_tok(TOK_INT, line, col);
            l->tok.uval = strtoull(buf + 2, NULL, 16);
            l->tok.ival = (int64_t)l->tok.uval;
            return;
        }
        if (ch == '0' && start + 1 < n && (s[start + 1] == 'b' || s[start + 1] == 'B')) {
            l->pos += 2;
            l->col += 2;
            while (l->pos < n && (s[l->pos] == '0' || s[l->pos] == '1' || s[l->pos] == '_')) {
                l->pos++;
                l->col++;
            }
            char buf[128];
            copy_number(buf, sizeof(buf), s + start, l->pos - start);
            l->tok = make_tok(TOK_INT, line, col);
            l->tok.uval = strtoull(buf + 2, NULL, 2);
            l->tok.ival = (int64_t)l->tok.uval;
            return;
        }
        while (l->pos < n && (isdigit((unsigned char)s[l->pos]) || s[l->pos] == '_')) {
            l->pos++;
            l->col++;
        }
        if (l->pos + 1 < n && s[l->pos] == '.' && isdigit((unsigned char)s[l->pos + 1])) {
            l->pos++;
            l->col++;
            while (l->pos < n && isdigit((unsigned char)s[l->pos])) {
                l->pos++;
                l->col++;
            }
            char buf[128];
            copy_number(buf, sizeof(buf), s + start, l->pos - start);
            l->tok = make_tok(TOK_FLOAT, line, col);
            l->tok.fval = strtod(buf, NULL);
            return;
        }
        char buf[128];
        copy_number(buf, sizeof(buf), s + start, l->pos - start);
        l->tok = make_tok(TOK_INT, line, col);
        /* strtoull keeps the full 64 bits, so a literal above INT64_MAX is
           still exact and can be given to a u64. */
        l->tok.uval = strtoull(buf, NULL, 10);
        l->tok.ival = (int64_t)l->tok.uval;
        return;
    }
    if (ch == '"') {
        size_t cap = 32, len = 0;
        char *buf = xmalloc(cap);
        l->pos++;
        l->col++;
        bool closed = false;
        while (l->pos < n) {
            char d = s[l->pos];
            if (d == '"') {
                l->pos++;
                l->col++;
                closed = true;
                break;
            }
            if (d == '\n') {
                break;
            }
            if (d == '\\') {
                if (l->pos + 1 >= n) {
                    break;
                }
                char e = s[l->pos + 1];
                char out;
                if (e == 'n') {
                    out = '\n';
                } else if (e == 't') {
                    out = '\t';
                } else if (e == 'r') {
                    out = '\r';
                } else if (e == '\\') {
                    out = '\\';
                } else if (e == '"') {
                    out = '"';
                } else {
                    comp_error(c, l->line, l->col, "unknown escape '\\%c'", e);
                    out = e;
                }
                l->pos += 2;
                l->col += 2;
                if (len + 2 > cap) {
                    cap *= 2;
                    buf = xrealloc(buf, cap);
                }
                buf[len++] = out;
                continue;
            }
            l->pos++;
            l->col++;
            if (len + 2 > cap) {
                cap *= 2;
                buf = xrealloc(buf, cap);
            }
            buf[len++] = d;
        }
        if (!closed) {
            comp_error(c, line, col, "unterminated string literal");
        }
        buf[len] = 0;
        const char *sv = intern(c, buf, len);
        free(buf);
        l->tok = make_tok(TOK_STR, line, col);
        l->tok.sval = sv;
        return;
    }
    l->pos++;
    l->col++;
    if (ch == '(') {
        l->tok = make_tok(TOK_LPAREN, line, col);
        return;
    }
    if (ch == ')') {
        l->tok = make_tok(TOK_RPAREN, line, col);
        return;
    }
    if (ch == '{') {
        l->tok = make_tok(TOK_LBRACE, line, col);
        return;
    }
    if (ch == '}') {
        l->tok = make_tok(TOK_RBRACE, line, col);
        return;
    }
    if (ch == ',') {
        l->tok = make_tok(TOK_COMMA, line, col);
        return;
    }
    if (ch == ':') {
        l->tok = make_tok(TOK_COLON, line, col);
        return;
    }
    if (ch == ';') {
        l->tok = make_tok(TOK_SEMI, line, col);
        return;
    }
    if (ch == '[') {
        l->tok = make_tok(TOK_LBRACKET, line, col);
        return;
    }
    if (ch == ']') {
        l->tok = make_tok(TOK_RBRACKET, line, col);
        return;
    }
    if (ch == '.') {
        /* `...` (variadic marker) keeps its old shape; `..` is an inclusive
           range. Both branches return before the shared advance at the
           bottom, so each consumes exactly its own dots. */
        if (l->pos + 1 < n && s[l->pos] == '.' && s[l->pos + 1] == '.') {
            l->pos += 2;
            l->col += 2;
            l->tok = make_tok(TOK_ELLIPSIS, line, col);
            return;
        }
        if (l->pos < n && s[l->pos] == '.') {
            /* Two dots, but not a third: an inclusive range (`0..5`). The
               outer advance (below) consumed the first dot, so consume the
               second one here and emit the token. */
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_DOTDOT, line, col);
            return;
        }
        l->tok = make_tok(TOK_DOT, line, col);
        return;
    }
    if (ch == '+') {
        l->tok = make_tok(TOK_PLUS, line, col);
        return;
    }
    if (ch == '%') {
        l->tok = make_tok(TOK_PERCENT, line, col);
        return;
    }
    if (ch == '*') {
        l->tok = make_tok(TOK_STAR, line, col);
        return;
    }
    if (ch == '/') {
        l->tok = make_tok(TOK_SLASH, line, col);
        return;
    }
    if (ch == '!') {
        if (l->pos < n && s[l->pos] == '=') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_NEQ, line, col);
            return;
        }
        l->tok = make_tok(TOK_NOT, line, col);
        return;
    }
    if (ch == '=') {
        if (l->pos < n && s[l->pos] == '=') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_EQEQ, line, col);
            return;
        }
        l->tok = make_tok(TOK_EQ, line, col);
        return;
    }
    if (ch == '<') {
        if (l->pos < n && s[l->pos] == '=') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_LE, line, col);
            return;
        }
        l->tok = make_tok(TOK_LT, line, col);
        return;
    }
    if (ch == '>') {
        if (l->pos < n && s[l->pos] == '=') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_GE, line, col);
            return;
        }
        l->tok = make_tok(TOK_GT, line, col);
        return;
    }
    if (ch == '&') {
        if (l->pos < n && s[l->pos] == '&') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_ANDAND, line, col);
            return;
        }
        comp_error(c, line, col,
                   "unexpected '&' (Oak writes addresses as ptr(x), not &x)");
        l->tok = make_tok(TOK_ANDAND, line, col);
        return;
    }
    if (ch == '|') {
        if (l->pos < n && s[l->pos] == '|') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_OROR, line, col);
            return;
        }
        comp_error(c, line, col, "unexpected '|'");
        l->tok = make_tok(TOK_EOF, line, col);
        return;
    }
    if (ch == '-') {
        if (l->pos < n && s[l->pos] == '>') {
            l->pos++;
            l->col++;
            l->tok = make_tok(TOK_ARROW, line, col);
            return;
        }
        l->tok = make_tok(TOK_MINUS, line, col);
        return;
    }
    comp_error(c, line, col, "unexpected character '%c'", ch);
    lex_next(l);
}
