package com.pipo.lang.resolve;

import com.intellij.psi.PsiDirectory;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiReference;
import com.intellij.psi.util.PsiTreeUtil;
import com.intellij.psi.tree.IElementType;
import com.pipo.lang.parser.PipoElementTypes;
import com.pipo.lang.psi.*;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BiConsumer;

import static com.pipo.lang.psi.PipoPsiUtil.is;
import static com.pipo.lang.psi.PipoPsiUtil.typeOf;

/**
 * Name lookup, following the interpreter's scoping:
 * <ul>
 *   <li>blocks, function parameters, loop variables and pattern bindings, innermost first;</li>
 *   <li>a let / const is visible after its statement, or anywhere inside a function body
 *       (bodies look names up when they run);</li>
 *   <li>functions, types, enums and interfaces of a block are visible in the whole block;</li>
 *   <li>functions are overload sets: lookup continues outwards and collects every overload,
 *       while a variable hides outer names;</li>
 *   <li>then the `global` functions of imported modules, then the builtins.</li>
 * </ul>
 */
public final class PipoResolver {
    public enum Kind {
        /** a name in an expression: variables, functions, and types (callable as constructors) */
        VALUE,
        /** a name in a type position */
        TYPE
    }

    private interface Visitor {
        void visit(@Nullable String name, @NotNull PsiElement decl);

        /** called after each scope level; true stops the walk */
        boolean endLevel();
    }

    private PipoResolver() {
    }

    /* ------------------------------------------------------------ public API */

    public static List<PsiElement> resolveName(PsiElement place, String name, Kind kind) {
        ByName v = new ByName(name);
        walk(place, kind, v);
        return v.result;
    }

    /** every visible declaration, by name; overloads are all kept, shadowed variables are not */
    public static Map<String, List<PsiElement>> visible(PsiElement place, Kind kind) {
        Collect v = new Collect();
        walk(place, kind, v);
        return v.map;
    }

    public static List<PsiElement> resolveRefExpr(PipoRefExpr ref) {
        String name = ref.getReferencedName();
        List<PsiElement> found = resolveName(ref, name, Kind.VALUE);
        PsiElement parent = ref.getParent();
        if (found.isEmpty() && is(parent, PipoElementTypes.METHOD_CALL_EXPR)) {
            // dot calls also look in the module of the receiver's type: try the imported modules
            for (PipoImportStmt imp : PipoPsiUtil.imports(ref.getContainingFile())) {
                PsiFile module = PipoModules.resolve(imp);
                if (module != null && !imp.isDestructured()) {
                    for (PsiElement e : moduleExports(module, name)) {
                        if (e instanceof PipoFunctionDecl) {
                            found.add(e);
                        }
                    }
                }
            }
        }
        return found;
    }

    public static List<PsiElement> resolveField(PipoFieldExpr f) {
        String name = f.getFieldName();
        List<PsiElement> out = new ArrayList<>();
        if (name == null || f.getQualifier() == null) {
            return out;
        }
        for (PsiElement t : targetsOf(f.getQualifier(), 0)) {
            members(t, (n, e) -> {
                if (name.equals(n) && !out.contains(e)) {
                    out.add(e);
                }
            }, 0);
        }
        return out;
    }

    /** every member of what qualifier refers to: module exports, enum variants, record fields, object keys */
    public static Map<String, List<PsiElement>> membersOfQualifier(PsiElement qualifier) {
        Map<String, List<PsiElement>> out = new LinkedHashMap<>();
        for (PsiElement t : targetsOf(qualifier, 0)) {
            members(t, (n, e) -> {
                if (n != null) {
                    List<PsiElement> l = out.computeIfAbsent(n, k -> new ArrayList<>());
                    if (!l.contains(e)) {
                        l.add(e);
                    }
                }
            }, 0);
        }
        return out;
    }

    public static List<PsiElement> resolveTypeRef(PipoTypeRef t) {
        String name = t.getTypeName();
        if (name == null) {
            return List.of();
        }
        PipoTypeRef qualifier = t.getQualifier();
        if (qualifier == null) {
            return resolveName(t, name, Kind.TYPE);
        }
        List<PsiElement> out = new ArrayList<>();
        for (PsiElement q : resolveTypeRef(qualifier)) {
            members(q, (n, e) -> {
                if (name.equals(n) && !out.contains(e)) {
                    out.add(e);
                }
            }, 0);
        }
        return out;
    }

    /** the exports of a module file named name (several for an overload set) */
    public static List<PsiElement> moduleExports(PsiFile module, String name) {
        List<PsiElement> out = new ArrayList<>();
        moduleExports(module, (n, e) -> {
            if (name.equals(n) && !out.contains(e)) {
                out.add(e);
            }
        });
        return out;
    }

    /** reports (exported name, declaration) for each export of a module file */
    public static void moduleExports(PsiFile module, BiConsumer<String, PsiElement> sink) {
        for (PsiElement s : PipoPsiUtil.statements(module)) {
            if (s instanceof PipoImportStmt imp) {
                if (!imp.isReexport()) {
                    continue;
                }
                if (!imp.isDestructured()) {
                    sink.accept(imp.getName(), imp); // export import "./shapes": the module itself
                    continue;
                }
                for (PipoImportName n : imp.getImportNames()) { // export import "./shapes" -> { rect }
                    List<PsiElement> targets = targets(n.getReference());
                    if (targets.isEmpty()) {
                        sink.accept(n.getName(), n);
                    }
                    for (PsiElement t : targets) {
                        sink.accept(n.getName(), t);
                    }
                }
                continue;
            }
            if (!is(s, PipoElementTypes.EXPORT_STMT)) {
                continue;
            }
            PsiElement d = PipoPsiUtil.unwrapExport(s);
            if (d != s && d instanceof PipoNamedElement n) {
                sink.accept(n.getName(), n);
                continue;
            }
            for (PsiElement item : ((PipoElement) s).children(PipoElementTypes.EXPORT_ITEM)) {
                PipoRefExpr ref = ((PipoElement) item).child(PipoRefExpr.class);
                if (ref == null) {
                    continue;
                }
                PsiElement alias = ((PipoElement) item).child(PipoElementTypes.NAME);
                String exported = alias != null ? alias.getText() : ref.getReferencedName();
                List<PsiElement> targets = targets(ref.getReference());
                targets.removeIf(PipoPsiUtil::isInBuiltinStub);
                if (targets.isEmpty()) {
                    sink.accept(exported, item);
                }
                for (PsiElement t : targets) {
                    sink.accept(exported, t);
                }
            }
        }
    }

    /* --------------------------------------------------------- scope walking */

    private static void walk(PsiElement place, Kind kind, Visitor v) {
        PsiElement child = place;
        PsiElement scope = place.getParent();
        boolean deferred = false;
        int placeStart = place.getTextRange().getStartOffset();
        while (scope != null && !(scope instanceof PsiDirectory)) {
            IElementType t = typeOf(scope);
            if (scope instanceof PsiFile || t == PipoElementTypes.BLOCK) {
                blockDeclarations(scope, child, placeStart, deferred, kind, v);
            } else if (scope instanceof PipoFunctionDecl || t == PipoElementTypes.LAMBDA_EXPR) {
                IElementType ct = typeOf(child);
                boolean inBody = ct != PipoElementTypes.PARAM_LIST && ct != PipoElementTypes.PARAM
                        && ct != PipoElementTypes.NAME && ct != PipoElementTypes.GENERIC_PARAMS
                        && !PipoElementTypes.TYPES.contains(ct);
                if (inBody && kind == Kind.VALUE) {
                    for (PipoParam p : parameters(scope)) {
                        v.visit(p.getName(), p);
                    }
                    if (scope instanceof PipoFunctionDecl f && f.isExpression() && f.getName() != null) {
                        v.visit(f.getName(), f); // a named function expression can call itself
                    }
                }
                genericParams(scope, kind, v);
                if (inBody) {
                    deferred = true;
                }
            } else if (scope instanceof PipoTypeDecl || scope instanceof PipoInterfaceDecl
                    || scope instanceof PipoEnumDecl || t == PipoElementTypes.IMPL_STMT) {
                genericParams(scope, kind, v);
                if (scope instanceof PipoInterfaceDecl iface && kind == Kind.VALUE) {
                    // default implementations call the interface's other functions
                    for (PipoFunctionDecl r : iface.getRequirements()) {
                        v.visit(r.getName(), r);
                    }
                }
            } else if (t == PipoElementTypes.FOR_STMT) {
                if (kind == Kind.VALUE && typeOf(child) == PipoElementTypes.BLOCK) {
                    for (PipoLocalBinding var : ((PipoElement) scope).children(PipoLocalBinding.class)) {
                        v.visit(var.getName(), var);
                    }
                }
            } else if (t == PipoElementTypes.MATCH_ARM) {
                if (kind == Kind.VALUE && typeOf(child) != PipoElementTypes.PATTERN) {
                    patternBindings(((PipoElement) scope).child(PipoElementTypes.PATTERN), v);
                }
            } else if (t == PipoElementTypes.IF_EXPR) {
                if (kind == Kind.VALUE && child == ((PipoElement) scope).child(PipoElementTypes.BLOCK)) {
                    patternBindings(((PipoElement) scope).child(PipoElementTypes.PATTERN), v);
                }
            }

            if (v.endLevel()) {
                return;
            }
            if (scope instanceof PsiFile) {
                break;
            }
            child = scope;
            scope = scope.getParent();
        }

        PsiFile file = place.getContainingFile();
        if (file == null) {
            return;
        }
        // `global` functions of imported modules are visible everywhere once loaded
        if (kind == Kind.VALUE) {
            for (PsiFile module : PipoModules.loadedModules(file)) {
                for (PipoNamedElement d : PipoPsiUtil.topLevelDeclarations(module)) {
                    if (d instanceof PipoFunctionDecl f && f.isGlobal()) {
                        v.visit(f.getName(), f);
                    }
                }
            }
            if (v.endLevel()) {
                return;
            }
        }

        PsiFile builtins = PipoBuiltins.getInstance(file.getProject()).builtinsFile();
        if (builtins != null && builtins != file.getOriginalFile()) {
            blockDeclarations(builtins, null, Integer.MAX_VALUE, true, kind, v);
            v.endLevel();
        }
    }

    private static List<PipoParam> parameters(PsiElement fn) {
        PipoElement e = (PipoElement) fn;
        PsiElement list = e.child(PipoElementTypes.PARAM_LIST);
        if (list instanceof PipoElement l) {
            return l.children(PipoParam.class);
        }
        return e.children(PipoParam.class); // x -> ... lambda
    }

    private static void genericParams(PsiElement scope, Kind kind, Visitor v) {
        if (kind != Kind.TYPE) {
            return;
        }
        PsiElement g = ((PipoElement) scope).child(PipoElementTypes.GENERIC_PARAMS);
        if (g instanceof PipoElement ge) {
            for (PipoGenericParam p : ge.children(PipoGenericParam.class)) {
                v.visit(p.getName(), p);
            }
        }
    }

    private static void patternBindings(@Nullable PsiElement pattern, Visitor v) {
        if (pattern == null) {
            return;
        }
        for (PipoLocalBinding b : PsiTreeUtil.findChildrenOfType(pattern, PipoLocalBinding.class)) {
            v.visit(b.getName(), b);
        }
    }

    private static void blockDeclarations(PsiElement scope, @Nullable PsiElement child, int placeStart,
                                          boolean deferred, Kind kind, Visitor v) {
        for (PsiElement s : PipoPsiUtil.statements(scope)) {
            PsiElement d = PipoPsiUtil.unwrapExport(s);
            boolean before = child == null || s.getTextRange().getEndOffset() <= placeStart;
            if (d instanceof PipoVarDecl var) {
                if (kind == Kind.VALUE && (before || deferred) && s != child) {
                    v.visit(var.getName(), var);
                } else if (kind == Kind.VALUE && s == child && deferred) {
                    v.visit(var.getName(), var); // const f = function() { f() }
                }
            } else if (d instanceof PipoFunctionDecl f) {
                if (kind == Kind.VALUE) {
                    v.visit(f.getName(), f);
                }
            } else if (d instanceof PipoTypeDecl || d instanceof PipoInterfaceDecl) {
                v.visit(((PipoNamedElement) d).getName(), d);
            } else if (d instanceof PipoEnumDecl e) {
                v.visit(e.getName(), e);
                if (kind == Kind.VALUE) {
                    for (PipoEnumVariant variant : e.getVariants()) {
                        if (variant.isConstructor()) {
                            v.visit(variant.getName(), variant);
                        }
                    }
                }
            } else if (d instanceof PipoImportStmt imp) {
                if (imp.isDestructured()) {
                    for (PipoImportName n : imp.getImportNames()) {
                        v.visit(n.getName(), n);
                    }
                } else {
                    v.visit(imp.getName(), imp);
                }
            }
        }
    }

    static boolean isOverloadable(PsiElement d) {
        return d instanceof PipoFunctionDecl || d instanceof PipoImportName
                || (d instanceof PipoEnumVariant v && v.isConstructor());
    }

    private static final class ByName implements Visitor {
        final String name;
        final List<PsiElement> result = new ArrayList<>();
        final List<PsiElement> level = new ArrayList<>();

        ByName(String name) {
            this.name = name;
        }

        @Override
        public void visit(@Nullable String n, @NotNull PsiElement decl) {
            if (name.equals(n) && !level.contains(decl) && !result.contains(decl)) {
                level.add(decl);
            }
        }

        @Override
        public boolean endLevel() {
            if (level.isEmpty()) {
                return false;
            }
            boolean overloads = level.stream().allMatch(PipoResolver::isOverloadable);
            result.addAll(level);
            level.clear();
            return !overloads;
        }
    }

    private static final class Collect implements Visitor {
        final Map<String, List<PsiElement>> map = new LinkedHashMap<>();
        final Set<String> closed = new HashSet<>();
        final List<String> levelNames = new ArrayList<>();

        @Override
        public void visit(@Nullable String n, @NotNull PsiElement decl) {
            if (n == null || closed.contains(n)) {
                return;
            }
            List<PsiElement> l = map.computeIfAbsent(n, k -> new ArrayList<>());
            if (!l.contains(decl)) {
                l.add(decl);
                levelNames.add(n);
            }
        }

        @Override
        public boolean endLevel() {
            for (String n : levelNames) {
                if (!map.get(n).stream().allMatch(PipoResolver::isOverloadable)) {
                    closed.add(n);
                }
            }
            levelNames.clear();
            return false;
        }
    }

    /* ------------------------------------------------- members and qualifiers */

    private static List<PsiElement> targets(@Nullable PsiReference ref) {
        List<PsiElement> out = new ArrayList<>();
        if (ref instanceof PipoReferences.Base<?> b) {
            out.addAll(b.targets());
        } else if (ref != null) {
            PsiElement t = ref.resolve();
            if (t != null) {
                out.add(t);
            }
        }
        return out;
    }

    /** what an expression refers to, as far as can be told without running it */
    static List<PsiElement> targetsOf(@Nullable PsiElement expr, int depth) {
        if (expr == null || depth > 6) {
            return List.of();
        }
        IElementType t = typeOf(expr);
        if (expr instanceof PipoRefExpr || expr instanceof PipoFieldExpr) {
            return targets(expr.getReference());
        }
        if (t == PipoElementTypes.PAREN_EXPR) {
            return targetsOf(((PipoElement) expr).child(PipoElement.class), depth + 1);
        }
        if (t == PipoElementTypes.OBJECT_EXPR) {
            return List.of(expr);
        }
        if (t == PipoElementTypes.CALL_EXPR || t == PipoElementTypes.METHOD_CALL_EXPR) {
            PsiElement callee = t == PipoElementTypes.CALL_EXPR ? expr.getFirstChild() : PipoPsiUtil.methodCallee(expr);
            List<PsiElement> out = new ArrayList<>();
            for (PsiElement c : targetsOf(callee, depth + 1)) {
                if (c instanceof PipoTypeDecl) {
                    out.add(c); // Rect{ ... } / Rect(...) builds a Rect
                } else if (c instanceof PipoFunctionDecl f) {
                    out.addAll(typeTargets(f.getReturnType(), depth + 1));
                }
            }
            return out;
        }
        return List.of();
    }

    private static List<PsiElement> typeTargets(@Nullable PsiElement type, int depth) {
        if (type == null || depth > 6) {
            return List.of();
        }
        if (type instanceof PipoTypeRef ref) {
            return targets(ref.getReference());
        }
        if (is(type, PipoElementTypes.PAREN_TYPE)) {
            return typeTargets(((PipoElement) type).child(PipoElementTypes.TYPES), depth + 1);
        }
        if (is(type, PipoElementTypes.RECORD_TYPE)) {
            return List.of(type);
        }
        return List.of();
    }

    /** reports (name, member) for the members of a declaration or value */
    static void members(PsiElement target, BiConsumer<String, PsiElement> sink, int depth) {
        if (depth > 6) {
            return;
        }
        if (target instanceof PipoImportStmt imp) {
            PsiFile module = PipoModules.resolve(imp);
            if (module != null) {
                moduleExports(module, sink);
            }
        } else if (target instanceof PipoImportName in) {
            for (PsiElement t : targets(in.getReference())) {
                members(t, sink, depth + 1);
            }
        } else if (target instanceof PipoEnumDecl e) {
            for (PipoEnumVariant v : e.getVariants()) {
                sink.accept(v.getName(), v);
            }
        } else if (target instanceof PipoTypeDecl td) {
            PsiElement def = td.getDefinition();
            for (PsiElement t : typeTargets(def, depth + 1)) {
                members(t, sink, depth + 1);
            }
        } else if (is(target, PipoElementTypes.RECORD_TYPE)) {
            for (PipoRecordField f : ((PipoElement) target).children(PipoRecordField.class)) {
                sink.accept(f.getName(), f);
            }
        } else if (target instanceof PipoVarDecl var) {
            if (var.getTypeElement() != null) {
                for (PsiElement t : typeTargets(var.getTypeElement(), depth + 1)) {
                    members(t, sink, depth + 1);
                }
            } else {
                for (PsiElement t : targetsOf(var.getInitializer(), depth + 1)) {
                    members(t, sink, depth + 1);
                }
            }
        } else if (target instanceof PipoParam p) {
            for (PsiElement t : typeTargets(p.getTypeElement(), depth + 1)) {
                members(t, sink, depth + 1);
            }
        } else if (target instanceof PipoRecordField f) {
            for (PsiElement t : typeTargets(f.getTypeElement(), depth + 1)) {
                members(t, sink, depth + 1);
            }
        } else if (target instanceof PipoObjectEntry entry) {
            if (!entry.isShorthand()) {
                for (PsiElement t : targetsOf(entry.getValue(), depth + 1)) {
                    members(t, sink, depth + 1);
                }
            }
        } else if (is(target, PipoElementTypes.OBJECT_EXPR)) {
            for (PipoObjectEntry entry : ((PipoElement) target).children(PipoObjectEntry.class)) {
                sink.accept(entry.getName(), entry);
            }
        }
    }
}
