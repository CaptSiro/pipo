package com.pipo.lang.parser;

import com.intellij.psi.tree.IElementType;
import com.intellij.psi.tree.IFileElementType;
import com.intellij.psi.tree.TokenSet;
import com.pipo.lang.PipoLanguage;
import com.pipo.lang.lexer.PipoTokenType;

/** Composite node types of the PSI tree. */
public interface PipoElementTypes {
    IFileElementType FILE = new IFileElementType("PIPO_FILE", PipoLanguage.INSTANCE);

    /** the name token(s) of a declaration: an identifier, an operator, `in`, `[]` or `[]=` */
    IElementType NAME = new PipoTokenType("NAME");

    // modules
    IElementType IMPORT_STMT = new PipoTokenType("IMPORT_STMT");
    IElementType MODULE_PATH = new PipoTokenType("MODULE_PATH");
    IElementType IMPORT_ALIAS = new PipoTokenType("IMPORT_ALIAS");
    IElementType IMPORT_LIST = new PipoTokenType("IMPORT_LIST");
    IElementType IMPORT_NAME = new PipoTokenType("IMPORT_NAME");
    IElementType EXPORT_STMT = new PipoTokenType("EXPORT_STMT");
    IElementType EXPORT_ITEM = new PipoTokenType("EXPORT_ITEM");

    // declarations
    IElementType VAR_DECL = new PipoTokenType("VAR_DECL");
    IElementType FUNCTION_DECL = new PipoTokenType("FUNCTION_DECL");
    IElementType PARAM_LIST = new PipoTokenType("PARAM_LIST");
    IElementType PARAM = new PipoTokenType("PARAM");
    IElementType GENERIC_PARAMS = new PipoTokenType("GENERIC_PARAMS");
    IElementType GENERIC_PARAM = new PipoTokenType("GENERIC_PARAM");
    IElementType TYPE_DECL = new PipoTokenType("TYPE_DECL");
    IElementType ENUM_DECL = new PipoTokenType("ENUM_DECL");
    IElementType ENUM_VARIANT = new PipoTokenType("ENUM_VARIANT");
    IElementType INTERFACE_DECL = new PipoTokenType("INTERFACE_DECL");
    IElementType INTERFACE_EXTENDS = new PipoTokenType("INTERFACE_EXTENDS");
    IElementType IMPL_STMT = new PipoTokenType("IMPL_STMT");
    IElementType PRECEDENCE_STMT = new PipoTokenType("PRECEDENCE_STMT");

    // statements
    IElementType BLOCK = new PipoTokenType("BLOCK");
    IElementType FOR_STMT = new PipoTokenType("FOR_STMT");
    IElementType FOR_VAR = new PipoTokenType("FOR_VAR");
    IElementType WHILE_STMT = new PipoTokenType("WHILE_STMT");
    IElementType RETURN_STMT = new PipoTokenType("RETURN_STMT");
    IElementType BREAK_STMT = new PipoTokenType("BREAK_STMT");
    IElementType CONTINUE_STMT = new PipoTokenType("CONTINUE_STMT");
    IElementType ASSIGN_STMT = new PipoTokenType("ASSIGN_STMT");
    IElementType EXPR_STMT = new PipoTokenType("EXPR_STMT");

    // types
    IElementType TYPE_REF = new PipoTokenType("TYPE_REF");
    IElementType TYPE_QUALIFIER = new PipoTokenType("TYPE_QUALIFIER");
    IElementType TYPE_ARGS = new PipoTokenType("TYPE_ARGS");
    IElementType ARRAY_TYPE = new PipoTokenType("ARRAY_TYPE");
    IElementType OPTION_TYPE = new PipoTokenType("OPTION_TYPE");
    IElementType UNION_TYPE = new PipoTokenType("UNION_TYPE");
    IElementType RECORD_TYPE = new PipoTokenType("RECORD_TYPE");
    IElementType RECORD_FIELD = new PipoTokenType("RECORD_FIELD");
    IElementType FUNCTION_TYPE = new PipoTokenType("FUNCTION_TYPE");
    IElementType PAREN_TYPE = new PipoTokenType("PAREN_TYPE");
    IElementType CALLBACK_TYPE = new PipoTokenType("CALLBACK_TYPE");

    // expressions
    IElementType REF_EXPR = new PipoTokenType("REF_EXPR");
    IElementType LITERAL = new PipoTokenType("LITERAL");
    IElementType PAREN_EXPR = new PipoTokenType("PAREN_EXPR");
    IElementType ARRAY_EXPR = new PipoTokenType("ARRAY_EXPR");
    IElementType OBJECT_EXPR = new PipoTokenType("OBJECT_EXPR");
    IElementType OBJECT_ENTRY = new PipoTokenType("OBJECT_ENTRY");
    IElementType FUNCTION_EXPR = new PipoTokenType("FUNCTION_EXPR");
    IElementType LAMBDA_EXPR = new PipoTokenType("LAMBDA_EXPR");
    IElementType IF_EXPR = new PipoTokenType("IF_EXPR");
    IElementType MATCH_EXPR = new PipoTokenType("MATCH_EXPR");
    IElementType MATCH_ARM = new PipoTokenType("MATCH_ARM");
    IElementType PATTERN = new PipoTokenType("PATTERN");
    IElementType PATTERN_BINDING = new PipoTokenType("PATTERN_BINDING");
    IElementType WILDCARD = new PipoTokenType("WILDCARD");
    IElementType CALL_EXPR = new PipoTokenType("CALL_EXPR");
    IElementType ARG_LIST = new PipoTokenType("ARG_LIST");
    IElementType INDEX_EXPR = new PipoTokenType("INDEX_EXPR");
    IElementType FIELD_EXPR = new PipoTokenType("FIELD_EXPR");
    IElementType METHOD_CALL_EXPR = new PipoTokenType("METHOD_CALL_EXPR");
    IElementType PREFIX_EXPR = new PipoTokenType("PREFIX_EXPR");
    IElementType CHAIN_EXPR = new PipoTokenType("CHAIN_EXPR");

    TokenSet TYPES = TokenSet.create(TYPE_REF, ARRAY_TYPE, OPTION_TYPE, UNION_TYPE, RECORD_TYPE, FUNCTION_TYPE,
            PAREN_TYPE, CALLBACK_TYPE);
    TokenSet EXPRESSIONS = TokenSet.create(REF_EXPR, LITERAL, PAREN_EXPR, ARRAY_EXPR, OBJECT_EXPR, FUNCTION_EXPR,
            LAMBDA_EXPR, IF_EXPR, MATCH_EXPR, CALL_EXPR, INDEX_EXPR, FIELD_EXPR, METHOD_CALL_EXPR, PREFIX_EXPR,
            CHAIN_EXPR, PATTERN_BINDING, WILDCARD);
    /** nodes whose `{ ... }` contents are indented and folded */
    TokenSet BRACED = TokenSet.create(BLOCK, OBJECT_EXPR, RECORD_TYPE, ENUM_DECL, INTERFACE_DECL, MATCH_EXPR,
            IMPORT_LIST);
}
