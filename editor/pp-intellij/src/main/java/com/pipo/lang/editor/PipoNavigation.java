package com.pipo.lang.editor;

import com.intellij.codeInsight.template.TemplateActionContext;
import com.intellij.codeInsight.template.TemplateContextType;
import com.intellij.lang.Language;
import com.intellij.lexer.Lexer;
import com.intellij.navigation.ChooseByNameContributorEx;
import com.intellij.navigation.NavigationItem;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiComment;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiManager;
import com.intellij.psi.impl.cache.impl.BaseFilterLexer;
import com.intellij.psi.impl.cache.impl.OccurrenceConsumer;
import com.intellij.psi.impl.cache.impl.todo.LexerBasedTodoIndexer;
import com.intellij.psi.impl.search.IndexPatternBuilder;
import com.intellij.psi.search.FileTypeIndex;
import com.intellij.psi.search.GlobalSearchScope;
import com.intellij.psi.tree.IElementType;
import com.intellij.psi.tree.TokenSet;
import com.intellij.ui.breadcrumbs.BreadcrumbsProvider;
import com.intellij.util.Processor;
import com.intellij.util.indexing.FindSymbolParameters;
import com.intellij.util.indexing.IdFilter;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.PipoLanguage;
import com.pipo.lang.lexer.PipoLexer;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/** Go to symbol, breadcrumbs, TODO comments and the live template context. */
public final class PipoNavigation {
    private PipoNavigation() {
    }

    /* ----------------------------------------------------- go to symbol */

    public static final class GotoSymbol implements ChooseByNameContributorEx {
        private static List<PipoNamedElement> declarations(PsiFile file) {
            List<PipoNamedElement> out = new ArrayList<>();
            for (PipoNamedElement d : PipoPsiUtil.topLevelDeclarations(file)) {
                out.add(d);
                if (d instanceof PipoEnumDecl e) {
                    out.addAll(e.getVariants());
                } else if (d instanceof PipoInterfaceDecl i) {
                    out.addAll(i.getRequirements());
                }
            }
            return out;
        }

        private static void forEachFile(GlobalSearchScope scope, java.util.function.Consumer<PsiFile> action) {
            if (scope.getProject() == null) {
                return;
            }
            PsiManager psi = PsiManager.getInstance(scope.getProject());
            for (VirtualFile vf : FileTypeIndex.getFiles(PipoFileType.INSTANCE, scope)) {
                PsiFile f = psi.findFile(vf);
                if (f != null) {
                    action.accept(f);
                }
            }
        }

        @Override
        public void processNames(@NotNull Processor<? super String> processor, @NotNull GlobalSearchScope scope,
                                 @Nullable IdFilter filter) {
            forEachFile(scope, f -> {
                for (PipoNamedElement d : declarations(f)) {
                    if (d.getName() != null) {
                        processor.process(d.getName());
                    }
                }
            });
        }

        @Override
        public void processElementsWithName(@NotNull String name, @NotNull Processor<? super NavigationItem> processor,
                                            @NotNull FindSymbolParameters parameters) {
            forEachFile(parameters.getSearchScope(), f -> {
                for (PipoNamedElement d : declarations(f)) {
                    if (name.equals(d.getName())) {
                        processor.process(d);
                    }
                }
            });
        }
    }

    /* ------------------------------------------------------ breadcrumbs */

    public static final class Breadcrumbs implements BreadcrumbsProvider {
        private static final Language[] LANGUAGES = {PipoLanguage.INSTANCE};

        @Override
        public Language[] getLanguages() {
            return LANGUAGES;
        }

        @Override
        public boolean acceptElement(@NotNull PsiElement e) {
            IElementType t = PipoPsiUtil.typeOf(e);
            return (e instanceof PipoFunctionDecl f && !f.isExpression()) || e instanceof PipoTypeDecl
                    || e instanceof PipoEnumDecl || e instanceof PipoInterfaceDecl || t == PipoElementTypes.IMPL_STMT
                    || t == PipoElementTypes.FOR_STMT || t == PipoElementTypes.WHILE_STMT || t == PipoElementTypes.IF_EXPR
                    || t == PipoElementTypes.MATCH_EXPR || t == PipoElementTypes.MATCH_ARM
                    || t == PipoElementTypes.LAMBDA_EXPR || t == PipoElementTypes.FUNCTION_EXPR;
        }

        @Override
        public @NotNull String getElementInfo(@NotNull PsiElement e) {
            if (e instanceof PipoFunctionDecl f && !f.isExpression()) {
                return f.getName() + "()";
            }
            if (e instanceof PipoNamedElement n && n.getName() != null) {
                return n.getName();
            }
            IElementType t = PipoPsiUtil.typeOf(e);
            if (t == PipoElementTypes.MATCH_ARM) {
                PsiElement pattern = ((PipoElement) e).child(PipoElementTypes.PATTERN);
                return pattern != null ? shorten(pattern.getText()) + " ->" : "arm";
            }
            if (t == PipoElementTypes.LAMBDA_EXPR || t == PipoElementTypes.FUNCTION_EXPR) {
                return "λ";
            }
            // for x in xs, while cond, if cond, match x, impl A for B: the header up to the block
            String text = e.getText();
            int brace = text.indexOf('{');
            return shorten(brace > 0 ? text.substring(0, brace) : text);
        }

        private static String shorten(String s) {
            s = PipoPsiUtil.collapse(s);
            return s.length() > 40 ? s.substring(0, 40) + "..." : s;
        }
    }

    /* ------------------------------------------------------------- TODO */

    public static final class TodoPatterns implements IndexPatternBuilder {
        @Override
        public @Nullable Lexer getIndexingLexer(@NotNull PsiFile file) {
            return file instanceof PipoFile ? new PipoLexer() : null;
        }

        @Override
        public @Nullable TokenSet getCommentTokenSet(@NotNull PsiFile file) {
            return file instanceof PipoFile ? PipoTokenTypes.COMMENTS : null;
        }

        @Override
        public int getCommentStartDelta(IElementType tokenType) {
            return PipoTokenTypes.COMMENTS.contains(tokenType) ? 2 : 0;
        }

        @Override
        public int getCommentEndDelta(IElementType tokenType) {
            return tokenType == PipoTokenTypes.BLOCK_COMMENT ? 2 : 0;
        }
    }

    public static final class TodoIndexer extends LexerBasedTodoIndexer {
        @Override
        public @NotNull Lexer createLexer(@NotNull OccurrenceConsumer consumer) {
            return new BaseFilterLexer(new PipoLexer(), consumer) {
                @Override
                public void advance() {
                    if (PipoTokenTypes.COMMENTS.contains(myDelegate.getTokenType())) {
                        advanceTodoItemCountsInToken();
                    }
                    myDelegate.advance();
                }
            };
        }
    }

    /* --------------------------------------------------- live templates */

    public static final class TemplateContext extends TemplateContextType {
        public TemplateContext() {
            super("PiPo");
        }

        @Override
        public boolean isInContext(@NotNull TemplateActionContext context) {
            if (!(context.getFile() instanceof PipoFile)) {
                return false;
            }
            PsiElement at = context.getFile().findElementAt(context.getStartOffset());
            return !(at instanceof PsiComment) && !PipoPsiUtil.is(at, PipoTokenTypes.STRING);
        }
    }
}
