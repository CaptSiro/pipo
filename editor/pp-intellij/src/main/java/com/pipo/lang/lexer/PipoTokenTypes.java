package com.pipo.lang.lexer;

import com.intellij.psi.TokenType;
import com.intellij.psi.tree.IElementType;
import com.intellij.psi.tree.TokenSet;

import java.util.Map;

/** Token types; they follow TokKind in src/lexer.h. */
public interface PipoTokenTypes {
    IElementType WHITE_SPACE = TokenType.WHITE_SPACE;
    IElementType BAD_CHARACTER = TokenType.BAD_CHARACTER;

    IElementType LINE_COMMENT = new PipoTokenType("LINE_COMMENT");
    IElementType BLOCK_COMMENT = new PipoTokenType("BLOCK_COMMENT");

    IElementType IDENT = new PipoTokenType("IDENT");
    IElementType INT = new PipoTokenType("INT");
    IElementType FLOAT = new PipoTokenType("FLOAT");
    IElementType STRING = new PipoTokenType("STRING");

    /** operator / symbolic function name: + - ... # == */
    IElementType OP = new PipoTokenType("OP");
    /** = */
    IElementType ASSIGN = new PipoTokenType("=");
    /** += -= ...: the whole text, '=' included */
    IElementType COMPOUND = new PipoTokenType("COMPOUND_ASSIGN");
    /** -> */
    IElementType ARROW = new PipoTokenType("->");
    IElementType DOT = new PipoTokenType(".");
    IElementType LPAREN = new PipoTokenType("(");
    IElementType RPAREN = new PipoTokenType(")");
    IElementType LBRACKET = new PipoTokenType("[");
    IElementType RBRACKET = new PipoTokenType("]");
    IElementType LBRACE = new PipoTokenType("{");
    IElementType RBRACE = new PipoTokenType("}");
    IElementType COMMA = new PipoTokenType(",");
    IElementType COLON = new PipoTokenType(":");
    IElementType SEMI = new PipoTokenType(";");

    // keywords
    IElementType IMPORT = new PipoTokenType("import");
    IElementType FUNCTION = new PipoTokenType("function");
    IElementType RETURN = new PipoTokenType("return");
    IElementType CONST = new PipoTokenType("const");
    IElementType LET = new PipoTokenType("let");
    IElementType IF = new PipoTokenType("if");
    IElementType ELSE = new PipoTokenType("else");
    IElementType FOR = new PipoTokenType("for");
    IElementType IN = new PipoTokenType("in");
    IElementType WHILE = new PipoTokenType("while");
    IElementType BREAK = new PipoTokenType("break");
    IElementType CONTINUE = new PipoTokenType("continue");
    IElementType MATCH = new PipoTokenType("match");
    IElementType ENUM = new PipoTokenType("enum");
    IElementType EXPORT = new PipoTokenType("export");
    IElementType GLOBAL = new PipoTokenType("global");
    IElementType TRUE = new PipoTokenType("true");
    IElementType FALSE = new PipoTokenType("false");

    /*
     * Contextual keywords. The lexer returns IDENT for them; the parser remaps
     * the token when the word is used as a keyword, so `type` can still be a
     * variable name elsewhere.
     */
    IElementType TYPE_KW = new PipoTokenType("type");
    IElementType INTERFACE_KW = new PipoTokenType("interface");
    IElementType IMPL_KW = new PipoTokenType("impl");
    IElementType PRECEDENCE_KW = new PipoTokenType("precedence");
    IElementType OVER_KW = new PipoTokenType("over");
    IElementType UNDER_KW = new PipoTokenType("under");
    IElementType AS_KW = new PipoTokenType("as");
    IElementType NATIVE_KW = new PipoTokenType("native");

    Map<String, IElementType> KEYWORDS = Map.ofEntries(
            Map.entry("import", IMPORT), Map.entry("function", FUNCTION), Map.entry("return", RETURN),
            Map.entry("const", CONST), Map.entry("let", LET), Map.entry("if", IF), Map.entry("else", ELSE),
            Map.entry("for", FOR), Map.entry("in", IN), Map.entry("while", WHILE), Map.entry("break", BREAK),
            Map.entry("continue", CONTINUE), Map.entry("match", MATCH), Map.entry("enum", ENUM),
            Map.entry("true", TRUE), Map.entry("false", FALSE), Map.entry("export", EXPORT), Map.entry("global", GLOBAL));

    TokenSet COMMENTS = TokenSet.create(LINE_COMMENT, BLOCK_COMMENT);
    TokenSet WHITESPACES = TokenSet.create(WHITE_SPACE);
    TokenSet STRINGS = TokenSet.create(STRING);
    TokenSet NUMBERS = TokenSet.create(INT, FLOAT);

    TokenSet HARD_KEYWORDS = TokenSet.create(IMPORT, FUNCTION, RETURN, CONST, LET, IF, ELSE, FOR, IN, WHILE, BREAK,
            CONTINUE, MATCH, ENUM, EXPORT, GLOBAL, TRUE, FALSE);
    TokenSet SOFT_KEYWORDS = TokenSet.create(TYPE_KW, INTERFACE_KW, IMPL_KW, PRECEDENCE_KW, OVER_KW, UNDER_KW, AS_KW,
            NATIVE_KW);
    TokenSet KEYWORDS_SET = TokenSet.orSet(HARD_KEYWORDS, SOFT_KEYWORDS);

    TokenSet OPERATORS = TokenSet.create(OP, ASSIGN, COMPOUND, ARROW);
    /** tokens that can name a function: an identifier, an operator, or `in` */
    TokenSet FN_NAMES = TokenSet.create(IDENT, OP, IN);
    /** tokens a plain identifier may have been remapped to */
    TokenSet IDENT_LIKE = TokenSet.orSet(TokenSet.create(IDENT), SOFT_KEYWORDS);
}
