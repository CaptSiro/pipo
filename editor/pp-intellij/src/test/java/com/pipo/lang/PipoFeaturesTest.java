package com.pipo.lang;

import com.intellij.codeInsight.lookup.Lookup;
import com.intellij.ide.structureView.StructureViewModel;
import com.intellij.ide.structureView.TreeBasedStructureViewBuilder;
import com.intellij.ide.util.treeView.smartTree.TreeElement;
import com.intellij.openapi.command.WriteCommandAction;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.psi.PsiPolyVariantReference;
import com.intellij.psi.PsiReference;
import com.intellij.psi.ResolveResult;
import com.intellij.psi.codeStyle.CodeStyleManager;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.intellij.usageView.UsageInfo;
import com.pipo.lang.editor.PipoDocumentationProvider;
import com.pipo.lang.psi.*;
import com.pipo.lang.structure.PipoStructureView;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/** Navigation, completion, rename, formatting, documentation and structure on small examples. */
public class PipoFeaturesTest extends BasePlatformTestCase {
    private List<PsiElement> resolveAtCaret() {
        PsiReference ref = myFixture.getFile().findReferenceAt(myFixture.getCaretOffset());
        assertNotNull("no reference at caret", ref);
        List<PsiElement> out = new ArrayList<>();
        if (ref instanceof PsiPolyVariantReference poly) {
            for (ResolveResult r : poly.multiResolve(false)) {
                out.add(r.getElement());
            }
        } else if (ref.resolve() != null) {
            out.add(ref.resolve());
        }
        return out;
    }

    private PsiElement single() {
        List<PsiElement> targets = resolveAtCaret();
        assertEquals("targets: " + targets, 1, targets.size());
        return targets.get(0);
    }

    /* ------------------------------------------------------------- resolve */

    public void testLocalsAndParameters() {
        myFixture.configureByText("a.pp", """
                function f(int a) -> int {
                    const b = a + 1
                    return <caret>b
                }
                """);
        assertEquals("b", ((PipoVarDecl) single()).getName());

        myFixture.configureByText("b.pp", """
                function f(int a) -> int {
                    return <caret>a * 2
                }
                """);
        assertInstanceOf(single(), PipoParam.class);
    }

    public void testVariableIsNotVisibleBeforeItsDeclarationButFunctionsAre() {
        myFixture.configureByText("a.pp", """
                const x = 1
                {
                    const y = <caret>x
                    const x = 2
                }
                """);
        assertTrue("resolves to the outer x", ((PipoVarDecl) single()).isTopLevel());

        myFixture.configureByText("b.pp", """
                function a() { return <caret>b() }
                function b() { return 1 }
                """);
        assertEquals("b", ((PipoFunctionDecl) single()).getName());
    }

    public void testOverloadsAndBuiltins() {
        myFixture.configureByText("a.pp", """
                function area(int s) -> int { return s * s }
                function area(int w, int h) -> int { return w * h }
                const x = <caret>area(2)
                """);
        assertEquals(2, resolveAtCaret().size());

        myFixture.configureByText("b.pp", "const s = <caret>str(1)\n");
        PsiElement builtin = single();
        assertTrue(PipoPsiUtil.isInBuiltinStub(builtin));

        // a user overload of a builtin joins the overload set
        myFixture.configureByText("c.pp", """
                type Vec = { int x, int y }
                function str(Vec v) -> string { return "vec" }
                const s = <caret>str(1)
                """);
        List<PsiElement> both = resolveAtCaret();
        assertEquals(2, both.size());
    }

    public void testOperatorsResolveToTheirFunctions() {
        myFixture.configureByText("a.pp", """
                type Vec = { int x, int y }
                function +(Vec a, Vec b) -> Vec { return a }
                const v = Vec{ x: 1, y: 2 } <caret>+ Vec{ x: 3, y: 4 }
                """);
        List<PsiElement> targets = resolveAtCaret();
        assertTrue(targets.stream().anyMatch(t -> t instanceof PipoFunctionDecl f && !PipoPsiUtil.isInBuiltinStub(f)));
    }

    public void testModulesAndExports() {
        myFixture.addFileToProject("geometry.pp", """
                // a two-dimensional vector
                export type Vec = { float x, float y }
                export function vec(float x, float y) -> Vec { return { x, y } }
                function hidden() {}
                """);
        myFixture.configureByText("main.pp", """
                import "./geometry"
                const v = geometry:<caret>vec(1.0, 2.0)
                """);
        PsiElement target = single();
        assertEquals("geometry.pp", target.getContainingFile().getName());

        myFixture.configureByText("main2.pp", """
                import "./geometry" -> { vec }
                const v = <caret>vec(1.0, 2.0)
                """);
        assertInstanceOf(single(), PipoImportName.class);

        myFixture.configureByText("main3.pp", """
                import "./geometry"
                const geometry:<caret>Vec v = geometry:vec(1.0, 2.0)
                """);
        assertInstanceOf(single(), PipoTypeDecl.class);

        myFixture.configureByText("main4.pp", "import \"./geo<caret>metry\"\n");
        assertEquals("geometry.pp", ((PsiFile) single()).getName());

        myFixture.configureByText("main5.pp", "import io\nio:<caret>print(1)\n");
        assertTrue(PipoPsiUtil.isInBuiltinStub(single()));
    }

    public void testEnumsRecordsAndPatterns() {
        myFixture.configureByText("a.pp", """
                enum Color { RED, GREEN }
                const c = Color:<caret>GREEN
                """);
        assertEquals("GREEN", ((PipoEnumVariant) single()).getName());

        myFixture.configureByText("b.pp", """
                type Person = { string name, int age }
                const Person p = { name: "a", age: 1 }
                const n = p:<caret>name
                """);
        assertInstanceOf(single(), PipoRecordField.class);

        myFixture.configureByText("c.pp", """
                const o = { size: 3 }
                const n = o:<caret>size
                """);
        assertInstanceOf(single(), PipoObjectEntry.class);

        myFixture.configureByText("d.pp", """
                const r = match parse_int("1") {
                    some(x) -> <caret>x + 1,
                    none() -> 0
                }
                """);
        assertInstanceOf(single(), PipoLocalBinding.class);

        myFixture.configureByText("e.pp", "const r = <caret>some(1)\n");
        assertInstanceOf(single(), PipoEnumVariant.class);
    }

    public void testGenericsAndLoops() {
        myFixture.configureByText("a.pp", """
                <T>function first(T[] xs) -> <caret>T? { return none() }
                """);
        assertInstanceOf(single(), PipoGenericParam.class);

        myFixture.configureByText("b.pp", """
                for i, x in [1, 2] {
                    const y = <caret>x
                }
                """);
        assertInstanceOf(single(), PipoLocalBinding.class);
    }

    /* ---------------------------------------------------------- completion */

    private List<String> complete(String name, String text) {
        myFixture.configureByText(name, text);
        myFixture.completeBasic();
        List<String> l = myFixture.getLookupElementStrings();
        return l != null ? l : List.of();
    }

    public void testCompletion() {
        List<String> io = complete("a.pp", "import io\nio:pr<caret>\n");
        assertTrue(io.toString(), io.containsAll(List.of("print", "print_nl")));

        List<String> locals = complete("b.pp", """
                const apple = 1
                function apply_all() {}
                const x = ap<caret>
                """);
        assertTrue(locals.toString(), locals.containsAll(List.of("apple", "apply_all")));

        List<String> variants = complete("c.pp", "enum Color { RED, ROSE, BLUE }\nconst c = Color:R<caret>\n");
        assertTrue(variants.toString(), variants.containsAll(List.of("RED", "ROSE")));
        assertFalse(variants.contains("BLUE"));

        List<String> types = complete("d.pp", "type Person = { string name }\nconst Pe<caret> p = 1\n");
        assertTrue(types.toString(), types.isEmpty() || types.contains("Person"));
    }

    public void testCompletionInsertsParentheses() {
        myFixture.configureByText("a.pp", "function compute(int x) -> int { return x }\nconst y = compu<caret>\n");
        myFixture.completeBasic();
        if (myFixture.getLookup() != null) {
            myFixture.finishLookup(Lookup.NORMAL_SELECT_CHAR);
        }
        assertTrue(myFixture.getFile().getText(), myFixture.getFile().getText().contains("const y = compute()"));
    }

    /* ---------------------------------------------- rename and find usages */

    public void testRenameLocal() {
        myFixture.configureByText("a.pp", """
                function f(int count) -> int {
                    const <caret>total = count + 1
                    return total * total
                }
                """);
        myFixture.renameElementAtCaret("sum");
        myFixture.checkResult("""
                function f(int count) -> int {
                    const sum = count + 1
                    return sum * sum
                }
                """);
    }

    public void testFindUsages() {
        myFixture.configureByText("a.pp", """
                function <caret>twice(int x) -> int { return x * 2 }
                const a = twice(1)
                const b = 3.twice()
                const c = [1].map(twice)
                """);
        Collection<UsageInfo> usages = myFixture.findUsages(myFixture.getElementAtCaret());
        assertEquals(3, usages.size());
    }

    /* ----------------------------------------------------------- formatter */

    public void testFormatterIndentsWithoutChangingMeaning() {
        myFixture.configureByText("a.pp", """
                function f(int a,int b) -> int {
                if a > b {
                return a
                }
                        const xs = [
                1,
                  2
                ]
                return xs[0]
                }
                """);
        WriteCommandAction.runWriteCommandAction(getProject(),
                (Runnable) () -> CodeStyleManager.getInstance(getProject()).reformat(myFixture.getFile()));
        myFixture.checkResult("""
                function f(int a, int b) -> int {
                    if a > b {
                        return a
                    }
                    const xs = [
                        1,
                        2
                    ]
                    return xs[0]
                }
                """);
    }

    /* ------------------------------------------------ documentation, structure */

    public void testDocumentationShowsCommentAndSignature() {
        myFixture.configureByText("a.pp", """
                // doubles a number
                // (works on ints)
                function twice(int x) -> int { return x * 2 }
                const y = twi<caret>ce(2)
                """);
        PsiElement target = single();
        String doc = new PipoDocumentationProvider().generateDoc(target,
                myFixture.getFile().findElementAt(myFixture.getCaretOffset()));
        assertNotNull(doc);
        assertTrue(doc, doc.contains("function twice(int x) -&gt; int"));
        assertTrue(doc, doc.contains("doubles a number (works on ints)"));
    }

    public void testStructureView() {
        myFixture.configureByText("a.pp", """
                import io
                type Person = { string name }
                enum Color { RED, GREEN }
                interface Str { function str(Str) -> string }
                const limit = 3
                function greet(Person p) -> string { return p:name }
                """);
        TreeBasedStructureViewBuilder builder = (TreeBasedStructureViewBuilder)
                new PipoStructureView().getStructureViewBuilder(myFixture.getFile());
        StructureViewModel model = builder.createStructureViewModel(myFixture.getEditor());
        try {
            List<String> names = new ArrayList<>();
            for (TreeElement e : model.getRoot().getChildren()) {
                names.add(e.getPresentation().getPresentableText());
            }
            assertEquals(List.of("Person", "Color", "Str", "limit", "greet(Person p) -> string"), names);
        } finally {
            model.dispose();
        }
    }
}
