package com.pipo.lang.format;

import com.intellij.formatting.Block;
import com.intellij.formatting.ChildAttributes;
import com.intellij.formatting.FormattingContext;
import com.intellij.formatting.FormattingModel;
import com.intellij.formatting.FormattingModelBuilder;
import com.intellij.formatting.FormattingModelProvider;
import com.intellij.formatting.Indent;
import com.intellij.formatting.Spacing;
import com.intellij.formatting.SpacingBuilder;
import com.intellij.lang.ASTNode;
import com.intellij.psi.TokenType;
import com.intellij.psi.codeStyle.CodeStyleSettings;
import com.intellij.psi.formatter.common.AbstractBlock;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.PipoLanguage;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.List;

import static com.pipo.lang.lexer.PipoTokenTypes.*;
import static com.pipo.lang.parser.PipoElementTypes.*;

/**
 * Re-indents code and normalizes a few spaces. In PiPo whitespace has meaning
 * (a newline ends a statement; f[x] indexes but f [x] passes an array), so the
 * formatter never joins or splits lines and only touches spaces where they
 * cannot change the meaning: around ',', '=', '->', and before a block's '{'.
 */
public final class PipoFormattingModelBuilder implements FormattingModelBuilder {
    @Override
    public @NotNull FormattingModel createModel(@NotNull FormattingContext context) {
        CodeStyleSettings settings = context.getCodeStyleSettings();
        SpacingBuilder spacing = new SpacingBuilder(settings, PipoLanguage.INSTANCE)
                .before(COMMA).spaceIf(false)
                .after(COMMA).spaceIf(true)
                .around(ARROW).spaces(1)
                .aroundInside(ASSIGN, VAR_DECL).spaces(1)
                .aroundInside(ASSIGN, ASSIGN_STMT).spaces(1)
                .around(COMPOUND).spaces(1)
                .before(BLOCK).spaces(1);
        return FormattingModelProvider.createFormattingModelForPsiFile(context.getContainingFile(),
                new PipoBlock(context.getContainingFile().getNode(), Indent.getNoneIndent(), spacing), settings);
    }

    static final class PipoBlock extends AbstractBlock {
        private final Indent indent;
        private final SpacingBuilder spacing;

        PipoBlock(@NotNull ASTNode node, Indent indent, SpacingBuilder spacing) {
            super(node, null, null);
            this.indent = indent;
            this.spacing = spacing;
        }

        /** the bracket pair whose contents are indented, for a node type */
        private static IElementType[] brackets(IElementType t) {
            if (BRACED.contains(t)) {
                return new IElementType[]{LBRACE, RBRACE};
            }
            if (t == ARRAY_EXPR || t == INDEX_EXPR) {
                return new IElementType[]{LBRACKET, RBRACKET};
            }
            if (t == ARG_LIST || t == PARAM_LIST || t == PAREN_EXPR || t == PAREN_TYPE || t == CALLBACK_TYPE) {
                return new IElementType[]{LPAREN, RPAREN};
            }
            return null;
        }

        @Override
        protected List<Block> buildChildren() {
            List<Block> out = new ArrayList<>();
            IElementType parent = myNode.getElementType();
            IElementType[] pair = brackets(parent);
            boolean inside = false;
            boolean first = true;
            for (ASTNode c = myNode.getFirstChildNode(); c != null; c = c.getTreeNext()) {
                IElementType t = c.getElementType();
                if (t == TokenType.WHITE_SPACE || c.getTextLength() == 0) {
                    continue;
                }
                Indent ind = Indent.getNoneIndent();
                if (pair != null && !inside && t == pair[0]) {
                    inside = true;
                } else if (pair != null && inside && t == pair[1]) {
                    inside = false;
                } else if (inside) {
                    ind = Indent.getNormalIndent();
                } else if (parent == CHAIN_EXPR && !first) {
                    ind = Indent.getContinuationIndent(); // a + \n b
                } else if (parent == METHOD_CALL_EXPR && t == DOT) {
                    ind = Indent.getContinuationIndent(); // xs \n .map(...)
                }
                out.add(new PipoBlock(c, ind, spacing));
                first = false;
            }
            return out;
        }

        @Override
        public Indent getIndent() {
            return indent;
        }

        @Override
        public @Nullable Spacing getSpacing(@Nullable Block child1, @NotNull Block child2) {
            return spacing.getSpacing(this, child1, child2);
        }

        @Override
        public @NotNull ChildAttributes getChildAttributes(int newChildIndex) {
            IElementType[] pair = brackets(myNode.getElementType());
            if (pair != null) {
                return new ChildAttributes(Indent.getNormalIndent(), null);
            }
            return new ChildAttributes(Indent.getNoneIndent(), null);
        }

        @Override
        public boolean isLeaf() {
            return myNode.getFirstChildNode() == null;
        }
    }
}
