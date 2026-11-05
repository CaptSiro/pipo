package com.pipo.lang.parser;

import com.intellij.lang.ASTNode;
import com.intellij.lang.ParserDefinition;
import com.intellij.lang.PsiParser;
import com.intellij.lexer.Lexer;
import com.intellij.openapi.project.Project;
import com.intellij.psi.FileViewProvider;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.tree.IElementType;
import com.intellij.psi.tree.IFileElementType;
import com.intellij.psi.tree.TokenSet;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.psi.*;
import org.jetbrains.annotations.NotNull;

import static com.pipo.lang.parser.PipoElementTypes.*;

public final class PipoParserDefinition implements ParserDefinition {
    @Override
    public @NotNull Lexer createLexer(Project project) {
        return new PipoLexer();
    }

    @Override
    public @NotNull PsiParser createParser(Project project) {
        return new PipoParser();
    }

    @Override
    public @NotNull IFileElementType getFileNodeType() {
        return FILE;
    }

    @Override
    public @NotNull TokenSet getCommentTokens() {
        return PipoTokenTypes.COMMENTS;
    }

    @Override
    public @NotNull TokenSet getStringLiteralElements() {
        return PipoTokenTypes.STRINGS;
    }

    @Override
    public @NotNull TokenSet getWhitespaceTokens() {
        return PipoTokenTypes.WHITESPACES;
    }

    @Override
    public @NotNull PsiFile createFile(@NotNull FileViewProvider viewProvider) {
        return new PipoFile(viewProvider);
    }

    @Override
    public @NotNull SpaceRequirements spaceExistenceTypeBetweenTokens(ASTNode left, ASTNode right) {
        return SpaceRequirements.MAY;
    }

    @Override
    public @NotNull PsiElement createElement(ASTNode node) {
        IElementType t = node.getElementType();
        if (t == FUNCTION_DECL || t == FUNCTION_EXPR) {
            return new PipoFunctionDecl(node);
        }
        if (t == VAR_DECL) {
            return new PipoVarDecl(node);
        }
        if (t == PARAM) {
            return new PipoParam(node);
        }
        if (t == GENERIC_PARAM) {
            return new PipoGenericParam(node);
        }
        if (t == TYPE_DECL) {
            return new PipoTypeDecl(node);
        }
        if (t == ENUM_DECL) {
            return new PipoEnumDecl(node);
        }
        if (t == ENUM_VARIANT) {
            return new PipoEnumVariant(node);
        }
        if (t == INTERFACE_DECL) {
            return new PipoInterfaceDecl(node);
        }
        if (t == RECORD_FIELD) {
            return new PipoRecordField(node);
        }
        if (t == FOR_VAR || t == PATTERN_BINDING) {
            return new PipoLocalBinding(node);
        }
        if (t == IMPORT_STMT) {
            return new PipoImportStmt(node);
        }
        if (t == MODULE_PATH) {
            return new PipoModulePath(node);
        }
        if (t == IMPORT_NAME) {
            return new PipoImportName(node);
        }
        if (t == OBJECT_ENTRY) {
            return new PipoObjectEntry(node);
        }
        if (t == REF_EXPR) {
            return new PipoRefExpr(node);
        }
        if (t == FIELD_EXPR) {
            return new PipoFieldExpr(node);
        }
        if (t == TYPE_REF || t == TYPE_QUALIFIER) {
            return new PipoTypeRef(node);
        }
        return new PipoElement(node);
    }
}
