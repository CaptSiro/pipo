#include "lexer.h"

#include <ctype.h>
#include <errno.h>

/* ------------------------------------------------------------ known ops */

static char** known_ops;
static int nknown, capknown;
static bool known_init = false;

static const char* BUILTIN_OPS[] = {
    "+", "-", "*", "/", "%", "**", "==", "!=", "<", ">", "<=", ">=",
    "!", "#", "...", "...=", "&&", "||", "?", NULL,
};

static void ensure_known_init(void) {
    if (known_init) {
        return;
    }

    known_init = true;
    for (int i = 0; BUILTIN_OPS[i]; i++) {
        lexer_add_known_op(BUILTIN_OPS[i], (int)strlen(BUILTIN_OPS[i]));
    }
}

bool lexer_is_known_op(const char* s, int len) {
    ensure_known_init();
    for (int i = 0; i < nknown; i++) {
        if ((int)strlen(known_ops[i]) == len && memcmp(known_ops[i], s, (size_t)len) == 0) {
            return true;
        }
    }

    return false;
}

void lexer_add_known_op(const char* s, int len) {
    ensure_known_init();
    if (lexer_is_known_op(s, len)) {
        return;
    }

    GROW(known_ops, nknown, capknown);
    known_ops[nknown++] = pp_strndup(s, (size_t)len);
}

static bool is_reserved_op(const char* s, int len) {
    return (len == 1 && s[0] == '=') || (len == 2 && s[0] == '-' && s[1] == '>');
}

/* ---------------------------------------------------------------- lexer */

typedef struct {
    const char* file;
    const char* p;
    const char* line_start;
    int line;
    TokenList* out;
} Lexer;

static const struct {
    const char* s;
    TokKind k;
} KEYWORDS[] = {
    {"import", T_IMPORT}, {"function", T_FUNCTION}, {"return", T_RETURN},   {"const", T_CONST},
    {"let", T_LET},       {"if", T_IF},             {"else", T_ELSE},       {"for", T_FOR},
    {"in", T_IN},         {"while", T_WHILE},       {"break", T_BREAK},     {"continue", T_CONTINUE},
    {"match", T_MATCH},   {"enum", T_ENUM},         {"true", T_TRUE},       {"false", T_FALSE},
    {"export", T_EXPORT},   {"global", T_GLOBAL},
    {NULL, T_EOF},
};

static const char* OP_CHARS = "+-*/%<>=!&|^~#?@$\\";

static bool is_op_char(char c) {
    return c != '\0' && strchr(OP_CHARS, c) != NULL;
}

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

static bool lex_error(Lexer* L, const char* at, const char* fmt, ...) {
    fflush(stdout);
    fprintf(stderr, "%s:%d:%d: syntax error: ", L->file, L->line, (int)(at - L->line_start) + 1);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    return false;
}

static void push_tok(TokenList* out, Token t) {
    GROW(out->toks, out->count, out->cap);
    out->toks[out->count++] = t;
}

static void utf8_encode(StrBuf* sb, uint32_t cp) {
    if (cp < 0x80) {
        sb_putc(sb, (char)cp);
    } else if (cp < 0x800) {
        sb_putc(sb, (char)(0xC0 | (cp >> 6)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sb_putc(sb, (char)(0xE0 | (cp >> 12)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(sb, (char)(0xF0 | (cp >> 18)));
        sb_putc(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static bool lex_string(Lexer* L, Token* t) {
    const char quote = *L->p++;
    StrBuf sb;
    sb_init(&sb);
    for (;;) {
        const char c = *L->p;
        if (c == '\0') {
            sb_free(&sb);
            return lex_error(L, t->start, "unterminated string literal");
        }

        if (c == quote) {
            L->p++;
            break;
        }

        if (c == '\n') {
            sb_putc(&sb, '\n');
            L->p++;
            L->line++;
            L->line_start = L->p;
            continue;
        }

        if (c != '\\') {
            sb_putc(&sb, c);
            L->p++;
            continue;
        }

        const char* esc = L->p;
        L->p++;
        const char e = *L->p++;
        switch (e) {
            case 'n': sb_putc(&sb, '\n'); break;
            case 't': sb_putc(&sb, '\t'); break;
            case 'r': sb_putc(&sb, '\r'); break;
            case '0': sb_putc(&sb, '\0'); break;
            case '\\': sb_putc(&sb, '\\'); break;
            case '"': sb_putc(&sb, '"'); break;
            case '\'': sb_putc(&sb, '\''); break;
            case 'x': {
                const int h1 = hex_val(L->p[0]);
                const int h2 = h1 < 0 ? -1 : hex_val(L->p[1]);
                if (h2 < 0) {
                    sb_free(&sb);
                    return lex_error(L, esc, "invalid \\x escape (expected two hex digits)");
                }

                sb_putc(&sb, (char)(h1 * 16 + h2));
                L->p += 2;
                break;
            }
            case 'u': {
                if (*L->p != '{') {
                    sb_free(&sb);
                    return lex_error(L, esc, "invalid \\u escape (expected \\u{XXXX})");
                }

                L->p++;
                uint32_t cp = 0;
                int digits = 0;
                while (hex_val(*L->p) >= 0) {
                    cp = cp * 16 + (uint32_t)hex_val(*L->p++);
                    digits++;
                }

                if (*L->p != '}' || digits == 0 || cp > 0x10FFFF) {
                    sb_free(&sb);
                    return lex_error(L, esc, "invalid \\u escape (expected \\u{XXXX})");
                }

                L->p++;
                utf8_encode(&sb, cp);
                break;
            }
            default:
                sb_free(&sb);
                return lex_error(L, esc, "unknown escape sequence '\\%c'", e ? e : '0');
        }
    }

    t->kind = T_STRING;
    t->sval = sb.data;
    t->slen = (int)sb.len;

    return true;
}

static bool lex_number(Lexer* L, Token* t) {
    const char* p = L->p;
    char buf[128];
    int n = 0;
    bool is_float = false;
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
        while (hex_val(*p) >= 0 || *p == '_') {
            if (*p != '_' && n < 120) {
                buf[n++] = *p;
            }

            p++;
        }
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        base = 2;
        p += 2;
        while (*p == '0' || *p == '1' || *p == '_') {
            if (*p != '_' && n < 120) {
                buf[n++] = *p;
            }

            p++;
        }
    } else {
        while (isdigit((unsigned char)*p) || *p == '_') {
            if (*p != '_' && n < 120) {
                buf[n++] = *p;
            }

            p++;
        }

        if (*p == '.' && isdigit((unsigned char)p[1])) {
            is_float = true;
            buf[n++] = *p++;
            while (isdigit((unsigned char)*p) || *p == '_') {
                if (*p != '_' && n < 120) {
                    buf[n++] = *p;
                }

                p++;
            }
        }

        if ((*p == 'e' || *p == 'E') &&
            (isdigit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
            is_float = true;
            buf[n++] = *p++;
            if (*p == '+' || *p == '-') {
                buf[n++] = *p++;
            }
            while (isdigit((unsigned char)*p)) {
                if (n < 120) {
                    buf[n++] = *p;
                }

                p++;
            }
        }
    }

    buf[n] = '\0';
    if (n == 0) {
        return lex_error(L, L->p, "malformed number literal");
    }

    if (is_ident_char(*p)) {
        return lex_error(L, p, "invalid character '%c' in number literal", *p);
    }

    if (is_float) {
        t->kind = T_FLOAT;
        t->fval = strtod(buf, NULL);
    } else {
        errno = 0;
        const unsigned long long v = strtoull(buf, NULL, base);
        /* hex / binary literals may use all 64 bits (two's complement) */
        if (errno == ERANGE || (base == 10 && v > (unsigned long long)INT64_MAX)) {
            return lex_error(L, L->p, "integer literal '%s' is too large", buf);
        }

        t->kind = T_INT;
        t->ival = (int64_t)v;
    }

    L->p = p;

    return true;
}

static void split_op(TokenList* out, Token t) {
    const char* s = t.start;
    int n = t.len;
    int pos = 0;
    bool first = true;
    while (pos < n) {
        Token piece = t;
        piece.start = s + pos;
        piece.col = t.col + pos;
        if (!first) {
            piece.nl_before = false;
            piece.space_before = false;
        }

        int rem = n - pos;
        int take = 0;
        if (is_reserved_op(s + pos, rem) || lexer_is_known_op(s + pos, rem)) {
            take = rem;
        } else if (first && n >= 2 && s[n - 1] == '=' && s[0] != '=' && lexer_is_known_op(s, n - 1)) {
            piece.kind = T_COMPOUND;
            piece.len = n - 1;
            push_tok(out, piece);
            return;
        } else {
            for (int len = rem - 1; len >= 1; len--) {
                if (is_reserved_op(s + pos, len) || lexer_is_known_op(s + pos, len)) {
                    take = len;
                    break;
                }
            }

            if (!take) {
                take = rem;
            }
        }

        piece.len = take;
        piece.kind = T_OP;
        if (is_reserved_op(piece.start, take)) {
            piece.kind = take == 1 ? T_ASSIGN : T_ARROW;
        }

        push_tok(out, piece);
        pos += take;
        first = false;
    }
}

bool lex(const char* src, const char* file, TokenList* out) {
    Lexer L;
    L.file = file;
    L.p = src;
    L.line_start = src;
    L.line = 1;
    TokenList raw = {0};
    L.out = &raw;

    if ((unsigned char)L.p[0] == 0xEF && (unsigned char)L.p[1] == 0xBB && (unsigned char)L.p[2] == 0xBF) {
        L.p += 3;
    }

    bool nl = true;
    bool space = true;
    for (;;) {
        const char c = *L.p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            L.p++;
            space = true;
            continue;
        }

        if (c == '\n') {
            L.p++;
            L.line++;
            L.line_start = L.p;
            nl = space = true;
            continue;
        }

        if (c == '/' && L.p[1] == '/') {
            while (*L.p && *L.p != '\n') {
                L.p++;
            }

            space = true;
            continue;
        }

        if (c == '/' && L.p[1] == '*') {
            const char* open = L.p;
            L.p += 2;
            while (*L.p && !(L.p[0] == '*' && L.p[1] == '/')) {
                if (*L.p == '\n') {
                    L.line++;
                    L.line_start = L.p + 1;
                    nl = true;
                }

                L.p++;
            }

            if (!*L.p) {
                return lex_error(&L, open, "unterminated block comment");
            }

            L.p += 2;
            space = true;
            continue;
        }

        Token t;
        memset(&t, 0, sizeof t);
        t.start = L.p;
        t.line = L.line;
        t.col = (int)(L.p - L.line_start) + 1;
        t.nl_before = nl;
        t.space_before = space;
        nl = space = false;

        if (c == '\0') {
            t.kind = T_EOF;
            push_tok(&raw, t);
            break;
        }

        if (isdigit((unsigned char)c)) {
            if (!lex_number(&L, &t)) {
                return false;
            }
        } else if (is_ident_start(c)) {
            while (is_ident_char(*L.p)) {
                L.p++;
            }

            t.kind = T_IDENT;
            const int len = (int)(L.p - t.start);
            for (int i = 0; KEYWORDS[i].s; i++) {
                if ((int)strlen(KEYWORDS[i].s) == len && memcmp(KEYWORDS[i].s, t.start, (size_t)len) == 0) {
                    t.kind = KEYWORDS[i].k;
                    break;
                }
            }
        } else if (c == '"' || c == '\'') {
            if (!lex_string(&L, &t)) {
                return false;
            }
        } else if (c == '.' && L.p[1] != '.') {
            t.kind = T_DOT;
            L.p++;
        } else if (is_op_char(c) || c == '.') {
            while (*L.p) {
                if (L.p[0] == '/' && (L.p[1] == '/' || L.p[1] == '*')) {
                    break;
                }

                if (is_op_char(*L.p)) {
                    L.p++;
                    continue;
                }

                if (*L.p == '.' && (L.p[1] == '.' || (L.p > t.start && L.p[-1] == '.'))) {
                    L.p++;
                    continue;
                }

                break;
            }

            t.kind = T_OP;
        } else {
            switch (c) {
                case '(': t.kind = T_LPAREN; break;
                case ')': t.kind = T_RPAREN; break;
                case '[': t.kind = T_LBRACKET; break;
                case ']': t.kind = T_RBRACKET; break;
                case '{': t.kind = T_LBRACE; break;
                case '}': t.kind = T_RBRACE; break;
                case ',': t.kind = T_COMMA; break;
                case ':': t.kind = T_COLON; break;
                case ';': t.kind = T_SEMI; break;
                default: return lex_error(&L, L.p, "unexpected character '%c'", c);
            }

            L.p++;
        }

        t.len = (int)(L.p - t.start);
        push_tok(&raw, t);
    }

    /* operators declared in this file (function <op>, precedence <op> over <op>) */
    for (int i = 0; i + 1 < raw.count; i++) {
        const Token* t = &raw.toks[i];
        if (t->kind == T_FUNCTION && raw.toks[i + 1].kind == T_OP) {
            lexer_add_known_op(raw.toks[i + 1].start, raw.toks[i + 1].len);
        }

        if (t->kind == T_IDENT && t->len == 10 && memcmp(t->start, "precedence", 10) == 0) {
            if (raw.toks[i + 1].kind == T_OP) {
                lexer_add_known_op(raw.toks[i + 1].start, raw.toks[i + 1].len);
            }

            if (i + 3 < raw.count && raw.toks[i + 3].kind == T_OP) {
                lexer_add_known_op(raw.toks[i + 3].start, raw.toks[i + 3].len);
            }
        }
    }

    for (int i = 0; i < raw.count; i++) {
        if (raw.toks[i].kind == T_OP) {
            split_op(out, raw.toks[i]);
        } else {
            push_tok(out, raw.toks[i]);
        }
    }

    free(raw.toks);

    return true;
}
