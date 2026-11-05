package com.pipo.lang.parser;

import com.intellij.lang.ASTNode;
import com.intellij.lang.PsiBuilder;
import com.intellij.lang.PsiParser;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import org.jetbrains.annotations.NotNull;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static com.pipo.lang.lexer.PipoTokenTypes.*;
import static com.pipo.lang.parser.PipoElementTypes.*;

/**
 * A port of src/parser.c onto PsiBuilder. Where the interpreter stops at the
 * first syntax error, this parser reports it and recovers, usually by skipping
 * to the end of the statement.
 * <p>
 * The language is sensitive to newlines and to spaces (a newline ends a
 * statement; {@code x[i]} indexes while {@code f [i]} passes an array), which
 * PsiBuilder hides. The parser therefore keeps its own table of the
 * significant tokens with "newline before" / "space before" flags and finds
 * the current one by the builder's offset.
 */
public final class PipoParser implements PsiParser {
    @Override
    public @NotNull ASTNode parse(@NotNull IElementType root, @NotNull PsiBuilder builder) {
        PsiBuilder.Marker file = builder.mark();
        new Impl(builder).parseFile();
        file.done(root);
        return builder.getTreeBuilt();
    }

    /** A finished expression marker and its node type. */
    private record Res(PsiBuilder.Marker m, IElementType kind) {
    }

    private enum ParamMode {
        /** every parameter needs a name; a lone identifier is the name */
        STRICT,
        /** a named function statement: may turn out to be a declaration */
        DECL,
        /** declaration / interface requirement: types with optional names */
        TYPES
    }

    private static final class Impl {
        private final PsiBuilder b;
        private final CharSequence text;

        /* significant tokens */
        private IElementType[] types;
        private int[] starts;
        private int[] ends;
        private boolean[] nl;
        private boolean[] space;
        private int count;

        private int depth;        // nesting inside (), [] and {} literals: newlines do not end expressions there
        private int blockDepth;   // 0 = top level of the file
        private boolean noLambda; // parsing a match pattern / guard
        private boolean inPattern;
        private boolean inNative;
        private boolean sawLoneParamName;

        private int cachedOffset = -1;
        private int cachedPos;

        Impl(PsiBuilder b) {
            this.b = b;
            this.text = b.getOriginalText();
            tokenize();
        }

        private void tokenize() {
            PipoLexer lexer = new PipoLexer();
            lexer.start(text, 0, text.length(), 0);
            List<IElementType> t = new ArrayList<>();
            List<int[]> r = new ArrayList<>();
            List<Boolean> n = new ArrayList<>();
            List<Boolean> s = new ArrayList<>();
            boolean pendingNl = true;
            boolean pendingSpace = true;
            while (lexer.getTokenType() != null) {
                IElementType type = lexer.getTokenType();
                if (type == WHITE_SPACE || type == LINE_COMMENT || type == BLOCK_COMMENT) {
                    pendingSpace = true;
                    if (type != LINE_COMMENT) {
                        for (int i = lexer.getTokenStart(); i < lexer.getTokenEnd(); i++) {
                            if (text.charAt(i) == '\n') {
                                pendingNl = true;
                                break;
                            }
                        }
                    }
                } else {
                    t.add(type);
                    r.add(new int[]{lexer.getTokenStart(), lexer.getTokenEnd()});
                    n.add(pendingNl);
                    s.add(pendingSpace);
                    pendingNl = false;
                    pendingSpace = false;
                }
                lexer.advance();
            }

            count = t.size();
            types = t.toArray(new IElementType[0]);
            starts = new int[count];
            ends = new int[count];
            nl = new boolean[count + 1];
            space = new boolean[count + 1];
            for (int i = 0; i < count; i++) {
                starts[i] = r.get(i)[0];
                ends[i] = r.get(i)[1];
                nl[i] = n.get(i);
                space[i] = s.get(i);
            }
            nl[count] = true; // EOF
            space[count] = true;
        }

        /* ------------------------------------------------------------ helpers */

        private int pos() {
            int offset = b.getCurrentOffset();
            if (offset == cachedOffset) {
                return cachedPos;
            }
            int i = Arrays.binarySearch(starts, offset);
            if (i < 0) {
                i = -i - 1;
            }
            cachedOffset = offset;
            cachedPos = i;
            return i;
        }

        private IElementType at(int n) {
            int i = pos() + n;
            return i >= 0 && i < count ? types[i] : null;
        }

        private boolean nl(int n) {
            int i = Math.min(pos() + n, count);
            return nl[i];
        }

        private boolean space(int n) {
            int i = Math.min(pos() + n, count);
            return space[i];
        }

        private String text(int n) {
            int i = pos() + n;
            return i >= 0 && i < count ? text.subSequence(starts[i], ends[i]).toString() : "";
        }

        private boolean eof() {
            return b.eof() || at(0) == null;
        }

        private boolean is(IElementType t) {
            return at(0) == t;
        }

        private boolean isIdent(int n) {
            return at(n) == IDENT;
        }

        private boolean word(int n, String w) {
            return at(n) == IDENT && text(n).equals(w);
        }

        private boolean op(int n, String s) {
            return at(n) == OP && text(n).equals(s);
        }

        private boolean opStarts(int n, char c) {
            return at(n) == OP && text(n).charAt(0) == c;
        }

        private void adv() {
            b.advanceLexer();
        }

        private void advAs(IElementType remap) {
            b.remapCurrentToken(remap);
            b.advanceLexer();
        }

        private boolean accept(IElementType t) {
            if (at(0) != t) {
                return false;
            }
            adv();
            return true;
        }

        private PsiBuilder.Marker mark() {
            return b.mark();
        }

        private String desc(int n) {
            IElementType t = at(n);
            if (t == null) {
                return "end of file";
            }
            if (t == STRING) {
                return "string literal";
            }
            String s = text(n);
            if (s.length() > 40) {
                s = s.substring(0, 40);
            }
            if (t == INT || t == FLOAT) {
                return "number '" + s + "'";
            }
            if (t == IDENT) {
                return "identifier '" + s + "'";
            }
            return "'" + s + "'";
        }

        private void error(String msg) {
            b.error(msg);
        }

        private boolean expect(IElementType t, String what) {
            if (accept(t)) {
                return true;
            }
            error("expected " + what + " but found " + desc(0));
            return false;
        }

        /** reports msg on the current token and consumes it */
        private void errorAdvance(String msg) {
            if (eof()) {
                error(msg);
                return;
            }
            PsiBuilder.Marker m = mark();
            adv();
            m.error(msg);
        }

        private boolean isTerminator(int n) {
            IElementType t = at(n);
            return t == SEMI || t == RBRACE || t == null || nl(n);
        }

        private boolean isCloser(IElementType t) {
            return t == RPAREN || t == RBRACKET || t == RBRACE || t == COMMA || t == SEMI || t == null;
        }

        /* an identifier, an operator, `in`, or `[]` / `[]=` */
        private boolean atFnName(int n) {
            IElementType t = at(n);
            if (t == IDENT || t == OP || t == IN) {
                return true;
            }
            return t == LBRACKET && at(n + 1) == RBRACKET && !space(n + 1) && !nl(n + 1);
        }

        private void takeFnName() {
            if (at(0) != LBRACKET) {
                adv();
                return;
            }
            adv();
            adv();
            if (at(0) == ASSIGN && !space(0) && !nl(0)) {
                adv();
            }
        }

        /** a function name wrapped in NAME */
        private boolean fnName() {
            if (!atFnName(0)) {
                error("expected a name but found " + desc(0));
                return false;
            }
            PsiBuilder.Marker m = mark();
            takeFnName();
            m.done(NAME);
            return true;
        }

        /** an identifier wrapped in NAME */
        private boolean name(String what) {
            if (!isIdent(0)) {
                error("expected " + what + " but found " + desc(0));
                return false;
            }
            PsiBuilder.Marker m = mark();
            adv();
            m.done(NAME);
            return true;
        }

        /* ------------------------------------------------------------- file */

        void parseFile() {
            while (!eof()) {
                if (accept(SEMI)) {
                    continue;
                }
                if (is(RBRACE)) {
                    errorAdvance("unexpected '}'");
                    continue;
                }
                int before = b.getCurrentOffset();
                parseStatement();
                if (b.getCurrentOffset() == before) {
                    errorAdvance("unexpected " + desc(0));
                }
            }
        }

        /* ------------------------------------------------------------ types */

        private PsiBuilder.Marker parseType() {
            PsiBuilder.Marker left = parseTypePostfix();
            while (left != null && op(0, "||")) {
                PsiBuilder.Marker u = left.precede();
                adv();
                parseTypePostfix();
                u.done(UNION_TYPE);
                left = u;
            }
            return left;
        }

        private PsiBuilder.Marker parseTypePostfix() {
            PsiBuilder.Marker t = parseTypePrimary();
            while (t != null) {
                if (is(LBRACKET) && !nl(0) && at(1) == RBRACKET) {
                    PsiBuilder.Marker a = t.precede();
                    adv();
                    adv();
                    a.done(ARRAY_TYPE);
                    t = a;
                    continue;
                }
                if (op(0, "?") && !nl(0)) {
                    PsiBuilder.Marker o = t.precede();
                    adv();
                    o.done(OPTION_TYPE);
                    t = o;
                    continue;
                }
                break;
            }
            return t;
        }

        private void parseTypeArgs(String of) {
            PsiBuilder.Marker m = mark();
            adv(); // <
            do {
                if (parseType() == null) {
                    break;
                }
            } while (accept(COMMA));
            if (op(0, ">")) {
                adv();
            } else {
                error("expected '>' to close the type arguments of '" + of + "'");
            }
            m.done(TYPE_ARGS);
        }

        private PsiBuilder.Marker parseTypePrimary() {
            if (isIdent(0)) {
                String name = text(0);
                PsiBuilder.Marker m = mark();
                if (name.equals("tuple")) {
                    adv();
                    if (op(0, "<") && !nl(0)) {
                        parseTypeArgs("tuple");
                    } else {
                        error("'tuple' needs its element types as type arguments, e.g. tuple<int, string>");
                    }
                    m.done(TYPE_REF);
                    return m;
                }

                // module:Type, package:module:Type -> TYPE_REF(TYPE_QUALIFIER(TYPE_QUALIFIER(package) : module) : Type)
                PsiBuilder.Marker qual = null;
                if (at(1) == COLON && !nl(1)) {
                    qual = mark();
                    adv();
                    qual.done(TYPE_QUALIFIER);
                    while (is(COLON) && !nl(0) && isIdent(1) && at(2) == COLON && !nl(2)) {
                        PsiBuilder.Marker q = qual.precede();
                        adv();
                        adv();
                        q.done(TYPE_QUALIFIER);
                        qual = q;
                    }
                    adv(); // the ':' before the type name
                }
                if (!isIdent(0)) {
                    error("expected a type name after ':' but found " + desc(0));
                    m.done(TYPE_REF);
                    return m;
                }
                name = text(0);
                adv();
                if (op(0, "<") && !nl(0)) {
                    parseTypeArgs(name);
                }
                m.done(TYPE_REF);
                return m;
            }

            if (is(FUNCTION)) {
                PsiBuilder.Marker m = mark();
                adv();
                m.done(FUNCTION_TYPE);
                return m;
            }

            if (is(LBRACE)) {
                PsiBuilder.Marker m = mark();
                adv();
                while (!is(RBRACE) && !eof()) {
                    if (accept(COMMA) || accept(SEMI)) {
                        continue;
                    }
                    int before = b.getCurrentOffset();
                    PsiBuilder.Marker f = mark();
                    if (parseType() == null) {
                        f.drop();
                        if (b.getCurrentOffset() == before) {
                            errorAdvance("expected a field type");
                        }
                        continue;
                    }
                    name("field name");
                    f.done(RECORD_FIELD);
                }
                expect(RBRACE, "'}'");
                m.done(RECORD_TYPE);
                return m;
            }

            if (is(LPAREN)) {
                PsiBuilder.Marker m = mark();
                adv();
                parseType();
                expect(RPAREN, "')'");
                m.done(PAREN_TYPE);
                return m;
            }

            error("expected a type but found " + desc(0));
            return null;
        }

        /* offset (from the current token) of the ')' matching the '(' at offset `at`, or -1 */
        private int matchingParen(int at) {
            int d = 0;
            for (int i = at; ; i++) {
                IElementType k = at(i);
                if (k == null) {
                    return -1;
                }
                if (k == LPAREN || k == LBRACKET || k == LBRACE) {
                    d++;
                } else if (k == RPAREN || k == RBRACKET || k == RBRACE) {
                    if (--d == 0) {
                        return k == RPAREN ? i : -1;
                    }
                }
            }
        }

        /* right-hand side of `type Name = ...`: maybe a callback type */
        private void parseTypeDef() {
            if (is(LPAREN)) {
                int close = matchingParen(0);
                if (close > 0 && at(close + 1) == ARROW) {
                    PsiBuilder.Marker fn = mark();
                    adv();
                    while (!is(RPAREN) && !eof()) {
                        if (op(0, "...")) {
                            adv();
                            break;
                        }
                        if (parseType() == null || !accept(COMMA)) {
                            break;
                        }
                    }
                    expect(RPAREN, "')' to close the callback parameter types");
                    expect(ARROW, "'->' and the return type of the callback type");
                    parseTypeDef();
                    fn.done(CALLBACK_TYPE);
                    return;
                }
            }

            PsiBuilder.Marker fn = mark();
            parseType();
            if (!is(ARROW) || nl(0)) {
                fn.drop();
                return;
            }
            adv();
            parseTypeDef();
            fn.done(CALLBACK_TYPE);
        }

        private void noInlineFnType() {
            if (is(ARROW)) {
                error("a callback type cannot be written inline, name it first: type ToInt = float -> int");
            }
        }

        /* <T, U = string> */
        private void parseGenericParams(boolean allowDefaults) {
            PsiBuilder.Marker m = mark();
            if (!op(0, "<")) {
                error("expected '<'");
                m.drop();
                return;
            }
            adv();
            do {
                PsiBuilder.Marker g = mark();
                if (!name("generic parameter name")) {
                    g.drop();
                    break;
                }
                if (is(ASSIGN)) {
                    if (!allowDefaults) {
                        error("default type arguments are only allowed on enums");
                    }
                    adv();
                    parseType();
                }
                g.done(GENERIC_PARAM);
            } while (accept(COMMA));
            if (op(0, ">")) {
                adv();
            } else {
                error("expected '>' to close the generic parameter list");
            }
            m.done(GENERIC_PARAMS);
        }

        private boolean isGenericPrefix() {
            if (!op(0, "<")) {
                return false;
            }
            int i = 1;
            for (; ; ) {
                if (at(i) != IDENT) {
                    return false;
                }
                IElementType sep = at(i + 1);
                if (sep == COMMA) {
                    i += 2;
                    continue;
                }
                if (!opStarts(i + 1, '>')) {
                    return false;
                }
                IElementType after = at(i + 2);
                return after == FUNCTION || word(i + 2, "type") || word(i + 2, "interface") || word(i + 2, "impl");
            }
        }

        private void parseParams(ParamMode mode) {
            PsiBuilder.Marker list = mark();
            if (!expect(LPAREN, "'('")) {
                list.drop();
                return;
            }
            while (!is(RPAREN) && !eof()) {
                if (op(0, "...")) {
                    adv();
                    break;
                }
                int before = b.getCurrentOffset();
                PsiBuilder.Marker prm = mark();
                IElementType nk = at(1);
                if (mode != ParamMode.TYPES && isIdent(0) && (nk == COMMA || nk == RPAREN)) {
                    if (mode == ParamMode.DECL) {
                        sawLoneParamName = true;
                    }
                    name("parameter name");
                } else {
                    parseType();
                    noInlineFnType();
                    if (isIdent(0)) {
                        name("parameter name");
                    } else if (mode == ParamMode.STRICT) {
                        error("expected parameter name but found " + desc(0));
                    }
                }
                prm.done(PARAM);
                if (b.getCurrentOffset() == before) {
                    break;
                }
                if (!accept(COMMA)) {
                    break;
                }
            }
            expect(RPAREN, "')'");
            list.done(PARAM_LIST);
        }

        /* ------------------------------------------------------- expressions */

        private void parseArgs() {
            PsiBuilder.Marker m = mark();
            expect(LPAREN, "'('");
            depth++;
            while (!is(RPAREN) && !eof()) {
                int before = b.getCurrentOffset();
                parseExpression();
                if (b.getCurrentOffset() == before) {
                    break;
                }
                if (!accept(COMMA)) {
                    break;
                }
            }
            expect(RPAREN, "')' to close the argument list");
            depth--;
            m.done(ARG_LIST);
        }

        /**
         * function name(params) -> T { ... } / a declaration without a body.
         * m starts the node (before `global`, `native` or the generic parameters).
         */
        private IElementType parseFunction(PsiBuilder.Marker m, boolean statement) {
            expect(FUNCTION, "'function'");
            boolean named = atFnName(0);
            if (named) {
                fnName();
            }

            PsiBuilder.Marker paramsStart = mark();
            boolean savedLone = sawLoneParamName;
            sawLoneParamName = false;
            parseParams(statement && named ? ParamMode.DECL : ParamMode.STRICT);
            boolean lone = sawLoneParamName;
            sawLoneParamName = savedLone;
            if (accept(ARROW)) {
                parseType();
            }

            if (inNative && is(LBRACE)) {
                error("a native function cannot have a body: its implementation is written in C");
            }

            if (statement && named && !is(LBRACE)) {
                // a declaration: a lone name in the parameter list is a type
                if (lone) {
                    paramsStart.rollbackTo();
                    parseParams(ParamMode.TYPES);
                    if (accept(ARROW)) {
                        parseType();
                    }
                } else {
                    paramsStart.drop();
                }
                m.done(FUNCTION_DECL);
                endStatement();
                return FUNCTION_DECL;
            }

            paramsStart.drop();
            parseBlock();
            IElementType kind = statement && named ? FUNCTION_DECL : FUNCTION_EXPR;
            m.done(kind);
            return kind;
        }

        private Res parseLambda() {
            PsiBuilder.Marker m = mark();
            if (isIdent(0)) {
                PsiBuilder.Marker prm = mark();
                name("parameter name");
                prm.done(PARAM);
            } else {
                parseParams(ParamMode.STRICT);
            }
            expect(ARROW, "'->'");
            boolean savedNl = noLambda;
            boolean savedPattern = inPattern;
            noLambda = false;
            inPattern = false;
            if (is(LBRACE)) {
                parseBlock();
            } else {
                int savedDepth = depth;
                parseExpression();
                depth = savedDepth;
            }
            noLambda = savedNl;
            inPattern = savedPattern;
            m.done(LAMBDA_EXPR);
            return new Res(m, LAMBDA_EXPR);
        }

        private boolean atLambda() {
            if (noLambda) {
                return false;
            }
            if (isIdent(0)) {
                return at(1) == ARROW && !nl(1);
            }
            if (is(LPAREN)) {
                int close = matchingParen(0);
                return close > 0 && at(close + 1) == ARROW && !nl(close + 1);
            }
            return false;
        }

        private void parsePattern() {
            PsiBuilder.Marker m = mark();
            boolean savedPattern = inPattern;
            boolean savedNl = noLambda;
            inPattern = true;
            noLambda = true;
            parseExpression();
            inPattern = savedPattern;
            noLambda = savedNl;
            m.done(PATTERN);
        }

        private Res parseIf() {
            PsiBuilder.Marker m = mark();
            adv(); // if
            boolean forced = accept(LET) || accept(CONST);
            if (forced) {
                parsePattern();
                expect(ASSIGN, "'=' after the pattern of 'if let'");
                parseExpression();
            } else {
                PsiBuilder.Marker attempt = mark();
                parseExpression();
                if (is(ASSIGN)) {
                    attempt.rollbackTo();
                    parsePattern();
                    adv(); // =
                    parseExpression();
                } else {
                    attempt.drop();
                }
            }
            parseBlock();
            if (accept(ELSE)) {
                if (is(IF)) {
                    parseIf();
                } else {
                    parseBlock();
                }
            }
            m.done(IF_EXPR);
            return new Res(m, IF_EXPR);
        }

        private Res parseMatch() {
            PsiBuilder.Marker m = mark();
            adv(); // match
            parseExpression();
            if (!expect(LBRACE, "'{' to open the match arms")) {
                m.done(MATCH_EXPR);
                return new Res(m, MATCH_EXPR);
            }
            int saved = depth;
            depth = 0;
            while (!is(RBRACE) && !eof()) {
                if (accept(COMMA) || accept(SEMI)) {
                    continue;
                }
                int before = b.getCurrentOffset();
                PsiBuilder.Marker arm = mark();
                parsePattern();
                if (accept(IF)) {
                    boolean savedNl = noLambda;
                    noLambda = true;
                    parseExpression();
                    noLambda = savedNl;
                }
                if (expect(ARROW, "'->' after the match pattern")) {
                    if (is(LBRACE)) {
                        parseBlock();
                    } else {
                        parseExpression();
                    }
                }
                arm.done(MATCH_ARM);
                if (b.getCurrentOffset() == before) {
                    errorAdvance("unexpected " + desc(0));
                } else if (!is(RBRACE) && !is(COMMA) && !is(SEMI) && !nl(0) && !eof()) {
                    skipToLineEnd("expected a new match arm (on a new line or after ',') but found " + desc(0));
                }
            }
            if (!accept(RBRACE)) {
                error("unterminated match (missing '}')");
            }
            depth = saved;
            m.done(MATCH_EXPR);
            return new Res(m, MATCH_EXPR);
        }

        private boolean startsJuxtaposedArg() {
            if (nl(0)) {
                return false;
            }
            IElementType t = at(0);
            if (t == IDENT || t == INT || t == FLOAT || t == STRING || t == TRUE || t == FALSE || t == FUNCTION) {
                return true;
            }
            return t == LBRACKET && space(0);
        }

        private boolean isObjectKey(IElementType t) {
            return t == STRING || t == IDENT || PipoTokenTypes.HARD_KEYWORDS.contains(t);
        }

        private Res parsePrimary() {
            IElementType t = at(0);
            if ((t == IDENT || t == LPAREN) && atLambda()) {
                return parseLambda();
            }
            if (t == INT || t == FLOAT || t == STRING || t == TRUE || t == FALSE) {
                PsiBuilder.Marker m = mark();
                adv();
                m.done(LITERAL);
                return new Res(m, LITERAL);
            }
            if (t == IDENT) {
                PsiBuilder.Marker m = mark();
                if (inPattern && !(at(1) == LPAREN && !nl(1)) && !(at(1) == COLON && !nl(1))) {
                    if (text(0).equals("_")) {
                        adv();
                        m.done(WILDCARD);
                        return new Res(m, WILDCARD);
                    }
                    PsiBuilder.Marker n = mark();
                    adv();
                    n.done(NAME);
                    m.done(PATTERN_BINDING);
                    return new Res(m, PATTERN_BINDING);
                }
                adv();
                if (startsJuxtaposedArg()) {
                    // unary syntax: `success value` == success(value)
                    m.done(REF_EXPR);
                    PsiBuilder.Marker c = m.precede();
                    parseExpression();
                    c.done(CALL_EXPR);
                    return new Res(c, CALL_EXPR);
                }
                m.done(REF_EXPR);
                return new Res(m, REF_EXPR);
            }
            if (t == LPAREN) {
                PsiBuilder.Marker m = mark();
                adv();
                depth++;
                parseExpression();
                expect(RPAREN, "')'");
                depth--;
                m.done(PAREN_EXPR);
                return new Res(m, PAREN_EXPR);
            }
            if (t == LBRACKET) {
                PsiBuilder.Marker m = mark();
                adv();
                depth++;
                while (!is(RBRACKET) && !eof()) {
                    int before = b.getCurrentOffset();
                    parseExpression();
                    if (b.getCurrentOffset() == before || !accept(COMMA)) {
                        break;
                    }
                }
                expect(RBRACKET, "']' to close the array");
                depth--;
                m.done(ARRAY_EXPR);
                return new Res(m, ARRAY_EXPR);
            }
            if (t == LBRACE) {
                return parseObject();
            }
            if (t == FUNCTION) {
                PsiBuilder.Marker m = mark();
                IElementType kind = parseFunction(m, false);
                return new Res(m, kind);
            }
            if (t == IF) {
                return parseIf();
            }
            if (t == MATCH) {
                return parseMatch();
            }

            if (isCloser(t) || nl(0)) {
                error("expected an expression but found " + desc(0));
            } else {
                errorAdvance("unexpected " + desc(0));
            }
            return null;
        }

        private Res parseObject() {
            PsiBuilder.Marker m = mark();
            adv(); // {
            depth++;
            while (!is(RBRACE) && !eof()) {
                IElementType k = at(0);
                if (!isObjectKey(k)) {
                    error("expected an object key but found " + desc(0));
                    break;
                }
                PsiBuilder.Marker e = mark();
                if (k == IDENT && (at(1) == COMMA || at(1) == RBRACE)) {
                    // shorthand { name }
                    PsiBuilder.Marker r = mark();
                    adv();
                    r.done(REF_EXPR);
                } else {
                    adv();
                    if (accept(COLON)) {
                        parseExpression();
                    } else {
                        error("expected ':' after object key");
                    }
                }
                e.done(OBJECT_ENTRY);
                if (!accept(COMMA)) {
                    break;
                }
            }
            expect(RBRACE, "'}' to close the object");
            depth--;
            m.done(OBJECT_EXPR);
            return new Res(m, OBJECT_EXPR);
        }

        private Res parsePostfix(Res e) {
            if (e == null || e.kind == IF_EXPR || e.kind == MATCH_EXPR) {
                return e;
            }
            PsiBuilder.Marker left = e.m;
            IElementType kind = e.kind;
            for (; ; ) {
                IElementType t = at(0);
                if (t == LPAREN && !nl(0)) {
                    PsiBuilder.Marker c = left.precede();
                    parseArgs();
                    c.done(CALL_EXPR);
                    left = c;
                    kind = CALL_EXPR;
                    continue;
                }
                if (t == LBRACE && !nl(0) && !space(0) && (kind == REF_EXPR || kind == FIELD_EXPR)) {
                    // record construction shorthand: Rect{ ... } == Rect({ ... })
                    PsiBuilder.Marker c = left.precede();
                    parseObject();
                    c.done(CALL_EXPR);
                    left = c;
                    kind = CALL_EXPR;
                    continue;
                }
                if (t == LBRACKET && !nl(0) && !space(0)) {
                    PsiBuilder.Marker ix = left.precede();
                    adv();
                    depth++;
                    if (is(RBRACKET)) {
                        error("expected an index between '[' and ']'");
                    } else {
                        do {
                            int before = b.getCurrentOffset();
                            parseExpression();
                            if (b.getCurrentOffset() == before) {
                                break;
                            }
                        } while (accept(COMMA) && !is(RBRACKET));
                    }
                    expect(RBRACKET, "']'");
                    depth--;
                    ix.done(INDEX_EXPR);
                    left = ix;
                    kind = INDEX_EXPR;
                    continue;
                }
                if (t == COLON && !nl(0)) {
                    // attribute access: object:field, module:symbol, Enum:VARIANT
                    PsiBuilder.Marker f = left.precede();
                    adv();
                    if (atFnName(0)) {
                        takeFnName();
                    } else {
                        error("expected an attribute name after ':' but found " + desc(0));
                    }
                    f.done(FIELD_EXPR);
                    left = f;
                    kind = FIELD_EXPR;
                    continue;
                }
                if (t == DOT) {
                    // dot syntax: a.f(b) == f(a, b), a.mod:f(b) == mod:f(a, b)
                    PsiBuilder.Marker mc = left.precede();
                    adv();
                    if (!atFnName(0)) {
                        error("expected a function name after '.' but found " + desc(0));
                        mc.done(METHOD_CALL_EXPR);
                        return new Res(mc, METHOD_CALL_EXPR);
                    }
                    PsiBuilder.Marker callee = mark();
                    String nm = text(0);
                    takeFnName();
                    callee.done(REF_EXPR);
                    while (is(COLON) && !nl(0)) {
                        PsiBuilder.Marker f = callee.precede();
                        adv();
                        if (atFnName(0)) {
                            nm = text(0);
                            takeFnName();
                        } else {
                            error("expected an attribute name after ':' but found " + desc(0));
                        }
                        f.done(FIELD_EXPR);
                        callee = f;
                    }
                    if (is(LPAREN) && !nl(0)) {
                        parseArgs();
                    } else {
                        error("'.' calls a function, expected '(' after '." + nm + "' (use ':' to access an attribute)");
                    }
                    mc.done(METHOD_CALL_EXPR);
                    left = mc;
                    kind = METHOD_CALL_EXPR;
                    continue;
                }
                break;
            }
            return new Res(left, kind);
        }

        private boolean atValueEnd(int n) {
            IElementType t = at(n);
            return t == COMMA || t == RPAREN || t == RBRACKET || t == RBRACE || t == null || t == SEMI || nl(n);
        }

        private Res parseUnary() {
            IElementType t = at(0);
            if (t == OP) {
                PsiBuilder.Marker m = mark();
                PsiBuilder.Marker r = mark();
                adv();
                r.done(REF_EXPR);
                if (atValueEnd(0)) {
                    m.drop(); // operator used as a value: reduce(xs, +, 0)
                    return new Res(r, REF_EXPR);
                }
                if (is(LPAREN) && !space(0)) {
                    // classic call of an operator: +(a, b)
                    m.drop();
                    PsiBuilder.Marker c = r.precede();
                    parseArgs();
                    c.done(CALL_EXPR);
                    return parsePostfix(new Res(c, CALL_EXPR));
                }
                parseUnary();
                m.done(PREFIX_EXPR);
                return new Res(m, PREFIX_EXPR);
            }

            if (t == IN) {
                // `in` is a keyword but also a function: in(3, 0...3), or a value: any(xs, in)
                boolean call = at(1) == LPAREN && !space(1);
                boolean value = atValueEnd(1);
                if (call || value) {
                    PsiBuilder.Marker r = mark();
                    adv();
                    r.done(REF_EXPR);
                    if (value) {
                        return parsePostfix(new Res(r, REF_EXPR));
                    }
                    PsiBuilder.Marker c = r.precede();
                    parseArgs();
                    c.done(CALL_EXPR);
                    return parsePostfix(new Res(c, CALL_EXPR));
                }
                errorAdvance("'in' needs a left operand (x in xs) or call parentheses with no space (in(x, xs))");
                return null;
            }

            return parsePostfix(parsePrimary());
        }

        /* an infix operator continues the expression: any operator token, or `in` */
        private boolean atInfix() {
            IElementType t = at(0);
            return (t == OP || t == IN) && (!nl(0) || depth > 0);
        }

        private Res parseExpression() {
            PsiBuilder.Marker chain = mark();
            Res first = parseUnary();
            if (!atInfix()) {
                chain.drop();
                return first;
            }
            while (atInfix()) {
                PsiBuilder.Marker r = mark();
                adv();
                r.done(REF_EXPR);
                parseUnary();
            }
            chain.done(CHAIN_EXPR);
            return new Res(chain, CHAIN_EXPR);
        }

        /* --------------------------------------------------------- statements */

        private void parseBlock() {
            PsiBuilder.Marker m = mark();
            if (!expect(LBRACE, "'{'")) {
                m.drop();
                return;
            }
            int saved = depth;
            depth = 0;
            blockDepth++;
            while (!is(RBRACE)) {
                if (eof()) {
                    error("unterminated block (missing '}')");
                    break;
                }
                if (accept(SEMI)) {
                    continue;
                }
                int before = b.getCurrentOffset();
                parseStatement();
                if (b.getCurrentOffset() == before) {
                    errorAdvance("unexpected " + desc(0));
                }
            }
            accept(RBRACE);
            depth = saved;
            blockDepth--;
            m.done(BLOCK);
        }

        /** skips the rest of a broken statement, keeping brackets balanced */
        private void skipToLineEnd(String msg) {
            PsiBuilder.Marker err = mark();
            int d = 0;
            boolean first = true;
            while (!eof()) {
                IElementType t = at(0);
                if (!first && d == 0 && (nl(0) || t == SEMI || t == RBRACE)) {
                    break;
                }
                if (first && d == 0 && (t == RBRACE)) {
                    break;
                }
                if (t == LPAREN || t == LBRACKET || t == LBRACE) {
                    d++;
                } else if (t == RPAREN || t == RBRACKET || t == RBRACE) {
                    if (d == 0) {
                        break;
                    }
                    d--;
                }
                adv();
                first = false;
            }
            err.error(msg);
        }

        private void endStatement() {
            if (is(SEMI)) {
                while (accept(SEMI)) {
                    // ;;
                }
                return;
            }
            if (isTerminator(0)) {
                return;
            }
            int p = pos();
            if (p > 0 && types[p - 1] == RBRACE) {
                return;
            }
            skipToLineEnd("expected the end of the statement (a newline or ';') before " + desc(0));
        }

        private void parseLet(PsiBuilder.Marker m) {
            adv(); // let / const
            if (isIdent(0) && (at(1) == ASSIGN || isTerminator(1))) {
                name("variable name");
            } else {
                parseType();
                noInlineFnType();
                name("variable name");
            }
            if (accept(ASSIGN)) {
                parseExpression();
            }
            m.done(VAR_DECL);
        }

        private void parseImport(PsiBuilder.Marker m) {
            adv(); // import
            if (blockDepth > 0) {
                error("'import' is only allowed at the top of a file, before any other statement");
            }
            PsiBuilder.Marker path = mark();
            if (is(STRING)) {
                adv();
            } else if (isIdent(0)) {
                adv();
            } else {
                error("expected module name or a \"./path\" string but found " + desc(0));
            }
            while (is(COLON) && !nl(0)) {
                adv();
                if (isIdent(0)) {
                    adv();
                } else {
                    error("expected submodule name after ':' but found " + desc(0));
                    break;
                }
            }
            path.done(MODULE_PATH);

            if (accept(ARROW)) {
                if (is(LBRACE)) {
                    PsiBuilder.Marker list = mark();
                    adv();
                    while (!is(RBRACE) && !eof()) {
                        if (!atFnName(0)) {
                            error("expected a name to import but found " + desc(0));
                            break;
                        }
                        PsiBuilder.Marker in = mark();
                        fnName();
                        in.done(IMPORT_NAME);
                        if (!accept(COMMA)) {
                            break;
                        }
                    }
                    expect(RBRACE, "'}'");
                    list.done(IMPORT_LIST);
                } else {
                    PsiBuilder.Marker a = mark();
                    name("alias name");
                    a.done(IMPORT_ALIAS);
                }
            }
            m.done(IMPORT_STMT);
        }

        private boolean atExportedDecl() {
            IElementType t = at(0);
            if (t == FUNCTION || t == GLOBAL || t == CONST || t == LET || t == ENUM) {
                return true;
            }
            if (t == OP) {
                return isGenericPrefix();
            }
            if (t == IDENT) {
                if (atNative()) {
                    return true;
                }
                return (word(0, "type") || word(0, "interface")) && at(1) == IDENT && !nl(1);
            }
            return false;
        }

        private void parseExport(PsiBuilder.Marker m) {
            adv(); // export
            if (blockDepth > 0) {
                error("'export' is only allowed at the top level of a file");
            }
            if (is(IMPORT)) {
                // export import "./fs" == import "./fs" + export fs
                parseImport(m);
                return;
            }
            if (atExportedDecl()) {
                parseStatement();
                m.done(EXPORT_STMT);
                return;
            }

            boolean braces = accept(LBRACE);
            do {
                if (braces && is(RBRACE)) {
                    break;
                }
                if (!atFnName(0)) {
                    error("expected a name to export but found " + desc(0));
                    break;
                }
                PsiBuilder.Marker item = mark();
                PsiBuilder.Marker r = mark();
                takeFnName();
                r.done(REF_EXPR);
                if (accept(ARROW)) {
                    fnName();
                }
                item.done(EXPORT_ITEM);
            } while (braces && accept(COMMA));
            if (braces) {
                expect(RBRACE, "'}'");
            }
            m.done(EXPORT_STMT);
        }

        private void parseFor(PsiBuilder.Marker m) {
            adv(); // for
            if (!accept(LET)) {
                accept(CONST);
            }
            PsiBuilder.Marker v = mark();
            if (name("loop variable name")) {
                v.done(FOR_VAR);
            } else {
                v.drop();
            }
            if (accept(COMMA)) {
                PsiBuilder.Marker v2 = mark();
                if (name("second loop variable name")) {
                    v2.done(FOR_VAR);
                } else {
                    v2.drop();
                }
            }
            expect(IN, "'in'");
            parseExpression();
            parseBlock();
            m.done(FOR_STMT);
        }

        private void parseEnum(PsiBuilder.Marker m) {
            adv(); // enum
            if (isIdent(0) && at(1) == IDENT) {
                parseType(); // value type: enum string Messages { ... }
            }
            name("enum name");
            if (opStarts(0, '<') && op(0, "<")) {
                parseGenericParams(true);
            }
            if (!expect(LBRACE, "'{'")) {
                m.done(ENUM_DECL);
                return;
            }
            while (!is(RBRACE) && !eof()) {
                if (accept(COMMA) || accept(SEMI)) {
                    continue;
                }
                PsiBuilder.Marker v = mark();
                if (!isIdent(0)) {
                    v.drop();
                    errorAdvance("expected variant name but found " + desc(0));
                    continue;
                }
                name("variant name");
                if (accept(LPAREN)) {
                    while (!is(RPAREN) && !eof()) {
                        if (parseType() == null || !accept(COMMA)) {
                            break;
                        }
                    }
                    expect(RPAREN, "')'");
                }
                if (accept(ASSIGN)) {
                    parseExpression();
                }
                v.done(ENUM_VARIANT);
            }
            expect(RBRACE, "'}'");
            m.done(ENUM_DECL);
        }

        /* type Name = ..., type Name<T> = ...; the current token is `type` */
        private void parseTypeDecl(PsiBuilder.Marker m) {
            advAs(TYPE_KW);
            String n = text(0);
            if (n.equals("tuple") || PipoBuiltinNames.PRIMITIVES.contains(n)) {
                error("cannot redefine the built-in type '" + n + "'");
            }
            name("type name");
            if (is(OP)) {
                parseGenericParams(false);
            }
            if (expect(ASSIGN, "'='")) {
                parseTypeDef();
            }
            m.done(TYPE_DECL);
        }

        private void parseInterface(PsiBuilder.Marker m) {
            advAs(INTERFACE_KW);
            name("interface name");
            if (is(OP)) {
                parseGenericParams(false);
            }
            if (!expect(LBRACE, "'{' to open the interface")) {
                m.done(INTERFACE_DECL);
                return;
            }
            while (!is(RBRACE) && !eof()) {
                if (accept(SEMI) || accept(COMMA)) {
                    continue;
                }
                int before = b.getCurrentOffset();
                if (atImpl()) {
                    PsiBuilder.Marker e = mark();
                    advAs(IMPL_KW);
                    parseType();
                    expect(FOR, "'for'");
                    parseType();
                    e.done(INTERFACE_EXTENDS);
                    continue;
                }

                PsiBuilder.Marker r = mark();
                if (isGenericPrefix()) {
                    parseGenericParams(false);
                }
                if (!is(FUNCTION)) {
                    r.drop();
                    skipToLineEnd("expected 'function' (an interface lists the functions a type must have) but found "
                            + desc(0));
                    if (b.getCurrentOffset() == before) {
                        errorAdvance("unexpected " + desc(0));
                    }
                    continue;
                }
                adv();
                fnName();
                parseParams(ParamMode.TYPES);
                if (accept(ARROW)) {
                    parseType();
                }
                if (is(LBRACE)) {
                    parseBlock();
                }
                r.done(FUNCTION_DECL);
                if (b.getCurrentOffset() == before) {
                    errorAdvance("unexpected " + desc(0));
                }
            }
            expect(RBRACE, "'}' to close the interface");
            m.done(INTERFACE_DECL);
        }

        private boolean atImpl() {
            if (!word(0, "impl") || at(1) != IDENT || nl(1)) {
                return false;
            }
            for (int i = 2; ; i++) {
                IElementType x = at(i);
                if (x == null || nl(i)) {
                    return false;
                }
                if (x == FOR) {
                    return true;
                }
                if (!(x == IDENT || x == COLON || x == COMMA || x == OP || x == LBRACKET || x == RBRACKET)) {
                    return false;
                }
            }
        }

        /* `native` is contextual: native function f(...), native "impl" function f(...), native type T */
        private boolean atNative() {
            if (!word(0, "native") || nl(1)) {
                return false;
            }
            if (word(1, "type")) {
                return at(2) == IDENT;
            }
            IElementType a = at(1);
            return a == FUNCTION || a == STRING || (a == OP && text(1).startsWith("<"));
        }

        private void parseNative(PsiBuilder.Marker m) {
            advAs(NATIVE_KW);
            if (blockDepth > 0) {
                error("'native' is only allowed at the top level of a file");
            }
            boolean impl = accept(STRING);
            if (word(0, "type")) {
                if (impl) {
                    error("a native type takes no implementation name");
                }
                advAs(TYPE_KW);
                name("type name after 'native type'");
                if (op(0, "<") && !nl(0)) {
                    parseGenericParams(false);
                }
                if (is(ASSIGN)) {
                    error("a native type cannot have a definition: it is implemented in C");
                }
                m.done(TYPE_DECL);
                return;
            }
            if (isGenericPrefix()) {
                parseGenericParams(false);
            }
            if (!is(FUNCTION) || !atFnName(1)) {
                m.done(FUNCTION_DECL);
                skipToLineEnd("expected a named function declaration after 'native' but found " + desc(0));
                return;
            }
            inNative = true;
            parseFunction(m, true);
            inNative = false;
        }

        private void parseImpl(PsiBuilder.Marker m) {
            advAs(IMPL_KW);
            parseType();
            expect(FOR, "'for'");
            parseType();
            m.done(IMPL_STMT);
        }

        private void parseStatement() {
            PsiBuilder.Marker m = mark();
            if (atNative()) {
                parseNative(m);
                return;
            }

            IElementType t = at(0);
            if (t == IMPORT) {
                parseImport(m);
                endStatement();
                return;
            }
            if (t == EXPORT) {
                parseExport(m);
                endStatement();
                return;
            }
            if (t == GLOBAL) {
                // global function f(...): visible everywhere once the file is loaded
                adv();
                if (blockDepth > 0) {
                    error("'global' is only allowed at the top level of a file");
                }
                if (atNative()) {
                    parseNative(m);
                    return;
                }
                if (isGenericPrefix()) {
                    parseGenericParams(false);
                }
                if (is(FUNCTION) && atFnName(1)) {
                    parseFunction(m, true);
                } else {
                    m.done(FUNCTION_DECL);
                    skipToLineEnd("expected a named function declaration after 'global' but found " + desc(0));
                }
                return;
            }
            if (t == CONST || t == LET) {
                parseLet(m);
                endStatement();
                return;
            }
            if (t == FUNCTION && atFnName(1)) {
                parseFunction(m, true);
                return;
            }
            if (t == OP && isGenericPrefix()) {
                parseGenericParams(false);
                if (word(0, "type")) {
                    parseTypeDecl(m);
                    endStatement();
                    return;
                }
                if (word(0, "interface")) {
                    parseInterface(m);
                    return;
                }
                if (word(0, "impl")) {
                    if (!atImpl()) {
                        error("expected 'impl Interface for Type' after the generic parameter list");
                    }
                    parseImpl(m);
                    endStatement();
                    return;
                }
                if (!is(FUNCTION)) {
                    error("expected 'function' after the generic parameter list");
                    m.done(FUNCTION_DECL);
                    return;
                }
                parseFunction(m, true);
                return;
            }
            if (t == RETURN) {
                adv();
                if (!isTerminator(0)) {
                    parseExpression();
                }
                m.done(RETURN_STMT);
                endStatement();
                return;
            }
            if (t == BREAK || t == CONTINUE) {
                adv();
                m.done(t == BREAK ? BREAK_STMT : CONTINUE_STMT);
                endStatement();
                return;
            }
            if (t == FOR) {
                parseFor(m);
                return;
            }
            if (t == WHILE) {
                adv();
                parseExpression();
                parseBlock();
                m.done(WHILE_STMT);
                return;
            }
            if (t == ENUM) {
                parseEnum(m);
                return;
            }
            if (t == LBRACE) {
                m.drop();
                parseBlock();
                return;
            }
            if (t == IDENT) {
                IElementType a = at(1);
                if (word(0, "type") && a == IDENT && !nl(1) && (at(2) == ASSIGN || op(2, "<"))) {
                    parseTypeDecl(m);
                    endStatement();
                    return;
                }
                if (word(0, "interface") && a == IDENT && !nl(1) && (at(2) == LBRACE || op(2, "<"))) {
                    parseInterface(m);
                    return;
                }
                if (atImpl()) {
                    parseImpl(m);
                    endStatement();
                    return;
                }
                if (word(0, "precedence") && (a == IDENT || a == OP || a == IN) && !nl(1)
                        && (word(2, "over") || word(2, "under") || word(2, "as"))) {
                    advAs(PRECEDENCE_KW);
                    PsiBuilder.Marker r1 = mark();
                    adv();
                    r1.done(REF_EXPR);
                    String rel = text(0);
                    advAs(rel.equals("over") ? OVER_KW : rel.equals("under") ? UNDER_KW : AS_KW);
                    if (atFnName(0) && at(0) != LBRACKET) {
                        PsiBuilder.Marker r2 = mark();
                        adv();
                        r2.done(REF_EXPR);
                    } else {
                        error("expected an operator after '" + rel + "' but found " + desc(0));
                    }
                    m.done(PRECEDENCE_STMT);
                    endStatement();
                    return;
                }
            }

            parseSimpleStatement(m);
        }

        private boolean isAssignable(IElementType k) {
            return k == REF_EXPR || k == FIELD_EXPR || k == INDEX_EXPR;
        }

        /* an expression statement or an assignment: target = value, target op= value */
        private void parseSimpleStatement(PsiBuilder.Marker m) {
            PsiBuilder.Marker attempt = mark();
            Res e = parseUnary();
            boolean opeq = e != null && isAssignable(e.kind) && is(OP) && !nl(0) && text(0).length() >= 2
                    && text(0).endsWith("=") && !text(0).equals("==");
            if (!opeq && !is(ASSIGN) && !is(COMPOUND)) {
                attempt.rollbackTo();
                e = parseExpression();
            } else {
                attempt.drop();
            }

            if (opeq || is(ASSIGN) || is(COMPOUND)) {
                if (e == null || !isAssignable(e.kind)) {
                    error("invalid assignment target (expected a variable, field or index)");
                }
                adv();
                parseExpression();
                m.done(ASSIGN_STMT);
            } else {
                m.done(EXPR_STMT);
            }
            endStatement();
        }
    }
}
