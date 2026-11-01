#ifndef PP_LEXER_H
#define PP_LEXER_H

#include "common.h"

typedef enum {
    T_EOF,
    T_IDENT,
    T_INT,
    T_FLOAT,
    T_STRING,
    T_OP,       /* operator / symbolic function name: + - ... # == */
    T_ASSIGN,   /* = */
    T_COMPOUND, /* += -= ... (text holds the operator without '=') */
    T_ARROW,    /* -> */
    T_DOT,
    T_LPAREN,
    T_RPAREN,
    T_LBRACKET,
    T_RBRACKET,
    T_LBRACE,
    T_RBRACE,
    T_COMMA,
    T_COLON,
    T_SEMI,
    /* keywords */
    T_IMPORT,
    T_FUNCTION,
    T_RETURN,
    T_CONST,
    T_LET,
    T_IF,
    T_ELSE,
    T_FOR,
    T_IN,
    T_WHILE,
    T_BREAK,
    T_CONTINUE,
    T_MATCH,
    T_ENUM,
    T_EXPORT,
    T_GLOBAL,
    T_TRUE,
    T_FALSE
} TokKind;

typedef struct {
    TokKind kind;
    const char* start;
    int len;
    int line;
    int col;
    bool nl_before;    /* a newline separates this token from the previous one */
    bool space_before; /* any whitespace separates this token from the previous one */
    int64_t ival;
    double fval;
    char* sval;        /* decoded string literal */
    int slen;
} Token;

typedef struct {
    Token* toks;
    int count;
    int cap;
} TokenList;

/* Tokenizes src. Prints a diagnostic and returns false on error. */
bool lex(const char* src, const char* file, TokenList* out);

/* Operator names known to the lexer, used to split runs like `...#` into `...` `#`. */
void lexer_add_known_op(const char* s, int len);
bool lexer_is_known_op(const char* s, int len);

#endif
