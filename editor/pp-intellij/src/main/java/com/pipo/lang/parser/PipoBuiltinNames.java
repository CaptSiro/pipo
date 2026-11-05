package com.pipo.lang.parser;

import java.util.Set;

/** Names the language itself knows about. */
public final class PipoBuiltinNames {
    /** primitive type names (prim_from_name in parser.c) */
    public static final Set<String> PRIMITIVES = Set.of("int", "float", "string", "bool", "void", "range", "object",
            "any", "type");

    /** words that are keywords only in some positions */
    public static final Set<String> CONTEXTUAL_KEYWORDS = Set.of("type", "interface", "impl", "precedence", "over",
            "under", "as", "native");

    private PipoBuiltinNames() {
    }
}
