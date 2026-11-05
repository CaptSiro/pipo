package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.intellij.util.IncorrectOperationException;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.List;

/**
 * import io / import "./geometry" / import os:fs -> files / import "./x" -> { a, b }.
 * As a declaration it binds the module under its alias (the last path segment by default).
 */
public final class PipoImportStmt extends PipoNamedElement {
    public PipoImportStmt(@NotNull ASTNode node) {
        super(node);
    }

    public @Nullable PipoModulePath getModulePath() {
        return child(PipoModulePath.class);
    }

    public boolean isReexport() {
        return child(PipoTokenTypes.EXPORT) != null;
    }

    public boolean isDestructured() {
        return child(PipoElementTypes.IMPORT_LIST) != null;
    }

    public List<PipoImportName> getImportNames() {
        PsiElement list = child(PipoElementTypes.IMPORT_LIST);
        return list instanceof PipoElement e ? e.children(PipoImportName.class) : List.of();
    }

    public @Nullable PsiElement getAlias() {
        PsiElement alias = child(PipoElementTypes.IMPORT_ALIAS);
        return alias instanceof PipoElement e ? e.child(PipoElementTypes.NAME) : null;
    }

    @Override
    public @Nullable PsiElement getNameIdentifier() {
        PsiElement alias = getAlias();
        if (alias != null) {
            return alias;
        }
        if (isDestructured()) {
            return null;
        }
        PipoModulePath path = getModulePath();
        PsiElement last = path != null ? path.getLastSegment() : null;
        return last != null && PipoPsiUtil.is(last, PipoTokenTypes.IDENT) ? last : null;
    }

    @Override
    public @Nullable String getName() {
        if (isDestructured()) {
            return null;
        }
        PsiElement id = getNameIdentifier();
        if (id != null) {
            return id.getText();
        }
        PipoModulePath path = getModulePath();
        PsiElement last = path != null ? path.getLastSegment() : null;
        if (last == null) {
            return null;
        }
        // import "./dir/geometry.pp" binds geometry
        String s = PipoModulePath.unquote(last.getText());
        int slash = Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\'));
        s = s.substring(slash + 1);
        if (s.endsWith(".pp") && s.length() > 3) {
            s = s.substring(0, s.length() - 3);
        }
        return s.isEmpty() ? null : s;
    }

    @Override
    public PsiElement setName(@NotNull String name) throws IncorrectOperationException {
        PsiElement alias = getAlias();
        if (alias == null) {
            throw new IncorrectOperationException("renaming the module itself would change what is imported; add an alias with '->'");
        }
        alias.replace(PipoElementFactory.createName(getProject(), name));
        return this;
    }

    @Override
    public @NotNull String presentableText() {
        PipoModulePath path = getModulePath();
        return path != null ? path.getText() : "import";
    }

    @Override
    public @NotNull String kindName() {
        return "module";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return AllIcons.Nodes.Package;
    }
}
