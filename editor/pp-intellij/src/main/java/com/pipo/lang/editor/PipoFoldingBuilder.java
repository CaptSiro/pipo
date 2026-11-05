package com.pipo.lang.editor;

import com.intellij.lang.ASTNode;
import com.intellij.lang.folding.FoldingBuilderEx;
import com.intellij.lang.folding.FoldingDescriptor;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.util.TextRange;
import com.intellij.psi.PsiComment;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiWhiteSpace;
import com.intellij.psi.tree.IElementType;
import com.intellij.psi.util.PsiTreeUtil;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.PipoImportStmt;
import com.pipo.lang.psi.PipoPsiUtil;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

/** Folds { ... } bodies, multi-line [ ... ] and ( ... ), comments and the import section. */
public final class PipoFoldingBuilder extends FoldingBuilderEx implements DumbAware {
    @Override
    public FoldingDescriptor @NotNull [] buildFoldRegions(@NotNull PsiElement root, @NotNull Document document, boolean quick) {
        List<FoldingDescriptor> out = new ArrayList<>();
        for (PsiElement e : PsiTreeUtil.collectElements(root, x -> true)) {
            IElementType t = PipoPsiUtil.typeOf(e);
            if (PipoElementTypes.BRACED.contains(t)) {
                fold(e, PipoTokenTypes.LBRACE, PipoTokenTypes.RBRACE, "{...}", document, out);
            } else if (t == PipoElementTypes.ARRAY_EXPR) {
                fold(e, PipoTokenTypes.LBRACKET, PipoTokenTypes.RBRACKET, "[...]", document, out);
            } else if (t == PipoElementTypes.ARG_LIST || t == PipoElementTypes.PARAM_LIST) {
                fold(e, PipoTokenTypes.LPAREN, PipoTokenTypes.RPAREN, "(...)", document, out);
            } else if (t == PipoTokenTypes.BLOCK_COMMENT) {
                addIfMultiline(e.getNode(), e.getTextRange(), "/*...*/", document, out);
            } else if (t == PipoTokenTypes.LINE_COMMENT) {
                foldLineComments((PsiComment) e, document, out);
            }
        }
        foldImports(root, document, out);
        return out.toArray(FoldingDescriptor.EMPTY_ARRAY);
    }

    private static void fold(PsiElement e, IElementType open, IElementType close, String placeholder, Document doc,
                             List<FoldingDescriptor> out) {
        ASTNode l = e.getNode().findChildByType(open);
        ASTNode r = e.getNode().findChildByType(close);
        if (l == null || r == null) {
            return;
        }
        addIfMultiline(e.getNode(), new TextRange(l.getStartOffset(), r.getStartOffset() + 1), placeholder, doc, out);
    }

    private static void addIfMultiline(ASTNode node, TextRange range, String placeholder, Document doc,
                                       List<FoldingDescriptor> out) {
        if (range.getLength() < 2 || range.getEndOffset() > doc.getTextLength()) {
            return;
        }
        if (doc.getLineNumber(range.getStartOffset()) == doc.getLineNumber(range.getEndOffset())) {
            return;
        }
        out.add(new FoldingDescriptor(node, range, null, placeholder));
    }

    /* a run of // lines folds into its first line */
    private static void foldLineComments(PsiComment first, Document doc, List<FoldingDescriptor> out) {
        PsiElement prev = PsiTreeUtil.skipSiblingsBackward(first, PsiWhiteSpace.class);
        if (PipoPsiUtil.is(prev, PipoTokenTypes.LINE_COMMENT) && !blankLineBetween(prev, first)) {
            return; // not the first of the run
        }
        PsiElement last = first;
        PsiElement next = PsiTreeUtil.skipSiblingsForward(first, PsiWhiteSpace.class);
        while (PipoPsiUtil.is(next, PipoTokenTypes.LINE_COMMENT) && !blankLineBetween(last, next)) {
            last = next;
            next = PsiTreeUtil.skipSiblingsForward(next, PsiWhiteSpace.class);
        }
        if (last != first) {
            String text = first.getText();
            String placeholder = text.length() > 60 ? text.substring(0, 60) + "..." : text + " ...";
            addIfMultiline(first.getNode(), new TextRange(first.getTextRange().getStartOffset(),
                    last.getTextRange().getEndOffset()), placeholder, doc, out);
        }
    }

    private static boolean blankLineBetween(PsiElement a, PsiElement b) {
        PsiElement ws = a.getNextSibling();
        return ws instanceof PsiWhiteSpace && ws != b && ws.getText().chars().filter(c -> c == '\n').count() > 1;
    }

    private static void foldImports(PsiElement root, Document doc, List<FoldingDescriptor> out) {
        PipoImportStmt first = null;
        PipoImportStmt last = null;
        for (PsiElement c = root.getFirstChild(); c != null; c = c.getNextSibling()) {
            if (c instanceof PipoImportStmt imp) {
                if (first == null) {
                    first = imp;
                }
                last = imp;
            }
        }
        if (first == null || first == last || first.getModulePath() == null) {
            return;
        }
        int start = first.getModulePath().getTextRange().getStartOffset();
        addIfMultiline(first.getNode(), new TextRange(start, last.getTextRange().getEndOffset()), "...", doc, out);
    }

    @Override
    public @Nullable String getPlaceholderText(@NotNull ASTNode node) {
        return "...";
    }

    @Override
    public boolean isCollapsedByDefault(@NotNull ASTNode node) {
        return false;
    }
}
