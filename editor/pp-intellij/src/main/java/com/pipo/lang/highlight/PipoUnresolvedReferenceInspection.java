package com.pipo.lang.highlight;

import com.intellij.codeInspection.LocalInspectionTool;
import com.intellij.codeInspection.ProblemHighlightType;
import com.intellij.codeInspection.ProblemsHolder;
import com.intellij.openapi.util.TextRange;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiElementVisitor;
import com.intellij.psi.PsiReference;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import com.pipo.lang.resolve.PipoModules;
import com.pipo.lang.resolve.PipoReferences;
import org.jetbrains.annotations.NotNull;

import java.util.List;

/**
 * Names that resolve to nothing: variables and functions, type names, imports
 * whose module cannot be found, and members of modules and enums. Fields of
 * objects and dot calls (which may come from the receiver's module) are not
 * checked, since only running the code can tell.
 */
public final class PipoUnresolvedReferenceInspection extends LocalInspectionTool {
    @Override
    public @NotNull PsiElementVisitor buildVisitor(@NotNull ProblemsHolder holder, boolean isOnTheFly) {
        return new PsiElementVisitor() {
            @Override
            public void visitElement(@NotNull PsiElement e) {
                if (PipoPsiUtil.isInBuiltinStub(e)) {
                    return;
                }
                if (e instanceof PipoRefExpr ref) {
                    checkRef(ref, holder);
                } else if (e instanceof PipoTypeRef type) {
                    checkType(type, holder);
                } else if (e instanceof PipoModulePath path) {
                    if (targets(path.getReference()).isEmpty()) {
                        holder.registerProblem(path, "Cannot find module " + path.getText(), ProblemHighlightType.WARNING);
                    }
                } else if (e instanceof PipoFieldExpr field) {
                    checkField(field, holder);
                } else if (e instanceof PipoImportName name) {
                    PipoImportStmt imp = name.getImport();
                    if (imp != null && imp.getModulePath() != null && !targets(imp.getModulePath().getReference()).isEmpty()
                            && targets(name.getReference()).isEmpty()) {
                        holder.registerProblem(name, "'" + name.getName() + "' is not exported by " + imp.getModulePath().getText(),
                                ProblemHighlightType.LIKE_UNKNOWN_SYMBOL);
                    }
                }
            }
        };
    }

    private static List<PsiElement> targets(PsiReference ref) {
        return ref instanceof PipoReferences.Base<?> b ? b.targets() : List.of();
    }

    private static void checkRef(PipoRefExpr ref, ProblemsHolder holder) {
        String name = ref.getReferencedName();
        if (name.equals("_") || ref.isOperator() || name.startsWith("[")) {
            return;
        }
        if (PipoPsiUtil.is(ref.getParent(), PipoElementTypes.METHOD_CALL_EXPR)) {
            return; // recv.f(): f may come from the module that declared the receiver's type
        }
        if (PipoPsiUtil.is(ref.getParent(), PipoElementTypes.PRECEDENCE_STMT)) {
            return;
        }
        if (targets(ref.getReference()).isEmpty()) {
            holder.registerProblem(ref, "Unresolved reference '" + name + "'", ProblemHighlightType.LIKE_UNKNOWN_SYMBOL);
        }
    }

    private static void checkType(PipoTypeRef type, ProblemsHolder holder) {
        if (type.isBuiltin() || type.getNameLeaf() == null || type.getReference() == null) {
            return;
        }
        if (type.getQualifier() != null && targets(type.getQualifier().getReference()).isEmpty()) {
            return; // the qualifier is reported instead
        }
        if (targets(type.getReference()).isEmpty()) {
            TextRange r = TextRange.from(type.getNameLeaf().getStartOffsetInParent(), type.getNameLeaf().getTextLength());
            String what = type.isQualifierPart() ? "Unknown module '" : "Unknown type '";
            holder.registerProblem(type, r, what + type.getTypeName() + "'");
        }
    }

    /* only where the qualifier is surely a module or an enum: there the member must exist */
    private static void checkField(PipoFieldExpr field, ProblemsHolder holder) {
        PsiElement q = field.getQualifier();
        if (q == null || field.getNameRange() == null || field.getFieldName() == null) {
            return;
        }
        List<PsiElement> qs = q.getReference() != null ? targets(q.getReference()) : List.of();
        if (qs.isEmpty()) {
            return;
        }
        boolean module = qs.stream().allMatch(t -> t instanceof PipoImportStmt);
        boolean enumeration = qs.stream().allMatch(t -> t instanceof PipoEnumDecl);
        if ((!module && !enumeration) || !targets(field.getReference()).isEmpty()) {
            return;
        }
        String msg = module ? "'" + field.getFieldName() + "' is not exported by module '" + q.getText() + "'"
                : "Enum '" + q.getText() + "' has no variant '" + field.getFieldName() + "'";
        if (module && qs.stream().anyMatch(t -> PipoModules.resolve((PipoImportStmt) t) == null)) {
            return; // the import itself is reported
        }
        holder.registerProblem(field, msg, ProblemHighlightType.LIKE_UNKNOWN_SYMBOL, field.getNameRange());
    }
}
