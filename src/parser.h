#ifndef PP_PARSER_H
#define PP_PARSER_H

#include "vm.h"

/* Parses a whole source file into an N_BLOCK. Prints diagnostics and returns NULL on error. */
Node* parse_program(VM* vm, const char* src, const char* file);

/* Parses a signature such as "<T>(T[] arr, T item) -> int" for an internal function. */
Signature* parse_native_signature(VM* vm, const char* name, const char* sig);

#endif
