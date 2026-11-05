package com.pipo.lang.structure;

import com.intellij.icons.AllIcons;
import com.intellij.ide.structureView.StructureViewBuilder;
import com.intellij.ide.structureView.StructureViewModel;
import com.intellij.ide.structureView.StructureViewModelBase;
import com.intellij.ide.structureView.StructureViewTreeElement;
import com.intellij.ide.structureView.TreeBasedStructureViewBuilder;
import com.intellij.ide.util.treeView.smartTree.SortableTreeElement;
import com.intellij.ide.util.treeView.smartTree.Sorter;
import com.intellij.ide.util.treeView.smartTree.TreeElement;
import com.intellij.lang.PsiStructureViewFactory;
import com.intellij.navigation.ItemPresentation;
import com.intellij.openapi.editor.Editor;
import com.intellij.psi.NavigatablePsiElement;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.pipo.lang.PipoIcons;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;
import java.util.ArrayList;
import java.util.List;

/**
 * The Structure tool window: top-level declarations, enum variants, interface
 * functions, `impl` statements, and functions / types declared inside functions.
 */
public final class PipoStructureView implements PsiStructureViewFactory {
    @Override
    public @Nullable StructureViewBuilder getStructureViewBuilder(@NotNull PsiFile psiFile) {
        return new TreeBasedStructureViewBuilder() {
            @Override
            public @NotNull StructureViewModel createStructureViewModel(@Nullable Editor editor) {
                return new Model(psiFile, editor);
            }
        };
    }

    private static final class Model extends StructureViewModelBase implements StructureViewModel.ElementInfoProvider {
        Model(PsiFile file, @Nullable Editor editor) {
            super(file, editor, new Element(file));
            withSorters(Sorter.ALPHA_SORTER);
            withSuitableClasses(PipoNamedElement.class);
        }

        @Override
        public boolean isAlwaysShowsPlus(StructureViewTreeElement element) {
            return false;
        }

        @Override
        public boolean isAlwaysLeaf(StructureViewTreeElement element) {
            return element.getValue() instanceof PipoVarDecl || element.getValue() instanceof PipoEnumVariant;
        }
    }

    private static final class Element implements StructureViewTreeElement, SortableTreeElement {
        private final NavigatablePsiElement element;

        Element(NavigatablePsiElement element) {
            this.element = element;
        }

        @Override
        public Object getValue() {
            return element;
        }

        @Override
        public void navigate(boolean requestFocus) {
            element.navigate(requestFocus);
        }

        @Override
        public boolean canNavigate() {
            return element.canNavigate();
        }

        @Override
        public boolean canNavigateToSource() {
            return element.canNavigateToSource();
        }

        @Override
        public @NotNull String getAlphaSortKey() {
            String n = element.getName();
            return n != null ? n : "";
        }

        @Override
        public @NotNull ItemPresentation getPresentation() {
            if (element instanceof PsiFile f) {
                return presentation(f.getName(), PipoIcons.FILE);
            }
            if (element instanceof PipoNamedElement n) {
                return presentation(n.presentableText(), n.kindIcon());
            }
            if (PipoPsiUtil.is(element, PipoElementTypes.IMPL_STMT)) {
                return presentation(PipoPsiUtil.collapse(element.getText()), AllIcons.Nodes.Interface);
            }
            ItemPresentation p = element.getPresentation();
            return p != null ? p : presentation(element.getText(), null);
        }

        private static ItemPresentation presentation(String text, @Nullable Icon icon) {
            return new ItemPresentation() {
                @Override
                public String getPresentableText() {
                    return text;
                }

                @Override
                public Icon getIcon(boolean unused) {
                    return icon;
                }
            };
        }

        @Override
        public TreeElement @NotNull [] getChildren() {
            List<TreeElement> out = new ArrayList<>();
            if (element instanceof PsiFile f) {
                for (PsiElement s : PipoPsiUtil.statements(f)) {
                    addStatement(PipoPsiUtil.unwrapExport(s), out);
                }
            } else if (element instanceof PipoEnumDecl e) {
                for (PipoEnumVariant v : e.getVariants()) {
                    out.add(new Element(v));
                }
            } else if (element instanceof PipoInterfaceDecl i) {
                for (PipoFunctionDecl r : i.getRequirements()) {
                    out.add(new Element(r));
                }
            } else if (element instanceof PipoTypeDecl t && t.getDefinition() instanceof PipoElement def
                    && def.type() == PipoElementTypes.RECORD_TYPE) {
                for (PipoRecordField field : def.children(PipoRecordField.class)) {
                    out.add(new Element(field));
                }
            } else if (element instanceof PipoFunctionDecl fn && fn.getBody() != null) {
                for (PsiElement s : PipoPsiUtil.statements(fn.getBody())) {
                    if (s instanceof PipoFunctionDecl || s instanceof PipoTypeDecl || s instanceof PipoEnumDecl
                            || s instanceof PipoInterfaceDecl) {
                        out.add(new Element((NavigatablePsiElement) s));
                    }
                }
            }
            return out.toArray(TreeElement.EMPTY_ARRAY);
        }

        private static void addStatement(PsiElement s, List<TreeElement> out) {
            if (s instanceof PipoImportStmt) {
                return;
            }
            if (s instanceof PipoNamedElement n && n.getName() != null) {
                out.add(new Element(n));
            } else if (PipoPsiUtil.is(s, PipoElementTypes.IMPL_STMT)) {
                out.add(new Element((NavigatablePsiElement) s));
            }
        }
    }
}
