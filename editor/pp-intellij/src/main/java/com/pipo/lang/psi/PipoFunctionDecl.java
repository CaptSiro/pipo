package com.pipo.lang.psi;

import com.intellij.icons.AllIcons;
import com.intellij.lang.ASTNode;
import com.intellij.psi.PsiElement;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.List;

/**
 * A named function: a definition, a declaration (no body), a native function,
 * an interface requirement, or a named function expression.
 */
public final class PipoFunctionDecl extends PipoNamedElement {
    public PipoFunctionDecl(@NotNull ASTNode node) {
        super(node);
    }

    public boolean isGlobal() {
        return child(PipoTokenTypes.GLOBAL) != null;
    }

    public boolean isNative() {
        return child(PipoTokenTypes.NATIVE_KW) != null;
    }

    public boolean isExpression() {
        return type() == PipoElementTypes.FUNCTION_EXPR;
    }

    public boolean isRequirement() {
        return getParent() instanceof PipoInterfaceDecl;
    }

    public @Nullable PsiElement getBody() {
        return child(PipoElementTypes.BLOCK);
    }

    /** a function written without a body (not native, not an interface requirement) */
    public boolean isDeclarationOnly() {
        return getBody() == null && !isNative() && !isRequirement() && !isExpression();
    }

    public @Nullable PipoElement getParamList() {
        return child(PipoElementTypes.PARAM_LIST) instanceof PipoElement e ? e : null;
    }

    public List<PipoParam> getParams() {
        PipoElement list = getParamList();
        return list != null ? list.children(PipoParam.class) : List.of();
    }

    public boolean isVariadic() {
        PipoElement list = getParamList();
        if (list == null) {
            return false;
        }
        for (PsiElement c = list.getFirstChild(); c != null; c = c.getNextSibling()) {
            if (c.getNode().getElementType() == PipoTokenTypes.OP && c.getText().equals("...")) {
                return true;
            }
        }
        return false;
    }

    /** the type after `->` */
    public @Nullable PsiElement getReturnType() {
        PsiElement arrow = child(PipoTokenTypes.ARROW);
        if (arrow == null) {
            return null;
        }
        for (PsiElement c = arrow.getNextSibling(); c != null; c = c.getNextSibling()) {
            if (c instanceof PipoElement e && PipoElementTypes.TYPES.contains(e.type())) {
                return c;
            }
        }
        return null;
    }

    public @Nullable PipoElement getGenericParams() {
        return child(PipoElementTypes.GENERIC_PARAMS) instanceof PipoElement e ? e : null;
    }

    /** (int a, string b) -> T */
    public @NotNull String signature() {
        StringBuilder sb = new StringBuilder("(");
        List<PipoParam> params = getParams();
        for (int i = 0; i < params.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append(params.get(i).presentableText());
        }
        if (isVariadic()) {
            sb.append(params.isEmpty() ? "..." : ", ...");
        }
        sb.append(")");
        PsiElement ret = getReturnType();
        if (ret != null) {
            sb.append(" -> ").append(PipoPsiUtil.collapse(ret.getText()));
        }
        return sb.toString();
    }

    @Override
    public @NotNull String presentableText() {
        String n = getName();
        return (n != null ? n : "function") + signature();
    }

    @Override
    public @NotNull String kindName() {
        return isRequirement() ? "interface function" : "function";
    }

    @Override
    public @NotNull Icon kindIcon() {
        return isRequirement() ? AllIcons.Nodes.AbstractMethod : AllIcons.Nodes.Function;
    }
}
