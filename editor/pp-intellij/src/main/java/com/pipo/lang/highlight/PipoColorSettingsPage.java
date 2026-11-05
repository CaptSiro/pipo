package com.pipo.lang.highlight;

import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighter;
import com.intellij.openapi.options.colors.AttributesDescriptor;
import com.intellij.openapi.options.colors.ColorDescriptor;
import com.intellij.openapi.options.colors.ColorSettingsPage;
import com.pipo.lang.PipoIcons;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.Map;

public final class PipoColorSettingsPage implements ColorSettingsPage {
    private static final AttributesDescriptor[] DESCRIPTORS = {
            new AttributesDescriptor("Keyword", PipoColors.KEYWORD),
            new AttributesDescriptor("Built-in type", PipoColors.BUILTIN_TYPE),
            new AttributesDescriptor("Number", PipoColors.NUMBER),
            new AttributesDescriptor("String//Text", PipoColors.STRING),
            new AttributesDescriptor("String//Escape sequence", PipoColors.VALID_ESCAPE),
            new AttributesDescriptor("String//Invalid escape sequence", PipoColors.INVALID_ESCAPE),
            new AttributesDescriptor("String//Format specifier", PipoColors.FORMAT_SPEC),
            new AttributesDescriptor("Comments//Line comment", PipoColors.LINE_COMMENT),
            new AttributesDescriptor("Comments//Block comment", PipoColors.BLOCK_COMMENT),
            new AttributesDescriptor("Operators//Operator", PipoColors.OPERATOR),
            new AttributesDescriptor("Operators//Arrow", PipoColors.ARROW),
            new AttributesDescriptor("Braces and punctuation//Parentheses", PipoColors.PARENTHESES),
            new AttributesDescriptor("Braces and punctuation//Brackets", PipoColors.BRACKETS),
            new AttributesDescriptor("Braces and punctuation//Braces", PipoColors.BRACES),
            new AttributesDescriptor("Braces and punctuation//Comma", PipoColors.COMMA),
            new AttributesDescriptor("Braces and punctuation//Semicolon", PipoColors.SEMICOLON),
            new AttributesDescriptor("Braces and punctuation//Dot (call)", PipoColors.DOT),
            new AttributesDescriptor("Braces and punctuation//Colon (attribute)", PipoColors.COLON),
            new AttributesDescriptor("Identifiers//Default", PipoColors.IDENTIFIER),
            new AttributesDescriptor("Identifiers//Local variable", PipoColors.LOCAL_VARIABLE),
            new AttributesDescriptor("Identifiers//Top-level variable", PipoColors.GLOBAL_VARIABLE),
            new AttributesDescriptor("Identifiers//Top-level constant", PipoColors.CONSTANT),
            new AttributesDescriptor("Identifiers//Parameter", PipoColors.PARAMETER),
            new AttributesDescriptor("Identifiers//Field", PipoColors.FIELD),
            new AttributesDescriptor("Identifiers//Enum variant", PipoColors.ENUM_VARIANT),
            new AttributesDescriptor("Identifiers//Module", PipoColors.NAMESPACE),
            new AttributesDescriptor("Functions//Function declaration", PipoColors.FUNCTION_DECLARATION),
            new AttributesDescriptor("Functions//Operator declaration", PipoColors.OPERATOR_DECLARATION),
            new AttributesDescriptor("Functions//Function call", PipoColors.FUNCTION_CALL),
            new AttributesDescriptor("Types//Type name", PipoColors.TYPE_NAME),
            new AttributesDescriptor("Types//Interface name", PipoColors.INTERFACE_NAME),
            new AttributesDescriptor("Types//Type parameter", PipoColors.TYPE_PARAMETER),
            new AttributesDescriptor("Bad character", PipoColors.BAD_CHARACTER),
    };

    private static final Map<String, TextAttributesKey> TAGS = Map.ofEntries(
            Map.entry("kw", PipoColors.KEYWORD),
            Map.entry("prim", PipoColors.BUILTIN_TYPE),
            Map.entry("esc", PipoColors.VALID_ESCAPE),
            Map.entry("fmt", PipoColors.FORMAT_SPEC),
            Map.entry("local", PipoColors.LOCAL_VARIABLE),
            Map.entry("global", PipoColors.GLOBAL_VARIABLE),
            Map.entry("const", PipoColors.CONSTANT),
            Map.entry("param", PipoColors.PARAMETER),
            Map.entry("field", PipoColors.FIELD),
            Map.entry("variant", PipoColors.ENUM_VARIANT),
            Map.entry("ns", PipoColors.NAMESPACE),
            Map.entry("fn", PipoColors.FUNCTION_DECLARATION),
            Map.entry("opdecl", PipoColors.OPERATOR_DECLARATION),
            Map.entry("call", PipoColors.FUNCTION_CALL),
            Map.entry("type", PipoColors.TYPE_NAME),
            Map.entry("iface", PipoColors.INTERFACE_NAME),
            Map.entry("tparam", PipoColors.TYPE_PARAMETER));

    @Override
    public @Nullable Icon getIcon() {
        return PipoIcons.FILE;
    }

    @Override
    public @NotNull SyntaxHighlighter getHighlighter() {
        return new PipoSyntaxHighlighter();
    }

    @Override
    public @NotNull String getDemoText() {
        return """
                import <ns>io</ns>
                import "./<ns>geometry</ns>" -> { <ns>vec</ns> }

                /* a record type and an enum */
                <kw>type</kw> <type>Person</type> = { <prim>string</prim> <field>name</field>, <prim>int</prim> <field>age</field> }
                enum <type>Color</type> { <variant>RED</variant>, <variant>GREEN</variant>, <variant>BLUE</variant> }

                <kw>interface</kw> <iface>Str</iface> {
                    function <fn>str</fn>(<iface>Str</iface>) -> <prim>string</prim>
                }

                const <const>LIMIT</const> = 1_000
                let <global>count</global> = 0x1F

                // functions are bound to types through their first parameter
                function <fn>greet</fn>(<type>Person</type> <param>p</param>, <prim>string</prim>? <param>title</param>) -> <prim>string</prim> {
                    const <local>t</local> = <param>title</param>.<call>unwrap_or</call>("")
                    return <call>format</call>("<fmt>%s</fmt> <fmt>%s</fmt>, <fmt>%d</fmt><esc>\\n</esc>", <local>t</local>, <param>p</param>:<field>name</field>, <param>p</param>:<field>age</field>)
                }

                <<tparam>T</tparam>>function <opdecl>+</opdecl>(<tparam>T</tparam>[] <param>a</param>, <tparam>T</tparam>[] <param>b</param>) -> <tparam>T</tparam>[] { return <param>a</param>.<call>concat</call>(<param>b</param>) }
                <kw>precedence</kw> ** <kw>over</kw> *

                const <type>Person</type> <const>john</const> = { <field>name</field>: "John", <field>age</field>: 22 }
                <ns>io</ns>:<call>print</call>(<const>john</const>.<call>greet</call>(<call>none</call>()))

                for <local>i</local>, <local>x</local> in [3, 1, 2].<call>sort</call>() {
                    if <local>x</local> > 1 && <local>i</local> != 0 { continue }
                }

                const <const>name</const> = match <type>Color</type>:<variant>RED</variant> {
                    <type>Color</type>:<variant>RED</variant> -> "red",
                    _ -> 'other'
                }
                """;
    }

    @Override
    public @Nullable Map<String, TextAttributesKey> getAdditionalHighlightingTagToDescriptorMap() {
        return TAGS;
    }

    @Override
    public AttributesDescriptor @NotNull [] getAttributeDescriptors() {
        return DESCRIPTORS;
    }

    @Override
    public ColorDescriptor @NotNull [] getColorDescriptors() {
        return ColorDescriptor.EMPTY_ARRAY;
    }

    @Override
    public @NotNull String getDisplayName() {
        return "PiPo";
    }
}
