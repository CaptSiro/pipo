package com.pipo.lang.completion;

import com.intellij.codeInsight.completion.AddSpaceInsertHandler;
import com.intellij.codeInsight.completion.CompletionContributor;
import com.intellij.codeInsight.completion.CompletionInitializationContext;
import com.intellij.codeInsight.completion.CompletionParameters;
import com.intellij.codeInsight.completion.CompletionProvider;
import com.intellij.codeInsight.completion.CompletionResultSet;
import com.intellij.codeInsight.completion.CompletionType;
import com.intellij.codeInsight.completion.PrioritizedLookupElement;
import com.intellij.codeInsight.completion.util.ParenthesesInsertHandler;
import com.intellij.codeInsight.lookup.LookupElement;
import com.intellij.codeInsight.lookup.LookupElementBuilder;
import com.intellij.icons.AllIcons;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.patterns.PlatformPatterns;
import com.intellij.psi.PsiComment;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.util.ProcessingContext;
import com.pipo.lang.PipoFileType;
import com.pipo.lang.PipoLanguage;
import com.pipo.lang.lexer.PipoTokenTypes;
import com.pipo.lang.parser.PipoBuiltinNames;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import com.pipo.lang.resolve.PipoModules;
import com.pipo.lang.resolve.PipoResolver;
import org.jetbrains.annotations.NotNull;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Completion of names in scope (with every overload), module exports after
 * {@code module:}, enum variants after {@code Enum:}, record fields and object
 * keys after {@code value:}, type names in type positions, module names after
 * {@code import}, and keywords.
 */
public final class PipoCompletionContributor extends CompletionContributor {
    private static final List<String> STATEMENT_KEYWORDS = List.of("let", "const", "function", "if", "for", "while",
            "match", "return", "break", "continue", "enum", "type", "interface", "impl", "import", "export", "global",
            "native", "precedence");
    private static final List<String> EXPRESSION_KEYWORDS = List.of("true", "false", "function", "if", "match");

    public PipoCompletionContributor() {
        extend(CompletionType.BASIC, PlatformPatterns.psiElement().withLanguage(PipoLanguage.INSTANCE), new Provider());
    }

    @Override
    public void beforeCompletion(@NotNull CompletionInitializationContext context) {
        // spaces matter in PiPo (f [x] vs f[x]), so the dummy identifier must not add one
        context.setDummyIdentifier(CompletionInitializationContext.DUMMY_IDENTIFIER_TRIMMED);
    }

    private static final class Provider extends CompletionProvider<CompletionParameters> {
        @Override
        protected void addCompletions(@NotNull CompletionParameters parameters, @NotNull ProcessingContext context,
                                      @NotNull CompletionResultSet result) {
            PsiElement pos = parameters.getPosition();
            if (pos instanceof PsiComment) {
                return;
            }
            PsiElement parent = pos.getParent();
            if (PipoPsiUtil.is(pos, PipoTokenTypes.STRING)) {
                if (parent instanceof PipoModulePath) {
                    int start = pos.getTextRange().getStartOffset() + 1;
                    int caret = parameters.getOffset();
                    String typed = caret > start ? pos.getContainingFile().getText().substring(start, caret) : "";
                    addFilePaths(parameters.getOriginalFile(), result.withPrefixMatcher(typed));
                }
                return;
            }
            if (parent instanceof PipoModulePath) {
                for (String m : PipoModules.availableModules(parameters.getOriginalFile())) {
                    result.addElement(LookupElementBuilder.create(m).withIcon(AllIcons.Nodes.Package).withTypeText("module"));
                }
                return;
            }
            if (parent instanceof PipoFieldExpr field && field.getQualifier() != null) {
                addAll(PipoResolver.membersOfQualifier(field.getQualifier()), false, result);
                return;
            }
            if (parent instanceof PipoTypeRef ref) {
                addTypes(ref, result);
                return;
            }
            if (parent instanceof PipoRefExpr ref) {
                boolean dotCall = PipoPsiUtil.is(ref.getParent(), PipoElementTypes.METHOD_CALL_EXPR);
                addAll(PipoResolver.visible(ref, PipoResolver.Kind.VALUE), dotCall, result);
                if (!dotCall) {
                    addKeywords(atStatementStart(ref) ? STATEMENT_KEYWORDS : EXPRESSION_KEYWORDS, result);
                }
                return;
            }
            if (PipoPsiUtil.is(parent, PipoElementTypes.NAME) || PipoPsiUtil.is(pos, PipoTokenTypes.INT)
                    || PipoPsiUtil.is(pos, PipoTokenTypes.FLOAT)) {
                return; // naming something new
            }
            addKeywords(STATEMENT_KEYWORDS, result);
        }
    }

    private static boolean atStatementStart(PsiElement ref) {
        PsiElement stmt = PipoPsiUtil.statementOf(ref);
        return stmt != null && stmt.getTextRange().getStartOffset() == ref.getTextRange().getStartOffset();
    }

    private static void addKeywords(List<String> keywords, CompletionResultSet result) {
        for (String kw : keywords) {
            result.addElement(PrioritizedLookupElement.withPriority(
                    LookupElementBuilder.create(kw).bold().withInsertHandler(AddSpaceInsertHandler.INSTANCE), -1));
        }
    }

    private static void addTypes(PipoTypeRef ref, CompletionResultSet result) {
        if (ref.isQualifierPart()) {
            for (Map.Entry<String, List<PsiElement>> e : PipoResolver.visible(ref, PipoResolver.Kind.TYPE).entrySet()) {
                if (e.getValue().get(0) instanceof PipoImportStmt imp) {
                    result.addElement(lookup(e.getKey(), imp, false));
                }
            }
            return;
        }
        if (ref.getQualifier() != null) {
            for (PsiElement q : PipoResolver.resolveTypeRef(ref.getQualifier())) {
                if (q instanceof PipoImportStmt imp) {
                    PsiFile module = PipoModules.resolve(imp);
                    if (module != null) {
                        PipoResolver.moduleExports(module, (name, e) -> {
                            if (name != null && isType(e)) {
                                result.addElement(lookup(name, e, false));
                            }
                        });
                    }
                }
            }
            return;
        }
        for (Map.Entry<String, List<PsiElement>> e : PipoResolver.visible(ref, PipoResolver.Kind.TYPE).entrySet()) {
            result.addElement(lookup(e.getKey(), e.getValue().get(0), false));
        }
        for (String prim : PipoBuiltinNames.PRIMITIVES) {
            result.addElement(LookupElementBuilder.create(prim).bold().withTypeText("built-in type"));
        }
        result.addElement(LookupElementBuilder.create("tuple").bold().withTypeText("built-in type").withTailText("<...>"));
        result.addElement(LookupElementBuilder.create("function").bold().withTypeText("any function"));
    }

    private static boolean isType(PsiElement e) {
        return e instanceof PipoTypeDecl || e instanceof PipoEnumDecl || e instanceof PipoInterfaceDecl;
    }

    private static void addAll(Map<String, List<PsiElement>> names, boolean functionsOnly, CompletionResultSet result) {
        for (Map.Entry<String, List<PsiElement>> e : names.entrySet()) {
            String name = e.getKey();
            if (!PipoElementFactory.isIdentifier(name)) {
                continue; // operators are not typed by name
            }
            Set<String> seen = new HashSet<>();
            for (PsiElement decl : e.getValue()) {
                boolean callable = decl instanceof PipoFunctionDecl || (decl instanceof PipoEnumVariant v && v.isConstructor());
                if (functionsOnly && !callable) {
                    continue;
                }
                LookupElement l = lookup(name, decl, callable);
                if (seen.add(l.getLookupString() + "|" + presentableTail(decl))) {
                    result.addElement(l);
                }
            }
        }
    }

    private static String presentableTail(PsiElement decl) {
        return decl instanceof PipoNamedElement n ? n.presentableText() : "";
    }

    private static LookupElement lookup(String name, PsiElement decl, boolean call) {
        LookupElementBuilder b = LookupElementBuilder.create(decl, name);
        if (decl instanceof PipoNamedElement n) {
            b = b.withIcon(n.kindIcon());
        }
        if (decl instanceof PipoFunctionDecl f) {
            String sig = f.signature();
            int arrow = sig.lastIndexOf(") -> ");
            String params = arrow >= 0 ? sig.substring(0, arrow + 1) : sig;
            b = b.withTailText(params, true);
            if (f.getReturnType() != null) {
                b = b.withTypeText(PipoPsiUtil.collapse(f.getReturnType().getText()));
            }
            if (call) {
                boolean none = f.getParams().isEmpty() && !f.isVariadic();
                b = b.withInsertHandler(none ? ParenthesesInsertHandler.NO_PARAMETERS : ParenthesesInsertHandler.WITH_PARAMETERS);
            }
        } else if (decl instanceof PipoEnumVariant v) {
            b = b.withTypeText(v.getEnum() != null ? v.getEnum().getName() : "variant");
            String text = v.presentableText();
            if (v.isConstructor()) {
                b = b.withTailText(text.substring(name.length()), true);
                if (call) {
                    b = b.withInsertHandler(text.endsWith("()") ? ParenthesesInsertHandler.NO_PARAMETERS
                            : ParenthesesInsertHandler.WITH_PARAMETERS);
                }
            }
        } else if (decl instanceof PipoVarDecl v && v.getTypeElement() != null) {
            b = b.withTypeText(PipoPsiUtil.collapse(v.getTypeElement().getText()));
        } else if (decl instanceof PipoParam p && p.getTypeElement() != null) {
            b = b.withTypeText(PipoPsiUtil.collapse(p.getTypeElement().getText()));
        } else if (decl instanceof PipoRecordField f && f.getTypeElement() != null) {
            b = b.withTypeText(PipoPsiUtil.collapse(f.getTypeElement().getText()));
        } else if (decl instanceof PipoNamedElement n) {
            b = b.withTypeText(n.kindName());
        }
        // names from the file itself first, then imports, then builtins
        int priority = PipoPsiUtil.isInBuiltinStub(decl) ? 0 : decl instanceof PipoNamedElement n && n.isTopLevel() ? 5 : 10;
        return PrioritizedLookupElement.withPriority(b, priority);
    }

    /** "./name" for the .pp files and folders next to the current file */
    private static void addFilePaths(PsiFile file, CompletionResultSet result) {
        VirtualFile vf = file.getOriginalFile().getVirtualFile();
        VirtualFile dir = vf != null ? vf.getParent() : null;
        if (dir == null) {
            return;
        }
        CompletionResultSet r = result;
        for (VirtualFile c : dir.getChildren()) {
            if (c.equals(vf)) {
                continue;
            }
            if (c.isDirectory()) {
                r.addElement(LookupElementBuilder.create("./" + c.getName() + "/").withIcon(AllIcons.Nodes.Folder));
            } else if (PipoFileType.EXTENSION.equals(c.getExtension())) {
                r.addElement(LookupElementBuilder.create("./" + c.getNameWithoutExtension()).withIcon(PipoFileType.INSTANCE.getIcon())
                        .withTypeText(c.getName()));
            }
        }
    }
}
