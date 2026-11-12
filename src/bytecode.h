#ifndef PP_BYTECODE_H
#define PP_BYTECODE_H

#include "backend.h"
#include "check_int.h"



// The bytecode target (--target=bytecode). bytecode.c compiles the checker's function
// instances; bcvm.c runs them. Values, objects, the garbage collector and the C functions of
// the modules are the interpreter's own; only the execution of .pp code is new.
//
// Code is an array of int32 words: an opcode followed by its operands. The VM is a stack
// machine whose stack is vm->stack: the globals of every module sit at its bottom, then the
// frames, each one its locals (parameters first) followed by its temporaries.

typedef enum {
    OP_INT,          // i: push the int i
    OP_CONST,        // k: push constant k
    OP_TRUE,
    OP_FALSE,
    OP_VOID,
    OP_POP,
    OP_DUP,
    OP_LOAD,         // slot: push local slot
    OP_STORE,        // slot: pop into local slot
    OP_GLOAD,        // g: push global g
    OP_GSTORE,       // g: pop into global g
    OP_CAP,          // i: push capture i of the running closure (a cell)
    OP_BOX,          // slot: put the value of local slot into a new cell, stored in the slot
    OP_CELL_GET,     // cell -> its value
    OP_CELL_SET,     // cell value -> (stores)

    OP_ADD_I,
    OP_SUB_I,
    OP_MUL_I,
    OP_DIV_I,
    OP_MOD_I,
    OP_NEG_I,
    OP_POW_I,
    OP_ADD_F,
    OP_SUB_F,
    OP_MUL_F,
    OP_DIV_F,
    OP_MOD_F,
    OP_NEG_F,
    OP_POW_F,
    OP_I2F,          // the int on top becomes a float
    OP_I2F_UNDER,    // the int below the top becomes a float
    OP_EQ_I,
    OP_NE_I,
    OP_LT_I,
    OP_GT_I,
    OP_LE_I,
    OP_GE_I,
    OP_EQ_F,
    OP_NE_F,
    OP_LT_F,
    OP_GT_F,
    OP_LE_F,
    OP_GE_F,
    OP_EQ_S,
    OP_NE_S,
    OP_LT_S,
    OP_GT_S,
    OP_LE_S,
    OP_GE_S,
    OP_EQ_B,
    OP_NE_B,
    OP_NOT,
    OP_CONCAT,
    OP_LEN_S,
    OP_LEN_A,
    OP_VALUES_EQ,    // a b -> a == b, structurally

    OP_JUMP,         // target
    OP_JUMP_FALSE,   // target: pops the condition
    OP_JUMP_TRUE,    // target: pops the condition
    OP_AND,          // target: false stays on the stack and jumps; true is popped (a && b)
    OP_OR,           // target: true stays on the stack and jumps; false is popped (a || b)

    OP_CALL,         // func, argc, line: a compiled function
    OP_CALL_NATIVE,  // native, argc, line: a C function
    OP_CALL_VALUE,   // argc, line: the callee is below the arguments
    OP_RETURN,       // returns the value on top

    OP_ARRAY,        // n: pops n values into a new array
    OP_TUPLE,        // n
    OP_OBJECT,       // push a new object
    OP_FIELD_INIT,   // k: obj value -> obj, with field k
    OP_GET_FIELD,    // k, line
    OP_SET_FIELD,    // k, line: obj value ->
    OP_INDEX,        // line: obj key -> value
    OP_INDEX_SET,    // line: obj key value ->
    OP_ENUM,         // enum, variant, n: pops n payload values
    OP_IS_VARIANT,   // enum, variant: value -> bool
    OP_PAYLOAD,      // k: enum value -> its k-th payload value
    OP_IS_TUPLE,     // n: value -> bool, a tuple of n elements
    OP_TUPLE_GET,    // k
    OP_AS,           // type: value -> some(value) when it has the static type, else none()
    OP_TAG,          // typedef: tags the object on top (or every object in the array on top) with a record type

    OP_ITER_INIT,    // slot: pops the iterable into slot; slot+1, slot+2: position and index
    OP_ITER_NEXT,    // slot, var1, var2, target: the next item into var1 / var2 (-1: none), or jump to target
    OP_CLOSURE,      // func, ncaps: pops the captured cells
    OP_RUN_MODULE,   // module: runs a module's top level code once
    OP_LINE,         // line: the current line (messages), and a point where the GC may run
    OP_NO_MATCH,     // line: the value on top matched no arm
    OP_FAIL,         // k: a runtime error with message constant k
    OP_DISPATCH,     // table, argc, line: calls the first candidate of the table the arguments fit
    OP_SCOPE_CALL,   // func, argc, line: callee/void, value slot, defer slot, block, args -> value, exit
    OP_SCOPE_EXIT,   // exit: leaves the running block (SCOPE_RETURN pops the value to return)
    OP_DEFER,        // closure -> (the cleanup of the running scope call)
    OP_COUNT
} OpCode;

// what a compiled function is: an ordinary function, or the body of a scope call (its block)
typedef enum { BC_FUNCTION, BC_BLOCK, BC_CALLBACK, BC_DEFER } BcKind;

typedef struct {
    const char* name;
    int noperands;
} OpInfo;

extern const OpInfo BC_OPS[OP_COUNT];

typedef struct BcFunc {
    ObjString* name;
    const char* file;
    int line;
    int nparams;
    int nlocals;        // slots, the parameters included
    int ncaps;
    int32_t* code;
    int count;
    int cap;
    CInstance* inst;
    int index;
    bool compiled;
    bool is_init;       // a module's top level: not shown in tracebacks
    BcKind kind;
    ObjString* scope;   // a block or defer: the name of its scope
    bool* cap_local;    // where each capture comes from in the function that creates the closure:
    int* cap_index;     // its local slot (cap_local) or its own capture
} BcFunc;

typedef struct {
    NativeFn fn;
    const char* name;
} BcNative;

// one candidate of a dynamic dispatch: a compiled function or a native, with its parameter types
typedef struct {
    bool native;
    int index;
    SType** params;
    int nparams;
    bool variadic;
} BcCand;

typedef struct {
    ObjString* name;
    BcCand* cands;
    int ncands;
} BcDispatch;

typedef struct {
    CModule* module;
    BcFunc* init;       // the top level code
    int global_base;    // the module's globals start here
    ObjString** gnames; // name -> global index (global_base + i), also names imported from other modules
    int* gindex;
    int ngnames;
    int capgnames;
    bool done;
    bool running;
    Value value;        // the module as a value, made when first used
} BcModule;

typedef struct {
    VM* vm;
    Checker* checker;
    BcFunc** funcs;
    int nfuncs;
    int capfuncs;
    Value* consts;
    int nconsts;
    int capconsts;
    BcNative* natives;
    int nnatives;
    int capnatives;
    ObjEnum** enums;
    TDecl** enum_decls;
    int nenums;
    int capenums;
    ObjTypeDef** typedefs;
    TDecl** typedef_decls;
    int ntypedefs;
    int captypedefs;
    SType** types;      // OP_AS
    int ntypes;
    int captypes;
    BcDispatch* dispatches;
    int ndispatches;
    int capdispatches;
    BcModule* modules;
    int nmodules;
    int nglobals;
    ObjArray* roots;    // keeps constants and the objects natives come from alive
    jmp_buf* err;
    const char* error;  // set when the program cannot be compiled
    int error_line;
    const char* error_file;
} BcProgram;

// bytecode.c
BcProgram* bc_compile(VM* vm, Checker* c);
void bc_dump(BcProgram* prog, FILE* out);

// bcvm.c
int bc_run(BcProgram* prog);
Value bc_call_closure(VM* vm, Obj* closure, int argc, Value* args, int line);



#endif // PP_BYTECODE_H
