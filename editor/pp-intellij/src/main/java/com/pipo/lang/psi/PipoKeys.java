package com.pipo.lang.psi;

import com.intellij.openapi.util.Key;

public final class PipoKeys {
    /** set on the bundled builtins / native module stub files; the value is the module name ("" for builtins) */
    public static final Key<String> STUB = Key.create("pipo.stub");

    private PipoKeys() {
    }
}
