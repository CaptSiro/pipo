package com.pipo.lang.highlight;

import com.intellij.lexer.Lexer;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighter;
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase;
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.lexer.PipoLexer;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.HashMap;
import java.util.Map;

import static com.pipo.lang.lexer.PipoTokenTypes.*;

/** Lexer-level colors. Names (functions, types, parameters, ...) are colored by PipoAnnotator. */
public final class PipoSyntaxHighlighter extends SyntaxHighlighterBase {
    private static final Map<IElementType, TextAttributesKey> COLORS = new HashMap<>();

    static {
        for (IElementType k : KEYWORDS_SET.getTypes()) {
            COLORS.put(k, PipoColors.KEYWORD);
        }
        COLORS.put(INT, PipoColors.NUMBER);
        COLORS.put(FLOAT, PipoColors.NUMBER);
        COLORS.put(STRING, PipoColors.STRING);
        COLORS.put(LINE_COMMENT, PipoColors.LINE_COMMENT);
        COLORS.put(BLOCK_COMMENT, PipoColors.BLOCK_COMMENT);
        COLORS.put(OP, PipoColors.OPERATOR);
        COLORS.put(ASSIGN, PipoColors.OPERATOR);
        COLORS.put(COMPOUND, PipoColors.OPERATOR);
        COLORS.put(ARROW, PipoColors.ARROW);
        COLORS.put(LPAREN, PipoColors.PARENTHESES);
        COLORS.put(RPAREN, PipoColors.PARENTHESES);
        COLORS.put(LBRACKET, PipoColors.BRACKETS);
        COLORS.put(RBRACKET, PipoColors.BRACKETS);
        COLORS.put(LBRACE, PipoColors.BRACES);
        COLORS.put(RBRACE, PipoColors.BRACES);
        COLORS.put(COMMA, PipoColors.COMMA);
        COLORS.put(SEMI, PipoColors.SEMICOLON);
        COLORS.put(DOT, PipoColors.DOT);
        COLORS.put(COLON, PipoColors.COLON);
        COLORS.put(IDENT, PipoColors.IDENTIFIER);
        COLORS.put(BAD_CHARACTER, PipoColors.BAD_CHARACTER);
    }

    @Override
    public @NotNull Lexer getHighlightingLexer() {
        return new PipoLexer();
    }

    @Override
    public TextAttributesKey @NotNull [] getTokenHighlights(IElementType tokenType) {
        return pack(COLORS.get(tokenType));
    }

    public static final class Factory extends SyntaxHighlighterFactory {
        @Override
        public @NotNull SyntaxHighlighter getSyntaxHighlighter(@Nullable Project project, @Nullable VirtualFile file) {
            return new PipoSyntaxHighlighter();
        }
    }
}
