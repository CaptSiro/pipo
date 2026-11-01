#ifndef PP_COMMON_H
#define PP_COMMON_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PP_VERSION "0.1.0"

#if defined(__GNUC__) || defined(__clang__)
#define PP_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define PP_NORETURN __declspec(noreturn)
#else
#define PP_NORETURN
#endif

void* pp_malloc(size_t size);
void* pp_calloc(size_t count, size_t size);
void* pp_realloc(void* ptr, size_t size);
char* pp_strndup(const char* s, size_t len);
char* pp_strdup(const char* s);
char* pp_read_file(const char* path, size_t* out_len);

/* grows a dynamic array so that one more element fits */
#define GROW(arr, count, cap)                                                  \
    do {                                                                       \
        if ((count) >= (cap)) {                                                \
            (cap) = (cap) < 8 ? 8 : (cap) * 2;                                 \
            (arr) = pp_realloc((arr), sizeof(*(arr)) * (size_t)(cap));         \
        }                                                                      \
    } while (0)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf* sb);
void sb_append(StrBuf* sb, const char* s, size_t len);
void sb_puts(StrBuf* sb, const char* s);
void sb_putc(StrBuf* sb, char c);
void sb_printf(StrBuf* sb, const char* fmt, ...);
void sb_free(StrBuf* sb);

#endif
