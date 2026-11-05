package com.pipo.lang.editor;

import com.intellij.lang.parameterInfo.CreateParameterInfoContext;
import com.intellij.lang.parameterInfo.ParameterInfoHandler;
import com.intellij.lang.parameterInfo.ParameterInfoUIContext;
import com.intellij.lang.parameterInfo.ParameterInfoUtils;
import com.intellij.lang.parameterInfo.UpdateParameterInfoContext;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.PipoFunctionDecl;
import com.pipo.lang.psi.PipoParam;
import com.pipo.lang.psi.PipoPsiUtil;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/**
 * Ctrl+P inside f(...) / recv.f(...): every overload of f. In a dot call the
 * receiver is the first parameter, so the highlighted one is shifted by one.
 */
public final class PipoParameterInfoHandler implements ParameterInfoHandler<PsiElement, PipoFunctionDecl> {
    private static @Nullable PsiElement findArgList(PsiFile file, int offset) {
        PsiElement e = file.findElementAt(offset);
        if (e == null && offset > 0) {
            e = file.findElementAt(offset - 1);
        }
        while (e != null && !(e instanceof PsiFile)) {
            if (PipoPsiUtil.is(e, PipoElementTypes.ARG_LIST)) {
                // inside the parentheses, not on the '(' of a nested call's callee
                if (offset > e.getTextRange().getStartOffset()) {
                    return e;
                }
            }
            e = e.getParent();
        }
        return null;
    }

    private static List<PipoFunctionDecl> candidates(PsiElement args) {
        List<PipoFunctionDecl> out = new ArrayList<>();
        PsiElement callee = PipoPsiUtil.calleeOfArgs(args);
        if (callee != null && callee.getReference() instanceof PipoReferences.Base<?> ref) {
            for (PsiElement t : ref.targets()) {
                if (t instanceof PipoFunctionDecl f) {
                    out.add(f);
                }
            }
        }
        return out;
    }

    private static boolean isDotCall(PsiElement args) {
        return PipoPsiUtil.is(args.getParent(), PipoElementTypes.METHOD_CALL_EXPR);
    }

    @Override
    public @Nullable PsiElement findElementForParameterInfo(@NotNull CreateParameterInfoContext context) {
        PsiElement args = findArgList(context.getFile(), context.getOffset());
        if (args == null) {
            return null;
        }
        List<PipoFunctionDecl> fns = candidates(args);
        if (fns.isEmpty()) {
            return null;
        }
        context.setItemsToShow(fns.toArray());
        return args;
    }

    @Override
    public void showParameterInfo(@NotNull PsiElement element, @NotNull CreateParameterInfoContext context) {
        context.showHint(element, element.getTextRange().getStartOffset(), this);
    }

    @Override
    public @Nullable PsiElement findElementForUpdatingParameterInfo(@NotNull UpdateParameterInfoContext context) {
        return findArgList(context.getFile(), context.getOffset());
    }

    @Override
    public void updateParameterInfo(@NotNull PsiElement args, @NotNull UpdateParameterInfoContext context) {
        int index = ParameterInfoUtils.getCurrentParameterIndex(args.getNode(), context.getOffset(), PipoTokenTypes.COMMA);
        context.setCurrentParameter(isDotCall(args) ? index + 1 : index);
    }

    @Override
    public void updateUI(PipoFunctionDecl fn, @NotNull ParameterInfoUIContext context) {
        if (fn == null || !fn.isValid()) {
            context.setUIComponentEnabled(false);
            return;
        }
        List<PipoParam> params = fn.getParams();
        StringBuilder sb = new StringBuilder();
        int current = context.getCurrentParameterIndex();
        int hlStart = -1;
        int hlEnd = -1;
        for (int i = 0; i < params.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            if (i == current) {
                hlStart = sb.length();
            }
            sb.append(params.get(i).presentableText());
            if (i == current) {
                hlEnd = sb.length();
            }
        }
        if (fn.isVariadic()) {
            if (!params.isEmpty()) {
                sb.append(", ");
            }
            if (current >= params.size()) {
                hlStart = sb.length();
                hlEnd = sb.length() + 3;
            }
            sb.append("...");
        }
        if (sb.isEmpty()) {
            sb.append("<no parameters>");
        }
        PsiElement ret = fn.getReturnType();
        if (ret != null) {
            sb.append("  -> ").append(PipoPsiUtil.collapse(ret.getText()));
        }
        boolean tooMany = !fn.isVariadic() && current >= params.size() && current > 0;
        context.setupUIComponentPresentation(sb.toString(), hlStart, hlEnd, tooMany, false, false,
                context.getDefaultParameterColor());
    }
}
