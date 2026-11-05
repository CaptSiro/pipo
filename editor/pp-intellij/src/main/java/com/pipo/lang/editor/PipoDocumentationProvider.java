package com.pipo.lang.editor;

import com.intellij.lang.documentation.AbstractDocumentationProvider;
import com.intellij.lang.documentation.DocumentationMarkup;
import com.intellij.openapi.util.text.StringUtil;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiReference;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.psi.*;
import com.pipo.lang.resolve.PipoBuiltins;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.Nls;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/**
 * Quick documentation (Ctrl+Q) and Ctrl+hover: the declaration's signature and
 * the // comment lines written directly above it. For an overloaded call it
 * lists every overload.
 */
public final class PipoDocumentationProvider extends AbstractDocumentationProvider {
    @Override
    public @Nullable @Nls String getQuickNavigateInfo(PsiElement element, PsiElement originalElement) {
        if (!(element instanceof PipoNamedElement n)) {
            return null;
        }
        String where = location(n);
        return StringUtil.escapeXmlEntities(definition(n)) + (where.isEmpty() ? "" : "\n" + StringUtil.escapeXmlEntities(where));
    }

    @Override
    public @Nullable @Nls String generateDoc(PsiElement element, @Nullable PsiElement originalElement) {
        if (!(element instanceof PipoNamedElement)) {
            return null;
        }
        List<PipoNamedElement> items = new ArrayList<>();
        for (PsiElement t : overloads(originalElement)) {
            if (t instanceof PipoNamedElement n) {
                items.add(n);
            }
        }
        if (!items.contains(element)) {
            items.clear();
            items.add((PipoNamedElement) element);
        }

        StringBuilder sb = new StringBuilder();
        for (PipoNamedElement n : items) {
            sb.append(DocumentationMarkup.DEFINITION_START)
                    .append(StringUtil.escapeXmlEntities(definition(n)))
                    .append(DocumentationMarkup.DEFINITION_END);
            List<String> comment = PipoPsiUtil.docComment(n);
            if (!comment.isEmpty()) {
                sb.append(DocumentationMarkup.CONTENT_START);
                boolean first = true;
                for (String line : comment) {
                    if (line.isBlank()) {
                        sb.append("<p>");
                        first = true;
                        continue;
                    }
                    if (!first) {
                        sb.append(' ');
                    }
                    sb.append(StringUtil.escapeXmlEntities(line));
                    first = false;
                }
                sb.append(DocumentationMarkup.CONTENT_END);
            }
            sb.append(DocumentationMarkup.SECTIONS_START);
            section(sb, "Kind:", n.kindName());
            String where = location(n);
            if (!where.isEmpty()) {
                section(sb, "Defined in:", where);
            }
            sb.append(DocumentationMarkup.SECTIONS_END);
        }
        return sb.toString();
    }

    private static void section(StringBuilder sb, String title, String value) {
        sb.append(DocumentationMarkup.SECTION_HEADER_START).append(title).append(DocumentationMarkup.SECTION_SEPARATOR)
                .append(StringUtil.escapeXmlEntities(value)).append(DocumentationMarkup.SECTION_END);
    }

    /** all targets of the reference at the caret, when it resolves to several overloads */
    private static List<PsiElement> overloads(@Nullable PsiElement original) {
        for (PsiElement e = original; e != null && !(e instanceof PsiFile); e = e.getParent()) {
            PsiReference ref = e.getReference();
            if (ref instanceof PipoReferences.Base<?> b) {
                return b.targets();
            }
        }
        return List.of();
    }

    private static String location(PipoNamedElement n) {
        PsiFile f = n.getContainingFile();
        if (f == null) {
            return "";
        }
        String stub = f.getUserData(PipoKeys.STUB);
        if (stub != null) {
            if (stub.startsWith(PipoBuiltins.LIB_PREFIX)) {
                return "library module " + stub.substring(PipoBuiltins.LIB_PREFIX.length())
                        + " (copy bundled with the plugin: no lib/ folder found)";
            }
            return stub.isEmpty() ? "internal (the interpreter)" : "native module " + stub;
        }
        return f.getName();
    }

    private static String clip(String s, int max) {
        s = PipoPsiUtil.collapse(s);
        return s.length() > max ? s.substring(0, max) + " ..." : s;
    }

    static String definition(PipoNamedElement n) {
        if (n instanceof PipoFunctionDecl f) {
            StringBuilder sb = new StringBuilder();
            if (f.isGlobal()) {
                sb.append("global ");
            }
            if (f.isNative()) {
                sb.append("native ");
            }
            if (f.getGenericParams() != null) {
                sb.append(PipoPsiUtil.collapse(f.getGenericParams().getText()));
            }
            return sb.append("function ").append(f.getName()).append(f.signature()).toString();
        }
        if (n instanceof PipoVarDecl v) {
            String s = (v.isConst() ? "const " : "let ") + v.presentableText();
            PsiElement init = v.getInitializer();
            if (init != null) {
                s += " = " + clip(init.getText(), 80);
            }
            return s;
        }
        if (n instanceof PipoTypeDecl t) {
            String text = t.getText();
            PsiElement def = t.getDefinition();
            if (def != null) {
                text = text.substring(0, def.getStartOffsetInParent()) + clip(def.getText(), 400);
            }
            return PipoPsiUtil.collapse(text);
        }
        if (n instanceof PipoEnumDecl e) {
            StringBuilder sb = new StringBuilder();
            PsiElement brace = e.child(PipoTokenTypes.LBRACE);
            sb.append(PipoPsiUtil.collapse(brace != null ? e.getText().substring(0, brace.getStartOffsetInParent()) : e.getText()));
            sb.append(" { ");
            List<PipoEnumVariant> vs = e.getVariants();
            for (int i = 0; i < vs.size(); i++) {
                sb.append(i > 0 ? ", " : "").append(vs.get(i).presentableText());
            }
            return sb.append(" }").toString();
        }
        if (n instanceof PipoEnumVariant v) {
            PipoEnumDecl e = v.getEnum();
            return (e != null ? e.getName() + ":" : "") + v.presentableText()
                    + (v.child(PipoTokenTypes.ASSIGN) != null ? " = " + clip(v.getText().substring(
                    v.child(PipoTokenTypes.ASSIGN).getStartOffsetInParent() + 1), 60) : "");
        }
        if (n instanceof PipoInterfaceDecl i) {
            StringBuilder sb = new StringBuilder("interface ").append(i.getName()).append(" {");
            for (PipoFunctionDecl r : i.getRequirements()) {
                sb.append(" function ").append(r.presentableText()).append(r.getBody() != null ? " {...}" : "").append(';');
            }
            return sb.append(" }").toString();
        }
        if (n instanceof PipoParam p) {
            PsiElement fn = p.getParent() != null ? p.getParent().getParent() : null;
            String of = fn instanceof PipoFunctionDecl f && f.getName() != null ? " of " + f.getName() : "";
            return "parameter " + p.presentableText() + of;
        }
        if (n instanceof PipoImportStmt imp) {
            return PipoPsiUtil.collapse(imp.getText());
        }
        return n.kindName() + " " + n.presentableText();
    }
}
