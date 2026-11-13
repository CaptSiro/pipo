#ifndef PP_PARSER_H
#define PP_PARSER_H

#include "vm.h"

// Parses a whole source file into an N_BLOCK. Prints diagnostics and returns NULL on error.
Node* parse_program(VM* vm, const char* src, const char* file);

// Parses a signature such as "<T>(T[] arr, T item) -> int" for an internal function.
Signature* parse_native_signature(VM* vm, const char* name, const char* sig);

// Tag calls (md/TAG_CALLS.md). The parser writes <f/> as f(), <f a=1/> as f({ a: 1 }), <f>...</f> as
// f([...]) and <f a=1>...</f> as f({ a: 1 }, [...]). tag_lower finishes the call once the candidates
// are known: <f/> becomes f({}) when no f takes zero arguments, and <f>...</f> becomes f({}, [...])
// unless an f takes exactly one array. sigs are the candidates' signatures (NULL entries are skipped);
// ctor: the callee is a record type (callable with one record); known: false when the candidates
// cannot be listed (a function value), which keeps the call as written. Returns false and writes the
// error into err when the tag's form cannot fit any candidate (a body for a function without
// children, or /> for one that needs them).
enum { TAG_ATTRS = 1, TAG_BODY = 2, TAG_LOWERED = 4 };
bool tag_lower(VM* vm, Node* call, Signature** sigs, int nsigs, bool ctor, bool known, StrBuf* err);



#endif // PP_PARSER_H
