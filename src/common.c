#include "common.h"

static void out_of_memory(void) {
    fprintf(stderr, "fatal: out of memory\n");
    exit(70);
}

void* pp_malloc(size_t size) {
    void* p = malloc(size ? size : 1);
    if (!p) {
        out_of_memory();
    }

    return p;
}

void* pp_calloc(size_t count, size_t size) {
    void* p = calloc(count ? count : 1, size ? size : 1);
    if (!p) {
        out_of_memory();
    }

    return p;
}

void* pp_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size ? size : 1);
    if (!p) {
        out_of_memory();
    }

    return p;
}

char* pp_strndup(const char* s, size_t len) {
    char* r = pp_malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';

    return r;
}

char* pp_strdup(const char* s) {
    return pp_strndup(s, strlen(s));
}

char* pp_read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }

    fseek(f, 0, SEEK_SET);
    char* buf = pp_malloc((size_t)n + 1);
    size_t read = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[read] = '\0';
    if (out_len) {
        *out_len = read;
    }

    return buf;
}

void sb_init(StrBuf* sb) {
    sb->cap = 32;
    sb->len = 0;
    sb->data = pp_malloc(sb->cap);
    sb->data[0] = '\0';
}

static void sb_reserve(StrBuf* sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }
    while (sb->len + extra + 1 > sb->cap) {
        sb->cap *= 2;
    }

    sb->data = pp_realloc(sb->data, sb->cap);
}

void sb_append(StrBuf* sb, const char* s, size_t len) {
    sb_reserve(sb, len);
    memcpy(sb->data + sb->len, s, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void sb_puts(StrBuf* sb, const char* s) {
    sb_append(sb, s, strlen(s));
}

void sb_putc(StrBuf* sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_printf(StrBuf* sb, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    const int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n > 0) {
        sb_reserve(sb, (size_t)n);
        vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
        sb->len += (size_t)n;
    }

    va_end(ap);
}

void sb_free(StrBuf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}
