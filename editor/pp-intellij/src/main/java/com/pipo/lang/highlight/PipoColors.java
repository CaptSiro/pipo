package com.pipo.lang.highlight;

import com.intellij.openapi.editor.DefaultLanguageHighlighterColors;
import com.intellij.openapi.editor.HighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;

import static com.intellij.openapi.editor.colors.TextAttributesKey.createTextAttributesKey;

/** Color keys; each falls back to a standard key so every color scheme works out of the box. */
public final class PipoColors {
    public static final TextAttributesKey KEYWORD =
            createTextAttributesKey("PIPO_KEYWORD", DefaultLanguageHighlighterColors.KEYWORD);
    public static final TextAttributesKey NUMBER =
            createTextAttributesKey("PIPO_NUMBER", DefaultLanguageHighlighterColors.NUMBER);
    public static final TextAttributesKey STRING =
            createTextAttributesKey("PIPO_STRING", DefaultLanguageHighlighterColors.STRING);
    public static final TextAttributesKey VALID_ESCAPE =
            createTextAttributesKey("PIPO_VALID_ESCAPE", DefaultLanguageHighlighterColors.VALID_STRING_ESCAPE);
    public static final TextAttributesKey INVALID_ESCAPE =
            createTextAttributesKey("PIPO_INVALID_ESCAPE", DefaultLanguageHighlighterColors.INVALID_STRING_ESCAPE);
    public static final TextAttributesKey FORMAT_SPEC =
            createTextAttributesKey("PIPO_FORMAT_SPEC", DefaultLanguageHighlighterColors.VALID_STRING_ESCAPE);
    public static final TextAttributesKey LINE_COMMENT =
            createTextAttributesKey("PIPO_LINE_COMMENT", DefaultLanguageHighlighterColors.LINE_COMMENT);
    public static final TextAttributesKey BLOCK_COMMENT =
            createTextAttributesKey("PIPO_BLOCK_COMMENT", DefaultLanguageHighlighterColors.BLOCK_COMMENT);
    public static final TextAttributesKey OPERATOR =
            createTextAttributesKey("PIPO_OPERATOR", DefaultLanguageHighlighterColors.OPERATION_SIGN);
    public static final TextAttributesKey ARROW =
            createTextAttributesKey("PIPO_ARROW", DefaultLanguageHighlighterColors.OPERATION_SIGN);
    public static final TextAttributesKey PARENTHESES =
            createTextAttributesKey("PIPO_PARENTHESES", DefaultLanguageHighlighterColors.PARENTHESES);
    public static final TextAttributesKey BRACKETS =
            createTextAttributesKey("PIPO_BRACKETS", DefaultLanguageHighlighterColors.BRACKETS);
    public static final TextAttributesKey BRACES =
            createTextAttributesKey("PIPO_BRACES", DefaultLanguageHighlighterColors.BRACES);
    public static final TextAttributesKey COMMA =
            createTextAttributesKey("PIPO_COMMA", DefaultLanguageHighlighterColors.COMMA);
    public static final TextAttributesKey SEMICOLON =
            createTextAttributesKey("PIPO_SEMICOLON", DefaultLanguageHighlighterColors.SEMICOLON);
    public static final TextAttributesKey DOT =
            createTextAttributesKey("PIPO_DOT", DefaultLanguageHighlighterColors.DOT);
    public static final TextAttributesKey COLON =
            createTextAttributesKey("PIPO_COLON", DefaultLanguageHighlighterColors.DOT);
    public static final TextAttributesKey IDENTIFIER =
            createTextAttributesKey("PIPO_IDENTIFIER", DefaultLanguageHighlighterColors.IDENTIFIER);
    public static final TextAttributesKey BAD_CHARACTER =
            createTextAttributesKey("PIPO_BAD_CHARACTER", HighlighterColors.BAD_CHARACTER);

    // semantic (annotator)
    public static final TextAttributesKey FUNCTION_DECLARATION =
            createTextAttributesKey("PIPO_FUNCTION_DECLARATION", DefaultLanguageHighlighterColors.FUNCTION_DECLARATION);
    public static final TextAttributesKey FUNCTION_CALL =
            createTextAttributesKey("PIPO_FUNCTION_CALL", DefaultLanguageHighlighterColors.FUNCTION_CALL);
    public static final TextAttributesKey OPERATOR_DECLARATION =
            createTextAttributesKey("PIPO_OPERATOR_DECLARATION", DefaultLanguageHighlighterColors.FUNCTION_DECLARATION);
    public static final TextAttributesKey TYPE_NAME =
            createTextAttributesKey("PIPO_TYPE_NAME", DefaultLanguageHighlighterColors.CLASS_NAME);
    public static final TextAttributesKey BUILTIN_TYPE =
            createTextAttributesKey("PIPO_BUILTIN_TYPE", DefaultLanguageHighlighterColors.KEYWORD);
    public static final TextAttributesKey INTERFACE_NAME =
            createTextAttributesKey("PIPO_INTERFACE_NAME", DefaultLanguageHighlighterColors.INTERFACE_NAME);
    public static final TextAttributesKey TYPE_PARAMETER =
            createTextAttributesKey("PIPO_TYPE_PARAMETER", DefaultLanguageHighlighterColors.CLASS_NAME);
    public static final TextAttributesKey PARAMETER =
            createTextAttributesKey("PIPO_PARAMETER", DefaultLanguageHighlighterColors.PARAMETER);
    public static final TextAttributesKey LOCAL_VARIABLE =
            createTextAttributesKey("PIPO_LOCAL_VARIABLE", DefaultLanguageHighlighterColors.LOCAL_VARIABLE);
    public static final TextAttributesKey GLOBAL_VARIABLE =
            createTextAttributesKey("PIPO_GLOBAL_VARIABLE", DefaultLanguageHighlighterColors.GLOBAL_VARIABLE);
    public static final TextAttributesKey CONSTANT =
            createTextAttributesKey("PIPO_CONSTANT", DefaultLanguageHighlighterColors.CONSTANT);
    public static final TextAttributesKey FIELD =
            createTextAttributesKey("PIPO_FIELD", DefaultLanguageHighlighterColors.INSTANCE_FIELD);
    public static final TextAttributesKey ENUM_VARIANT =
            createTextAttributesKey("PIPO_ENUM_VARIANT", DefaultLanguageHighlighterColors.STATIC_FIELD);
    public static final TextAttributesKey NAMESPACE =
            createTextAttributesKey("PIPO_NAMESPACE", DefaultLanguageHighlighterColors.IDENTIFIER);

    private PipoColors() {
    }
}
