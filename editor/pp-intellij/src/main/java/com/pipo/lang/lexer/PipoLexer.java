package com.pipo.lang.lexer;

import com.intellij.lexer.LexerBase;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * A port of src/lexer.c. It never fails: malformed input becomes BAD_CHARACTER
 * or an unterminated STRING / BLOCK_COMMENT, which the annotator reports.
 * <p>
 * Like the interpreter, it splits a run of operator characters into the
 * operators it knows: the built-in ones plus the ones the file declares with
 * {@code function <op>} or {@code precedence <op> over <op>}.
 */
public final class PipoLexer extends LexerBase {
    private static final String[] BUILTIN_OPS = {
            "+", "-", "*", "/", "%", "**", "==", "!=", "<", ">", "<=", ">=", "!", "#", "...", "...=", "&&", "||", "?",
    };
    private static final String OP_CHARS = "+-*/%<>=!&|^~#?@$\\";

    private CharSequence buffer = "";
    private int bufferEnd;

    private int tokenStart;
    private int tokenEnd;
    private IElementType tokenType;

    /* pieces of a split operator run that are still to be returned */
    private final List<int[]> pieces = new ArrayList<>();
    private final List<IElementType> pieceTypes = new ArrayList<>();
    private int pieceIndex;
    private boolean continuation;

    private Set<String> knownOps = new HashSet<>();
    private CharSequence knownOpsFor;

    /* result of rawToken() */
    private IElementType rawType;

    @Override
    public void start(@NotNull CharSequence buffer, int startOffset, int endOffset, int initialState) {
        this.buffer = buffer;
        this.bufferEnd = endOffset;
        if (knownOpsFor != buffer) {
            knownOps = collectKnownOps(buffer);
            knownOpsFor = buffer;
        }

        pieces.clear();
        pieceTypes.clear();
        pieceIndex = 0;
        tokenEnd = startOffset;
        advance();
    }

    @Override
    public int getState() {
        // the second and later pieces of a split operator run: the highlighter must not restart there
        return continuation ? 1 : 0;
    }

    @Override
    public IElementType getTokenType() {
        return tokenType;
    }

    @Override
    public int getTokenStart() {
        return tokenStart;
    }

    @Override
    public int getTokenEnd() {
        return tokenEnd;
    }

    @Override
    public void advance() {
        if (pieceIndex < pieces.size()) {
            int[] piece = pieces.get(pieceIndex);
            tokenType = pieceTypes.get(pieceIndex);
            tokenStart = piece[0];
            tokenEnd = piece[1];
            continuation = pieceIndex > 0;
            pieceIndex++;
            return;
        }

        pieces.clear();
        pieceTypes.clear();
        pieceIndex = 0;
        continuation = false;

        tokenStart = tokenEnd;
        if (tokenStart >= bufferEnd) {
            tokenType = null;
            return;
        }

        int end = rawToken(buffer, tokenStart, bufferEnd);
        if (rawType == PipoTokenTypes.OP) {
            split(tokenStart, end);
            advance();
            return;
        }

        tokenType = rawType;
        tokenEnd = end;
    }

    @Override
    public @NotNull CharSequence getBufferSequence() {
        return buffer;
    }

    @Override
    public int getBufferEnd() {
        return bufferEnd;
    }

    /* ------------------------------------------------------------ raw tokens */

    static boolean isOpChar(char c) {
        return OP_CHARS.indexOf(c) >= 0;
    }

    public static boolean isIdentStart(char c) {
        return c >= 0x80 ? c != 0xFEFF : (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    public static boolean isIdentChar(char c) {
        return isIdentStart(c) || (c >= '0' && c <= '9');
    }

    private static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    private static boolean isHex(char c) {
        return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    private static char at(CharSequence s, int i, int end) {
        return i < end ? s.charAt(i) : '\0';
    }

    /** Lexes one unsplit token at p, sets rawType and returns its end. */
    private int rawToken(CharSequence s, int p, int end) {
        char c = s.charAt(p);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0x0B || c == '﻿') {
            while (p < end) {
                char w = s.charAt(p);
                if (!(w == ' ' || w == '\t' || w == '\r' || w == '\n' || w == '\f' || w == 0x0B || w == '﻿')) {
                    break;
                }
                p++;
            }
            rawType = PipoTokenTypes.WHITE_SPACE;
            return p;
        }

        char c1 = at(s, p + 1, end);
        if (c == '/' && c1 == '/') {
            while (p < end && s.charAt(p) != '\n') {
                p++;
            }
            rawType = PipoTokenTypes.LINE_COMMENT;
            return p;
        }

        if (c == '/' && c1 == '*') {
            p += 2;
            while (p < end && !(s.charAt(p) == '*' && at(s, p + 1, end) == '/')) {
                p++;
            }
            rawType = PipoTokenTypes.BLOCK_COMMENT;
            return Math.min(end, p + 2);
        }

        if (isDigit(c)) {
            return lexNumber(s, p, end);
        }

        if (isIdentStart(c)) {
            int start = p;
            while (p < end && isIdentChar(s.charAt(p))) {
                p++;
            }
            IElementType kw = PipoTokenTypes.KEYWORDS.get(s.subSequence(start, p).toString());
            rawType = kw != null ? kw : PipoTokenTypes.IDENT;
            return p;
        }

        if (c == '"' || c == '\'') {
            p++;
            while (p < end) {
                char x = s.charAt(p);
                if (x == c) {
                    p++;
                    break;
                }
                p += x == '\\' ? 2 : 1;
            }
            rawType = PipoTokenTypes.STRING;
            return Math.min(p, end);
        }

        if (c == '.' && c1 != '.') {
            rawType = PipoTokenTypes.DOT;
            return p + 1;
        }

        if (isOpChar(c) || c == '.') {
            int start = p;
            while (p < end) {
                char x = s.charAt(p);
                char nx = at(s, p + 1, end);
                if (x == '/' && (nx == '/' || nx == '*')) {
                    break;
                }
                if (isOpChar(x)) {
                    p++;
                    continue;
                }
                if (x == '.' && (nx == '.' || (p > start && s.charAt(p - 1) == '.'))) {
                    p++;
                    continue;
                }
                break;
            }
            rawType = PipoTokenTypes.OP;
            return p;
        }

        rawType = switch (c) {
            case '(' -> PipoTokenTypes.LPAREN;
            case ')' -> PipoTokenTypes.RPAREN;
            case '[' -> PipoTokenTypes.LBRACKET;
            case ']' -> PipoTokenTypes.RBRACKET;
            case '{' -> PipoTokenTypes.LBRACE;
            case '}' -> PipoTokenTypes.RBRACE;
            case ',' -> PipoTokenTypes.COMMA;
            case ':' -> PipoTokenTypes.COLON;
            case ';' -> PipoTokenTypes.SEMI;
            default -> PipoTokenTypes.BAD_CHARACTER;
        };
        return p + 1;
    }

    private int lexNumber(CharSequence s, int p, int end) {
        char c1 = at(s, p + 1, end);
        boolean isFloat = false;
        if (s.charAt(p) == '0' && (c1 == 'x' || c1 == 'X')) {
            p += 2;
            while (p < end && (isHex(s.charAt(p)) || s.charAt(p) == '_')) {
                p++;
            }
        } else if (s.charAt(p) == '0' && (c1 == 'b' || c1 == 'B')) {
            p += 2;
            while (p < end && (s.charAt(p) == '0' || s.charAt(p) == '1' || s.charAt(p) == '_')) {
                p++;
            }
        } else {
            while (p < end && (isDigit(s.charAt(p)) || s.charAt(p) == '_')) {
                p++;
            }
            if (at(s, p, end) == '.' && isDigit(at(s, p + 1, end))) {
                isFloat = true;
                p++;
                while (p < end && (isDigit(s.charAt(p)) || s.charAt(p) == '_')) {
                    p++;
                }
            }
            char e = at(s, p, end);
            char e1 = at(s, p + 1, end);
            if ((e == 'e' || e == 'E') && (isDigit(e1) || ((e1 == '+' || e1 == '-') && isDigit(at(s, p + 2, end))))) {
                isFloat = true;
                p += 2;
                while (p < end && isDigit(s.charAt(p))) {
                    p++;
                }
            }
        }
        rawType = isFloat ? PipoTokenTypes.FLOAT : PipoTokenTypes.INT;
        return p;
    }

    /* ------------------------------------------------------- operator splits */

    private static boolean isReserved(CharSequence s) {
        return (s.length() == 1 && s.charAt(0) == '=') || (s.length() == 2 && s.charAt(0) == '-' && s.charAt(1) == '>');
    }

    private boolean isKnown(CharSequence s) {
        return knownOps.contains(s.toString());
    }

    private void emit(int start, int end, IElementType type) {
        pieces.add(new int[]{start, end});
        pieceTypes.add(type);
    }

    /* split_op() in lexer.c */
    private void split(int s, int e) {
        int n = e - s;
        int pos = 0;
        boolean first = true;
        while (pos < n) {
            int rem = n - pos;
            int take = 0;
            CharSequence rest = buffer.subSequence(s + pos, e);
            if (isReserved(rest) || isKnown(rest)) {
                take = rem;
            } else if (first && n >= 2 && buffer.charAt(e - 1) == '=' && buffer.charAt(s) != '='
                    && isKnown(buffer.subSequence(s, e - 1))) {
                emit(s, e, PipoTokenTypes.COMPOUND);
                return;
            } else {
                for (int len = rem - 1; len >= 1; len--) {
                    CharSequence part = buffer.subSequence(s + pos, s + pos + len);
                    if (isReserved(part) || isKnown(part)) {
                        take = len;
                        break;
                    }
                }
                if (take == 0) {
                    take = rem;
                }
            }

            CharSequence piece = buffer.subSequence(s + pos, s + pos + take);
            IElementType type = PipoTokenTypes.OP;
            if (isReserved(piece)) {
                type = take == 1 ? PipoTokenTypes.ASSIGN : PipoTokenTypes.ARROW;
            }
            emit(s + pos, s + pos + take, type);
            pos += take;
            first = false;
        }
    }

    /** The built-in operators plus those the file declares (function <op>, precedence <op> over <op>). */
    private Set<String> collectKnownOps(CharSequence s) {
        Set<String> ops = new HashSet<>(List.of(BUILTIN_OPS));
        int end = s.length();
        List<IElementType> types = new ArrayList<>();
        List<int[]> ranges = new ArrayList<>();
        int p = 0;
        while (p < end) {
            int e = rawToken(s, p, end);
            if (rawType != PipoTokenTypes.WHITE_SPACE && rawType != PipoTokenTypes.LINE_COMMENT
                    && rawType != PipoTokenTypes.BLOCK_COMMENT) {
                types.add(rawType);
                ranges.add(new int[]{p, e});
            }
            p = Math.max(e, p + 1);
        }

        for (int i = 0; i + 1 < types.size(); i++) {
            if (types.get(i) == PipoTokenTypes.FUNCTION && types.get(i + 1) == PipoTokenTypes.OP) {
                ops.add(text(s, ranges.get(i + 1)));
            }
            if (types.get(i) == PipoTokenTypes.IDENT && "precedence".equals(text(s, ranges.get(i)))) {
                if (types.get(i + 1) == PipoTokenTypes.OP) {
                    ops.add(text(s, ranges.get(i + 1)));
                }
                if (i + 3 < types.size() && types.get(i + 3) == PipoTokenTypes.OP) {
                    ops.add(text(s, ranges.get(i + 3)));
                }
            }
        }
        return ops;
    }

    private static String text(CharSequence s, int[] range) {
        return s.subSequence(range[0], range[1]).toString();
    }
}
