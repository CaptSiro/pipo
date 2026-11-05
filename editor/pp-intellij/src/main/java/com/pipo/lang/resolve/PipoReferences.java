package com.pipo.lang.resolve;

import com.intellij.openapi.util.TextRange;
import com.intellij.openapi.util.io.FileUtil;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiElementResolveResult;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiPolyVariantReferenceBase;
import com.intellij.psi.ResolveResult;
import com.intellij.psi.impl.source.resolve.ResolveCache;
import com.intellij.psi.util.PsiTreeUtil;
import com.intellij.util.IncorrectOperationException;
import com.pipo.lang.psi.PipoElementFactory;
import com.pipo.lang.psi.PipoFieldExpr;
import com.pipo.lang.psi.PipoFile;
import com.pipo.lang.psi.PipoImportName;
import com.pipo.lang.psi.PipoImportStmt;
import com.pipo.lang.psi.PipoModulePath;
import com.pipo.lang.psi.PipoRefExpr;
import com.pipo.lang.psi.PipoTypeRef;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.Collection;
import java.util.List;

/** The reference kinds. Results are cached by ResolveCache until the next PSI change. */
public final class PipoReferences {
    private PipoReferences() {
    }

    public abstract static class Base<T extends PsiElement> extends PsiPolyVariantReferenceBase<T> {
        private static final ResolveCache.PolyVariantResolver<Base<?>> RESOLVER =
                (ref, incomplete) -> PsiElementResolveResult.createResults(ref.compute());

        protected Base(@NotNull T element, @NotNull TextRange range) {
            super(element, range);
        }

        protected abstract Collection<? extends PsiElement> compute();

        @Override
        public ResolveResult @NotNull [] multiResolve(boolean incompleteCode) {
            return ResolveCache.getInstance(getElement().getProject())
                    .resolveWithCaching(this, RESOLVER, false, incompleteCode);
        }

        public List<PsiElement> targets() {
            return java.util.Arrays.stream(multiResolve(false)).map(ResolveResult::getElement)
                    .filter(java.util.Objects::nonNull).toList();
        }

        @Override
        public Object @NotNull [] getVariants() {
            return EMPTY_ARRAY; // PipoCompletionContributor provides completion
        }

        @Override
        public PsiElement handleElementRename(@NotNull String newName) throws IncorrectOperationException {
            PsiElement leaf = getElement().findElementAt(getRangeInElement().getStartOffset());
            if (leaf == null || !getRangeInElement().equals(rangeOf(leaf))) {
                throw new IncorrectOperationException("cannot rename this reference");
            }
            leaf.replace(PipoElementFactory.createIdentifier(getElement().getProject(), newName));
            return getElement();
        }

        private TextRange rangeOf(PsiElement leaf) {
            int start = leaf.getTextRange().getStartOffset() - getElement().getTextRange().getStartOffset();
            return TextRange.from(start, leaf.getTextLength());
        }
    }

    /** x, f, + ... */
    public static final class NameRef extends Base<PipoRefExpr> {
        public NameRef(PipoRefExpr e) {
            super(e, TextRange.from(0, e.getTextLength()));
        }

        @Override
        protected Collection<? extends PsiElement> compute() {
            return PipoResolver.resolveRefExpr(getElement());
        }
    }

    /** obj:field, module:export, Enum:VARIANT */
    public static final class FieldRef extends Base<PipoFieldExpr> {
        public FieldRef(PipoFieldExpr e) {
            super(e, e.getNameRange());
        }

        @Override
        protected Collection<? extends PsiElement> compute() {
            return PipoResolver.resolveField(getElement());
        }
    }

    /** Person, geometry:Vec, and the geometry qualifier itself */
    public static final class TypeRef extends Base<PipoTypeRef> {
        public TypeRef(PipoTypeRef e) {
            super(e, TextRange.from(e.getNameLeaf().getStartOffsetInParent(), e.getNameLeaf().getTextLength()));
        }

        @Override
        protected Collection<? extends PsiElement> compute() {
            return PipoResolver.resolveTypeRef(getElement());
        }
    }

    /** a in import "./x" -> { a } */
    public static final class ImportNameRef extends Base<PipoImportName> {
        public ImportNameRef(PipoImportName e) {
            super(e, TextRange.from(e.getNameIdentifier().getStartOffsetInParent(), e.getNameIdentifier().getTextLength()));
        }

        @Override
        protected Collection<? extends PsiElement> compute() {
            PipoImportStmt imp = getElement().getImport();
            PsiFile module = imp != null ? PipoModules.resolve(imp) : null;
            String name = getElement().getName();
            if (module == null || name == null) {
                return List.of();
            }
            return PipoResolver.moduleExports(module, name);
        }

        @Override
        public boolean isReferenceTo(@NotNull PsiElement element) {
            return element != getElement() && super.isReferenceTo(element);
        }
    }

    /** the path of an import: resolves to the module file */
    public static final class ModuleRef extends Base<PipoModulePath> {
        public ModuleRef(PipoModulePath e) {
            super(e, TextRange.from(0, e.getTextLength()));
        }

        @Override
        protected Collection<? extends PsiElement> compute() {
            if (!(getElement().getParent() instanceof PipoImportStmt imp)) {
                return List.of();
            }
            PsiFile f = PipoModules.resolve(imp);
            return f != null ? List.of(f) : List.of();
        }

        /**
         * The string of `import "./path"`, when it names the file directly. Library imports
         * (`import std`) and submodule paths (`"./ui":shapes`) do not, so they are left alone.
         */
        private @Nullable PsiElement editablePath() {
            PipoModulePath path = getElement();
            return path.isFile() && path.getSubNames().isEmpty() ? path.getFirstSegment() : null;
        }

        /** a file was renamed: replace the last segment of the path */
        @Override
        public PsiElement handleElementRename(@NotNull String newName) {
            PsiElement str = editablePath();
            if (str == null) {
                return getElement();
            }
            String old = PipoModulePath.unquote(str.getText());
            int slash = Math.max(old.lastIndexOf('/'), old.lastIndexOf('\\'));
            String name = old.endsWith(".pp") || !newName.endsWith(".pp") ? newName
                    : newName.substring(0, newName.length() - 3);
            return replacePath(str, old.substring(0, slash + 1) + name);
        }

        /** a file was moved (or this file was): point the path at target from where this file is now */
        @Override
        public PsiElement bindToElement(@NotNull PsiElement element) {
            PsiElement str = editablePath();
            if (str == null || !(element instanceof PsiFile target)) {
                return getElement();
            }
            VirtualFile to = target.getVirtualFile();
            VirtualFile from = getElement().getContainingFile().getOriginalFile().getVirtualFile();
            if (to == null || from == null || from.getParent() == null) {
                return getElement();
            }
            String rel = FileUtil.getRelativePath(from.getParent().getPath(), to.getPath(), '/');
            if (rel == null) {
                rel = to.getPath(); // another drive: only an absolute path works
            } else if (!rel.startsWith("../")) {
                rel = "./" + rel;
            }
            String old = PipoModulePath.unquote(str.getText());
            if (!old.endsWith(".pp") && rel.endsWith(".pp")) {
                rel = rel.substring(0, rel.length() - 3);
            }
            if (old.equals(rel)) {
                return getElement();
            }
            return replacePath(str, rel);
        }

        private PsiElement replacePath(PsiElement str, String path) {
            char quote = str.getText().charAt(0);
            String escaped = path.replace("\\", "\\\\").replace(String.valueOf(quote), "\\" + quote);
            PipoFile dummy = PipoElementFactory.createFile(getElement().getProject(),
                    "import " + quote + escaped + quote);
            PipoModulePath fresh = PsiTreeUtil.findChildOfType(dummy, PipoModulePath.class);
            if (fresh == null || fresh.getFirstSegment() == null) {
                throw new IncorrectOperationException("cannot write the import path " + path);
            }
            str.replace(fresh.getFirstSegment());
            return getElement();
        }
    }
}
