package com.pipo.lang.highlight;

import com.intellij.lang.annotation.AnnotationHolder;
import com.intellij.lang.annotation.Annotator;
import com.intellij.lang.annotation.HighlightSeverity;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.util.TextRange;
import com.intellij.openapi.util.text.StringUtil;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiReference;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoBuiltinNames;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.math.BigInteger;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static com.pipo.lang.psi.PipoPsiUtil.is;
import static com.pipo.lang.psi.PipoPsiUtil.typeOf;

/**
 * Semantic colors (declarations, calls, types, parameters, ...) and the checks
 * the interpreter makes after parsing: import / export order, declarations
 * without a definition, literals, and a few rules about where things may appear.
 * The messages are the interpreter's own.
 */
public final class PipoAnnotator implements Annotator {
    private static final Pattern FORMAT_SPEC = Pattern.compile(
            "%(?:%|[-+ 0#]*(?:\\*|\\d+)?(?:\\.(?:\\*|\\d+))?(?:hh|h|ll|l|L|q|j|z|t)?[diuoxXfFeEgGaAcs])");

    @Override
    public void annotate(@NotNull PsiElement element, @NotNull AnnotationHolder holder) {
        IElementType t = typeOf(element);
        if (element.getFirstChild() == null) {
            annotateLeaf(element, t, holder);
            return;
        }

        if (t == PipoElementTypes.NAME) {
            colorDeclarationName(element, holder);
        } else if (element instanceof PipoTypeRef ref) {
            colorTypeRef(ref, holder);
        } else if (element instanceof PipoModulePath path) {
            for (PsiElement c = path.getFirstChild(); c != null; c = c.getNextSibling()) {
                if (is(c, PipoTokenTypes.IDENT)) {
                    color(holder, c.getTextRange(), PipoColors.NAMESPACE);
                }
            }
        } else if (element instanceof PipoRefExpr ref) {
            colorRef(ref, holder);
        } else if (element instanceof PipoFieldExpr field) {
            colorField(field, holder);
        } else if (element instanceof PipoObjectEntry entry && !entry.isShorthand() && entry.getFirstChild() != null
                && !is(entry.getFirstChild(), PipoTokenTypes.STRING)) {
            color(holder, entry.getFirstChild().getTextRange(), PipoColors.FIELD);
        }

        if (PipoPsiUtil.isInBuiltinStub(element)) {
            return;
        }
        if (element instanceof PsiFile file) {
            checkFileOrder(file, holder);
            checkDeclarations(file, holder);
        } else if (t == PipoElementTypes.BLOCK) {
            checkDeclarations(element, holder);
        } else if (element instanceof PipoVarDecl var) {
            checkVar(var, holder);
        } else if (element instanceof PipoFunctionDecl fn) {
            checkFunction(fn, holder);
        } else if (t == PipoElementTypes.EXPORT_STMT) {
            checkExport(element, holder);
        }
    }

    private static void color(AnnotationHolder holder, TextRange range, TextAttributesKey key) {
        holder.newSilentAnnotation(HighlightSeverity.INFORMATION).range(range).textAttributes(key).create();
    }

    private static void error(AnnotationHolder holder, PsiElement at, String message) {
        holder.newAnnotation(HighlightSeverity.ERROR, message).range(at).create();
    }

    private static void error(AnnotationHolder holder, TextRange at, String message) {
        holder.newAnnotation(HighlightSeverity.ERROR, message).range(at).create();
    }

    /* ---------------------------------------------------------------- leaves */

    private void annotateLeaf(PsiElement leaf, IElementType t, AnnotationHolder holder) {
        if (PipoTokenTypes.SOFT_KEYWORDS.contains(t)) {
            color(holder, leaf.getTextRange(), PipoColors.KEYWORD);
        } else if (t == PipoTokenTypes.STRING) {
            checkString(leaf, holder);
        } else if (t == PipoTokenTypes.BLOCK_COMMENT) {
            String s = leaf.getText();
            if (s.length() < 4 || !s.endsWith("*/")) {
                error(holder, leaf, "unterminated block comment");
            }
        } else if (t == PipoTokenTypes.INT || t == PipoTokenTypes.FLOAT) {
            checkNumber(leaf, t, holder);
        } else if (t == PipoTokenTypes.BAD_CHARACTER) {
            error(holder, leaf, "unexpected character '" + leaf.getText() + "'");
        }
    }

    private void checkString(PsiElement leaf, AnnotationHolder holder) {
        String s = leaf.getText();
        int base = leaf.getTextRange().getStartOffset();
        char quote = s.charAt(0);
        int i = 1;
        boolean closed = false;
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c == quote) {
                closed = i == s.length() - 1;
                break;
            }
            if (c != '\\') {
                i++;
                continue;
            }
            int start = i;
            char e = i + 1 < s.length() ? s.charAt(i + 1) : '\0';
            i += 2;
            String problem = null;
            switch (e) {
                case 'n', 't', 'r', '0', '\\', '"', '\'' -> {
                }
                case 'x' -> {
                    if (i + 1 < s.length() && isHex(s.charAt(i)) && isHex(s.charAt(i + 1))) {
                        i += 2;
                    } else {
                        problem = "invalid \\x escape (expected two hex digits)";
                    }
                }
                case 'u' -> {
                    int j = i;
                    if (j < s.length() && s.charAt(j) == '{') {
                        j++;
                        int digitsStart = j;
                        while (j < s.length() && isHex(s.charAt(j))) {
                            j++;
                        }
                        boolean ok = j < s.length() && s.charAt(j) == '}' && j > digitsStart && j - digitsStart <= 8
                                && Long.parseLong(s.substring(digitsStart, j), 16) <= 0x10FFFF;
                        if (ok) {
                            i = j + 1;
                        } else {
                            problem = "invalid \\u escape (expected \\u{XXXX})";
                        }
                    } else {
                        problem = "invalid \\u escape (expected \\u{XXXX})";
                    }
                }
                default -> problem = "unknown escape sequence '\\" + (e == '\0' ? "0" : String.valueOf(e)) + "'";
            }
            TextRange r = new TextRange(base + start, base + Math.min(i, s.length()));
            if (problem == null) {
                color(holder, r, PipoColors.VALID_ESCAPE);
            } else {
                holder.newAnnotation(HighlightSeverity.ERROR, problem).range(r)
                        .textAttributes(PipoColors.INVALID_ESCAPE).create();
            }
        }
        if (!closed) {
            error(holder, leaf, "unterminated string literal");
            return;
        }
        if (isFormatString(leaf)) {
            Matcher m = FORMAT_SPEC.matcher(s);
            while (m.find()) {
                color(holder, new TextRange(base + m.start(), base + m.end()), PipoColors.FORMAT_SPEC);
            }
        }
    }

    private static boolean isHex(char c) {
        return Character.digit(c, 16) >= 0;
    }

    /** the first argument of format(...), or the receiver of "...".format(...) */
    private static boolean isFormatString(PsiElement leaf) {
        PsiElement lit = leaf.getParent();
        if (!is(lit, PipoElementTypes.LITERAL)) {
            return false;
        }
        PsiElement p = lit.getParent();
        if (is(p, PipoElementTypes.ARG_LIST)) {
            PsiElement first = ((PipoElement) p).child(PipoElement.class);
            PsiElement callee = PipoPsiUtil.calleeOfArgs(p);
            return first == lit && callee != null && callee.getText().equals("format")
                    && !is(callee.getParent(), PipoElementTypes.METHOD_CALL_EXPR);
        }
        if (is(p, PipoElementTypes.METHOD_CALL_EXPR) && p.getFirstChild() == lit) {
            PsiElement callee = PipoPsiUtil.methodCallee(p);
            return callee != null && callee.getText().equals("format");
        }
        return false;
    }

    private void checkNumber(PsiElement leaf, IElementType t, AnnotationHolder holder) {
        String s = leaf.getText();
        PsiFile file = leaf.getContainingFile();
        int end = leaf.getTextRange().getEndOffset();
        CharSequence text = file.getViewProvider().getContents();
        if (end < text.length() && PipoLexer.isIdentChar(text.charAt(end))) {
            error(holder, new TextRange(end, end + 1), "invalid character '" + text.charAt(end) + "' in number literal");
            return;
        }
        if (t != PipoTokenTypes.INT) {
            return;
        }
        String digits = s.replace("_", "");
        int radix = 10;
        if (digits.length() > 1 && (digits.charAt(1) == 'x' || digits.charAt(1) == 'X')) {
            radix = 16;
            digits = digits.substring(2);
        } else if (digits.length() > 1 && (digits.charAt(1) == 'b' || digits.charAt(1) == 'B')) {
            radix = 2;
            digits = digits.substring(2);
        }
        if (digits.isEmpty()) {
            error(holder, leaf, "malformed number literal");
            return;
        }
        BigInteger v = new BigInteger(digits, radix);
        // hex / binary literals may use all 64 bits (two's complement)
        BigInteger limit = radix == 10 ? BigInteger.valueOf(Long.MAX_VALUE) : BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE);
        if (v.compareTo(limit) > 0) {
            error(holder, leaf, "integer literal '" + digits + "' is too large");
        }
    }

    /* ---------------------------------------------------------------- colors */

    private void colorDeclarationName(PsiElement name, AnnotationHolder holder) {
        PsiElement p = name.getParent();
        TextAttributesKey key = null;
        if (p instanceof PipoFunctionDecl) {
            key = PipoLexer.isIdentStart(name.getText().charAt(0)) ? PipoColors.FUNCTION_DECLARATION
                    : PipoColors.OPERATOR_DECLARATION;
        } else if (p instanceof PipoTypeDecl || p instanceof PipoEnumDecl) {
            key = PipoColors.TYPE_NAME;
        } else if (p instanceof PipoInterfaceDecl) {
            key = PipoColors.INTERFACE_NAME;
        } else if (p instanceof PipoEnumVariant) {
            key = PipoColors.ENUM_VARIANT;
        } else if (p instanceof PipoParam) {
            key = PipoColors.PARAMETER;
        } else if (p instanceof PipoGenericParam) {
            key = PipoColors.TYPE_PARAMETER;
        } else if (p instanceof PipoVarDecl var) {
            key = variableColor(var);
        } else if (p instanceof PipoRecordField) {
            key = PipoColors.FIELD;
        } else if (p instanceof PipoLocalBinding) {
            key = PipoColors.LOCAL_VARIABLE;
        } else if (is(p, PipoElementTypes.IMPORT_ALIAS)) {
            key = PipoColors.NAMESPACE;
        }
        if (key != null) {
            color(holder, name.getTextRange(), key);
        }
    }

    private static TextAttributesKey variableColor(PipoVarDecl var) {
        if (!var.isTopLevel()) {
            return PipoColors.LOCAL_VARIABLE;
        }
        return var.isConst() ? PipoColors.CONSTANT : PipoColors.GLOBAL_VARIABLE;
    }

    private void colorTypeRef(PipoTypeRef ref, AnnotationHolder holder) {
        PsiElement leaf = ref.getNameLeaf();
        if (leaf == null) {
            return;
        }
        if (ref.isQualifierPart()) {
            color(holder, leaf.getTextRange(), PipoColors.NAMESPACE);
            return;
        }
        if (ref.isBuiltin()) {
            color(holder, leaf.getTextRange(), PipoColors.BUILTIN_TYPE);
            return;
        }
        PsiElement target = firstTarget(ref.getReference());
        TextAttributesKey key = PipoColors.TYPE_NAME;
        if (target instanceof PipoGenericParam) {
            key = PipoColors.TYPE_PARAMETER;
        } else if (target instanceof PipoInterfaceDecl) {
            key = PipoColors.INTERFACE_NAME;
        }
        color(holder, leaf.getTextRange(), key);
    }

    private static @Nullable PsiElement firstTarget(@Nullable PsiReference ref) {
        if (ref instanceof PipoReferences.Base<?> b) {
            List<PsiElement> t = b.targets();
            return t.isEmpty() ? null : t.get(0);
        }
        return null;
    }

    private void colorRef(PipoRefExpr ref, AnnotationHolder holder) {
        if (ref.isOperator() || !is(ref.getFirstChild(), PipoTokenTypes.IDENT)) {
            return;
        }
        PsiElement target = firstTarget(ref.getReference());
        TextAttributesKey key = colorForTarget(target, PipoPsiUtil.isCallee(ref));
        if (key != null) {
            color(holder, ref.getTextRange(), key);
        }
    }

    private void colorField(PipoFieldExpr field, AnnotationHolder holder) {
        TextRange r = field.getNameRange();
        if (r == null || field.getNameElement() == null || !is(field.getNameElement(), PipoTokenTypes.IDENT)) {
            return;
        }
        PsiElement target = firstTarget(field.getReference());
        TextAttributesKey key = colorForTarget(target, PipoPsiUtil.isCallee(field));
        if (key == null) {
            key = PipoPsiUtil.isCallee(field) ? PipoColors.FUNCTION_CALL : PipoColors.FIELD;
        }
        color(holder, r.shiftRight(field.getTextRange().getStartOffset()), key);
    }

    private static @Nullable TextAttributesKey colorForTarget(@Nullable PsiElement target, boolean callee) {
        if (target instanceof PipoTypeDecl || target instanceof PipoEnumDecl) {
            return PipoColors.TYPE_NAME;
        }
        if (target instanceof PipoInterfaceDecl) {
            return PipoColors.INTERFACE_NAME;
        }
        if (callee || target instanceof PipoFunctionDecl) {
            return PipoColors.FUNCTION_CALL;
        }
        if (target instanceof PipoParam) {
            return PipoColors.PARAMETER;
        }
        if (target instanceof PipoImportStmt) {
            return PipoColors.NAMESPACE;
        }
        if (target instanceof PipoEnumVariant) {
            return PipoColors.ENUM_VARIANT;
        }
        if (target instanceof PipoVarDecl var) {
            return variableColor(var);
        }
        if (target instanceof PipoRecordField || target instanceof PipoObjectEntry) {
            return PipoColors.FIELD;
        }
        if (target instanceof PipoLocalBinding) {
            return PipoColors.LOCAL_VARIABLE;
        }
        return null;
    }

    /* ---------------------------------------------------------------- checks */

    private static int line(PsiElement e) {
        PsiFile f = e.getContainingFile();
        return StringUtil.offsetToLineNumber(f.getViewProvider().getContents(), e.getTextRange().getStartOffset()) + 1;
    }

    private static PsiElement firstToken(PsiElement e) {
        PsiElement c = e;
        while (c.getFirstChild() != null) {
            c = c.getFirstChild();
        }
        return c;
    }

    /* a file is: imports, then the code, then exports */
    private void checkFileOrder(PsiFile file, AnnotationHolder holder) {
        int part = 0; // 0 imports, 1 code, 2 exports
        int codeLine = 0;
        int exportLine = 0;
        for (PsiElement s : PipoPsiUtil.statements(file)) {
            if (s instanceof PipoImportStmt) {
                if (part == 1) {
                    error(holder, firstToken(s), "imports must come first in a file, before any other statement (code starts on line "
                            + codeLine + ")");
                } else if (part == 2) {
                    error(holder, firstToken(s), "imports must come first in a file, before any other statement (exports start on line "
                            + exportLine + ")");
                }
            } else if (is(s, PipoElementTypes.EXPORT_STMT) && PipoPsiUtil.unwrapExport(s) == s
                    && ((PipoElement) s).child(PipoElementTypes.EXPR_STMT) == null) {
                if (part != 2) {
                    exportLine = line(s);
                }
                part = 2;
            } else {
                if (part == 2) {
                    error(holder, firstToken(s), "exports must be the last statements of a file (exports start on line "
                            + exportLine + ")");
                }
                if (part == 0) {
                    codeLine = line(s);
                }
                part = Math.max(part, 1);
            }
        }
    }

    /** generics, name and types: what must match between a declaration and its definition */
    private static String signatureKey(PipoFunctionDecl f) {
        StringBuilder sb = new StringBuilder();
        PipoElement g = f.getGenericParams();
        if (g != null) {
            sb.append('<');
            List<PipoGenericParam> ps = g.children(PipoGenericParam.class);
            for (int i = 0; i < ps.size(); i++) {
                sb.append(i > 0 ? ", " : "").append(ps.get(i).getName());
            }
            sb.append('>');
        }
        sb.append(f.getName()).append('(');
        List<PipoParam> params = f.getParams();
        for (int i = 0; i < params.size(); i++) {
            PsiElement type = params.get(i).getTypeElement();
            sb.append(i > 0 ? ", " : "").append(type != null ? PipoPsiUtil.collapse(type.getText()) : "any");
        }
        if (f.isVariadic()) {
            sb.append(params.isEmpty() ? "..." : ", ...");
        }
        PsiElement ret = f.getReturnType();
        sb.append(") -> ").append(ret != null ? PipoPsiUtil.collapse(ret.getText()) : "any");
        return sb.toString().replace(" ,", ",");
    }

    /* every declaration (function without a body) needs a definition later in the same block, with the same types */
    private void checkDeclarations(PsiElement scope, AnnotationHolder holder) {
        List<PsiElement> stmts = PipoPsiUtil.statements(scope);
        for (int i = 0; i < stmts.size(); i++) {
            if (!(PipoPsiUtil.unwrapExport(stmts.get(i)) instanceof PipoFunctionDecl d) || !d.isDeclarationOnly()
                    || d.getName() == null) {
                continue;
            }
            String want = signatureKey(d);
            PipoFunctionDecl def = null;
            for (int j = i + 1; j < stmts.size() && def == null; j++) {
                if (!(PipoPsiUtil.unwrapExport(stmts.get(j)) instanceof PipoFunctionDecl s) || !d.getName().equals(s.getName())
                        || !want.equals(signatureKey(s))) {
                    continue;
                }
                if (s.isDeclarationOnly()) {
                    error(holder, s.getNameIdentifier(), "function " + want + " is already declared on line " + line(d));
                    break;
                }
                if (s.isGlobal() != d.isGlobal()) {
                    String g = d.isGlobal() ? "'global'" : "without 'global'";
                    error(holder, s.getNameIdentifier(), "function " + want + " is declared " + g + " on line " + line(d)
                            + ", so its definition must be " + (d.isGlobal() ? "'global'" : "without 'global'") + " too");
                }
                def = s;
            }
            if (def == null) {
                error(holder, d.getNameIdentifier(), "function " + want
                        + " is declared but not defined (the definition must follow in the same block, with the same types)");
            }
        }
    }

    private void checkVar(PipoVarDecl var, AnnotationHolder holder) {
        PsiElement init = var.getInitializer();
        PsiElement name = var.getNameIdentifier();
        if (name == null) {
            return;
        }
        String kw = var.isConst() ? "const" : "let";
        if (is(init, PipoElementTypes.LAMBDA_EXPR) && var.getTypeElement() == null) {
            error(holder, var.getFirstChild(), "a lambda stored in a variable needs a callback type: type Fn = (int) -> int, then "
                    + kw + " Fn " + var.getName() + " = ...");
        }
        if (init == null && var.isConst() && var.getTypeElement() != null && var.getNode().findChildByType(PipoTokenTypes.ASSIGN) == null) {
            error(holder, name, "constant '" + var.getName() + "' needs a value");
        }
    }

    private void checkFunction(PipoFunctionDecl fn, AnnotationHolder holder) {
        if (fn.getBody() == null) {
            return;
        }
        List<PipoParam> params = fn.getParams();
        for (int i = 0; i < params.size(); i++) {
            PipoParam p = params.get(i);
            if (fn.isRequirement()) {
                if (p.getNameIdentifier() == null) {
                    error(holder, p, "the parameters of a default implementation need names (parameter " + (i + 1) + " of '"
                            + fn.getName() + "' has none)");
                }
            } else if (p.getTypeElement() == null && p.getName() != null && PipoBuiltinNames.PRIMITIVES.contains(p.getName())) {
                // (int) is a type without a name: fine in a declaration, an error in a definition
                error(holder, p, "expected parameter name but found " + describeNext(p));
            }
        }
    }

    private static String describeNext(PsiElement p) {
        PsiElement n = p.getNextSibling();
        while (n != null && (n.getText().isBlank())) {
            n = n.getNextSibling();
        }
        return n != null ? "'" + n.getText() + "'" : "end of file";
    }

    private void checkExport(PsiElement export, AnnotationHolder holder) {
        PsiElement stmt = ((PipoElement) export).child(PipoElementTypes.EXPR_STMT);
        if (stmt == null) {
            for (PsiElement c = export.getFirstChild(); c != null; c = c.getNextSibling()) {
                if (c instanceof PipoElement e && !(c instanceof PipoNamedElement) && e.type() != PipoElementTypes.EXPORT_ITEM
                        && e.type() != PipoElementTypes.NAME) {
                    stmt = c;
                    break;
                }
            }
        }
        if (stmt != null && PipoPsiUtil.unwrapExport(export) == export) {
            error(holder, stmt, "'export' before a statement needs a named declaration (type, enum, interface, function, const or let)");
        }
    }
}
