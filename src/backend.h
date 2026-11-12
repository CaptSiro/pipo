#ifndef PP_BACKEND_H
#define PP_BACKEND_H

#include "check.h"



// The compiling targets. Both take a program the checker accepted and work from its
// instances: every function body is typed per instantiation (see check.h).

// bytecode.c / bcvm.c: compiles to bytecode and runs it. dump: print the bytecode first.
int bytecode_run(VM* vm, Checker* c, bool dump);

// wasm.c: writes <base>.wasm, <base>.mjs (a loader for node and browsers) and <base>.html
int wasm_compile(VM* vm, Checker* c, const char* base);

// how a scope call ended: its block finished normally or left with return / break / continue
typedef enum { SCOPE_NORMAL, SCOPE_RETURN, SCOPE_BREAK, SCOPE_CONTINUE } ScopeExit;

// backend.c: a set of names
typedef struct {
    ObjString** items;
    int count;
    int cap;
} NameList;

bool names_has(const NameList* l, ObjString* name);
void names_add(NameList* l, ObjString* name);

// adds the names used inside the nested functions of n (lambdas, local functions, the blocks of
// scope calls, defer): the variables with these names are captured, so they live in cells
void names_used_nested(NameList* out, Node* n, bool nested);



#endif // PP_BACKEND_H
