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
        } else if (strcmp(id, "return") == 0) {
            k = TOK_RETURN;
        } else if (strcmp(id, "true") == 0) {
            k = TOK_TRUE;
        } else if (strcmp(id, "false") == 0) {
            k = TOK_FALSE;
        } else if (strcmp(id, "i32") == 0) {
            k = TOK_I32;
        } else if (strcmp(id, "f64") == 0) {
            k = TOK_F64;
        } else if (strcmp(id, "bool") == 0) {
            k = TOK_BOOL;
        } else if (strcmp(id, "string") == 0) {
            k = TOK_STRTYPE;
        } else if (strcmp(id, "ptr") == 0) {
            k = TOK_PTR;
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
        while (l->pos < n && isdigit((unsigned char)s[l->pos])) {
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
            size_t flen = l->pos - start;
            if (flen >= sizeof(buf)) {
                flen = sizeof(buf) - 1;
            }
            memcpy(buf, s + start, flen);
            buf[flen] = 0;
            l->tok = make_tok(TOK_FLOAT, line, col);
            l->tok.fval = strtod(buf, NULL);
            return;
        }
        int64_t v = 0;
        for (size_t i = start; i < l->pos; i++) {
            v = v * 10 + (s[i] - '0');
        }
        l->tok = make_tok(TOK_INT, line, col);
        l->tok.ival = v;
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
        if (l->pos + 1 < n && s[l->pos] == '.' && s[l->pos + 1] == '.') {
            l->pos += 2;
            l->col += 2;
            l->tok = make_tok(TOK_ELLIPSIS, line, col);
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
        comp_error(c, line, col, "unexpected '&' (Oak has no pointers or references in source)");
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
