package com.pipo.lang.editor;

import com.intellij.codeInsight.editorActions.SimpleTokenSetQuoteHandler;
import com.intellij.lang.BracePair;
import com.intellij.lang.Commenter;
import com.intellij.lang.PairedBraceMatcher;
import com.intellij.lang.refactoring.NamesValidator;
import com.intellij.lang.refactoring.RefactoringSupportProvider;
import com.intellij.openapi.project.Project;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.psi.PipoElementFactory;
import com.pipo.lang.psi.PipoImportStmt;
import com.pipo.lang.psi.PipoLocalBinding;
import com.pipo.lang.psi.PipoNamedElement;
import com.pipo.lang.psi.PipoParam;
import com.pipo.lang.psi.PipoVarDecl;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/** The small language services, grouped in one file. */
public final class PipoBasics {
    private PipoBasics() {
    }

    public static final class BraceMatcher implements PairedBraceMatcher {
        private static final BracePair[] PAIRS = {
                new BracePair(PipoTokenTypes.LPAREN, PipoTokenTypes.RPAREN, false),
                new BracePair(PipoTokenTypes.LBRACKET, PipoTokenTypes.RBRACKET, false),
                new BracePair(PipoTokenTypes.LBRACE, PipoTokenTypes.RBRACE, true),
        };

        @Override
        public BracePair @NotNull [] getPairs() {
            return PAIRS;
        }

        @Override
        public boolean isPairedBracesAllowedBeforeType(@NotNull IElementType lbraceType, @Nullable IElementType next) {
            return true;
        }

        @Override
        public int getCodeConstructStart(PsiFile file, int openingBraceOffset) {
            return openingBraceOffset;
        }
    }

    public static final class QuoteHandler extends SimpleTokenSetQuoteHandler {
        public QuoteHandler() {
            super(PipoTokenTypes.STRING);
        }
    }

    public static final class PipoCommenter implements Commenter {
        @Override
        public String getLineCommentPrefix() {
            return "//";
        }

        @Override
        public String getBlockCommentPrefix() {
            return "/*";
        }

        @Override
        public String getBlockCommentSuffix() {
            return "*/";
        }

        @Override
        public String getCommentedBlockCommentPrefix() {
            return null;
        }

        @Override
        public String getCommentedBlockCommentSuffix() {
            return null;
        }
    }

    public static final class Refactoring extends RefactoringSupportProvider {
        @Override
        public boolean isMemberInplaceRenameAvailable(@NotNull PsiElement element, @Nullable PsiElement context) {
            return element instanceof PipoNamedElement && !(element instanceof PipoImportStmt);
        }

        @Override
        public boolean isInplaceRenameAvailable(@NotNull PsiElement element, PsiElement context) {
            return element instanceof PipoParam || element instanceof PipoLocalBinding
                    || (element instanceof PipoVarDecl v && !v.isTopLevel());
        }
    }

    public static final class Names implements NamesValidator {
        @Override
        public boolean isKeyword(@NotNull String name, Project project) {
            return PipoTokenTypes.KEYWORDS.containsKey(name);
        }

        @Override
        public boolean isIdentifier(@NotNull String name, Project project) {
            return PipoElementFactory.isIdentifier(name);
        }
    }
}
