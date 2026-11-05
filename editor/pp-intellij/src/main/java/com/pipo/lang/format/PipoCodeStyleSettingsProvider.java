package com.pipo.lang.format;

import com.intellij.application.options.CodeStyleAbstractConfigurable;
import com.intellij.application.options.CodeStyleAbstractPanel;
import com.intellij.application.options.IndentOptionsEditor;
import com.intellij.application.options.SmartIndentOptionsEditor;
import com.intellij.application.options.TabbedLanguageCodeStylePanel;
import com.intellij.lang.Language;
import com.intellij.psi.codeStyle.CodeStyleConfigurable;
import com.intellij.psi.codeStyle.CodeStyleSettings;
import com.intellij.psi.codeStyle.CodeStyleSettingsCustomizable;
import com.intellij.psi.codeStyle.CommonCodeStyleSettings;
import com.intellij.psi.codeStyle.LanguageCodeStyleSettingsProvider;
import com.pipo.lang.PipoLanguage;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/** Settings | Editor | Code Style | PiPo: tabs and indents. */
public final class PipoCodeStyleSettingsProvider extends LanguageCodeStyleSettingsProvider {
    @Override
    public @NotNull Language getLanguage() {
        return PipoLanguage.INSTANCE;
    }

    @Override
    protected void customizeDefaults(@NotNull CommonCodeStyleSettings common,
                                     @NotNull CommonCodeStyleSettings.IndentOptions indent) {
        indent.INDENT_SIZE = 4;
        indent.CONTINUATION_INDENT_SIZE = 4;
        indent.TAB_SIZE = 4;
        indent.USE_TAB_CHARACTER = false;
    }

    @Override
    public @Nullable IndentOptionsEditor getIndentOptionsEditor() {
        return new SmartIndentOptionsEditor();
    }

    @Override
    public void customizeSettings(@NotNull CodeStyleSettingsCustomizable consumer, @NotNull SettingsType settingsType) {
        if (settingsType == SettingsType.INDENT_SETTINGS) {
            consumer.showStandardOptions("INDENT_SIZE", "CONTINUATION_INDENT_SIZE", "TAB_SIZE", "USE_TAB_CHARACTER");
        }
    }

    @Override
    public @Nullable String getCodeSample(@NotNull SettingsType settingsType) {
        return """
                import io

                type Person = { string name, int age }

                function greet(Person p, string? title) -> string {
                    const t = title.unwrap_or("")
                    if p:age > 18 {
                        return t + " " + p:name
                    }
                    return match p:age {
                        0 -> "newborn",
                        _ -> p:name
                    }
                }

                const people = [
                    { name: "John", age: 22 },
                    { name: "Ann", age: 3 }
                ]
                for p in people {
                    io:print(p.greet(none()))
                }
                """;
    }

    @Override
    public @NotNull CodeStyleConfigurable createConfigurable(@NotNull CodeStyleSettings settings,
                                                             @NotNull CodeStyleSettings modelSettings) {
        return new CodeStyleAbstractConfigurable(settings, modelSettings, "PiPo") {
            @Override
            protected @NotNull CodeStyleAbstractPanel createPanel(@NotNull CodeStyleSettings s) {
                return new TabbedLanguageCodeStylePanel(PipoLanguage.INSTANCE, getCurrentSettings(), s) {
                    @Override
                    protected void initTabs(CodeStyleSettings tabSettings) {
                        addIndentOptionsTab(tabSettings);
                    }
                };
            }
        };
    }

    @Override
    public @Nullable String getConfigurableDisplayName() {
        return "PiPo";
    }
}
