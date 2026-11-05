package com.pipo.lang.psi;

import com.intellij.lang.ASTNode;
import com.intellij.navigation.ItemPresentation;
import com.intellij.openapi.util.Iconable;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiNameIdentifierOwner;
import com.intellij.psi.search.LocalSearchScope;
import com.intellij.psi.search.SearchScope;
import com.intellij.util.IncorrectOperationException;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

/** A declaration: its name is the NAME child unless a subclass says otherwise. */
public abstract class PipoNamedElement extends PipoElement implements PsiNameIdentifierOwner {
    protected PipoNamedElement(@NotNull ASTNode node) {
        super(node);
    }

    @Override
    public @Nullable PsiElement getNameIdentifier() {
        return child(PipoElementTypes.NAME);
    }

    @Override
    public @Nullable String getName() {
        PsiElement id = getNameIdentifier();
        return id != null ? id.getText() : null;
    }

    @Override
    public PsiElement setName(@NotNull String name) throws IncorrectOperationException {
        PsiElement id = getNameIdentifier();
        if (id == null) {
            throw new IncorrectOperationException("no name to rename");
        }
        if (id.getNode().getElementType() == PipoElementTypes.NAME) {
            id.replace(PipoElementFactory.createName(getProject(), name));
        } else {
            id.replace(PipoElementFactory.createIdentifier(getProject(), name));
        }
        return this;
    }

    @Override
    public int getTextOffset() {
        PsiElement id = getNameIdentifier();
        return id != null ? id.getTextOffset() : super.getTextOffset();
    }

    /** "function", "variable", ... for find usages and documentation */
    public abstract @NotNull String kindName();

    public abstract @NotNull Icon kindIcon();

    /** a short one-line description used in completion, structure view and quick navigation */
    public @NotNull String presentableText() {
        String n = getName();
        return n != null ? n : "<anonymous>";
    }

    /** declared directly in the file (possibly inside `export`), not inside a function or block */
    public boolean isTopLevel() {
        PsiElement p = getParent();
        if (p instanceof PipoElement e && e.type() == PipoElementTypes.EXPORT_STMT) {
            p = p.getParent();
        }
        return p instanceof PipoFile;
    }

    @Override
    public @NotNull SearchScope getUseScope() {
        if (!isTopLevel() && !(this instanceof PipoEnumVariant) && !(this instanceof PipoRecordField)
                && !(this instanceof PipoFunctionDecl f && f.getParent() instanceof PipoInterfaceDecl)) {
            PsiFile file = getContainingFile();
            return new LocalSearchScope(file != null ? file : this);
        }
        return super.getUseScope();
    }

    @Override
    public Icon getIcon(@Iconable.IconFlags int flags) {
        return kindIcon();
    }

    @Override
    public ItemPresentation getPresentation() {
        PipoNamedElement self = this;
        return new ItemPresentation() {
            @Override
            public String getPresentableText() {
                return self.presentableText();
            }

            @Override
            public String getLocationString() {
                PsiFile file = self.getContainingFile();
                return file != null ? file.getName() : null;
            }

            @Override
            public Icon getIcon(boolean unused) {
                return self.kindIcon();
            }
        };
    }

    @Override
    public String toString() {
        return getClass().getSimpleName() + "(" + getName() + ")";
    }
}
