// The C library of the guest image (arm64 build): what the game and the platform layer use, freestanding.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "math.h"
#include "hostapi.h"

// ------------------------------------------------------------------------------------------------ memory
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if ((((uintptr_t) dp | (uintptr_t) sp | n) & 7) == 0) {
        for (size_t i = 0; i < n; i += 8) *(uint64_t *) (dp + i) = *(const uint64_t *) (sp + i);
        return d;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n) return memcpy(d, s, n);
    while (n--) dp[n] = sp[n];
    return d;
}

void *memset(void *d, int c, size_t n) {
    uint8_t *dp = d;
    if ((((uintptr_t) dp | n) & 7) == 0) {
        uint64_t v = (uint8_t) c * 0x0101010101010101ull;
        for (size_t i = 0; i < n; i += 8) *(uint64_t *) (dp + i) = v;
        return d;
    }
    while (n--) *dp++ = (uint8_t) c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] - y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return (unsigned char) *a - (unsigned char) *b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char) a[i] - (unsigned char) b[i];
    }
    return 0;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) {
    }
    return r;
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}
char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char) c) return (char *) s;
        if (!*s) return NULL;
    }
}
char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char) c) r = s;
        if (!*s) return (char *) r;
    }
}

// ------------------------------------------------------------------------------------------------ numbers
unsigned long strtoul(const char *s, char **end, int base) {
    while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    unsigned long v = 0;
    for (;; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'z' ? *s - 'a' + 10
                : *s >= 'A' && *s <= 'Z' ? *s - 'A' + 10 : 99;
        if (d >= base) break;
        v = v * (unsigned long) base + (unsigned long) d;
    }
    if (end) *end = (char *) s;
    return v;
}
long strtol(const char *s, char **end, int base) {
    while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
    bool neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    long v = (long) strtoul(s, end, base);
    return neg ? -v : v;
}
int abs(int x) { return x < 0 ? -x : x; }

static uint32_t s_rand = 1;
int rand(void) {
    s_rand = s_rand * 1103515245u + 12345u;
    return (int) ((s_rand >> 16) & 0x7fff);
}
void srand(unsigned seed) { s_rand = seed; }

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    uint8_t *b = base, tmp[256];
    for (size_t i = 1; i < n; i++) { // insertion sort: small arrays only
        for (size_t j = i; j > 0 && cmp(b + (j - 1) * size, b + j * size) > 0; j--) {
            for (size_t k = 0; k < size; k += sizeof(tmp)) {
                size_t c = size - k < sizeof(tmp) ? size - k : sizeof(tmp);
                memcpy(tmp, b + (j - 1) * size + k, c);
                memcpy(b + (j - 1) * size + k, b + j * size + k, c);
                memcpy(b + j * size + k, tmp, c);
            }
        }
    }
}

void abort(void) { hapi_abort(); }

// ------------------------------------------------------------------------------------------------ heap
// A first-fit allocator over a fixed arena (the platform layer allocates little; the game has its own heaps).
#define HEAP_SIZE (16u << 20)
static uint8_t s_heap[HEAP_SIZE] __attribute__((aligned(16)));
typedef struct Block {
    uint32_t size; // payload size, bit 0 = used
    uint32_t pad[3];
} Block;
static bool s_heap_ready;
static void *s_heap_lock;

static void heap_init(void) {
    Block *b = (Block *) s_heap;
    b->size = HEAP_SIZE - sizeof(Block);
    s_heap_ready = true;
}

void *malloc(size_t n) {
    if (!s_heap_lock) s_heap_lock = hapi_mutex_new();
    hapi_mutex_lock(s_heap_lock);
    if (!s_heap_ready) heap_init();
    n = (n + 15) & ~(size_t) 15;
    for (uint8_t *p = s_heap; p < s_heap + HEAP_SIZE;) {
        Block *b = (Block *) p;
        uint32_t size = b->size & ~1u;
        if (!(b->size & 1)) {
            // Merge following free blocks.
            for (;;) {
                Block *next = (Block *) (p + sizeof(Block) + size);
                if ((uint8_t *) next >= s_heap + HEAP_SIZE || (next->size & 1)) break;
                size += sizeof(Block) + next->size;
            }
            b->size = size;
            if (size >= n) {
                if (size >= n + sizeof(Block) + 16) {
                    Block *rest = (Block *) (p + sizeof(Block) + n);
                    rest->size = size - (uint32_t) n - sizeof(Block);
                    size = (uint32_t) n;
                }
                b->size = size | 1;
                hapi_mutex_unlock(s_heap_lock);
                return p + sizeof(Block);
            }
        }
        p += sizeof(Block) + size;
    }
    hapi_mutex_unlock(s_heap_lock);
    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;
    hapi_mutex_lock(s_heap_lock);
    Block *b = (Block *) ((uint8_t *) ptr - sizeof(Block));
    b->size &= ~1u;
    hapi_mutex_unlock(s_heap_lock);
}

void *calloc(size_t a, size_t b) {
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}

void *realloc(void *ptr, size_t n) {
    if (!ptr) return malloc(n);
    Block *b = (Block *) ((uint8_t *) ptr - sizeof(Block));
    uint32_t old = b->size & ~1u;
    if (old >= n) return ptr;
    void *p = malloc(n);
    if (p) {
        memcpy(p, ptr, old);
        free(ptr);
    }
    return p;
}

// ------------------------------------------------------------------------------------------------ printf
static void put(char *buf, size_t n, size_t *pos, char c) {
    if (*pos + 1 < n) buf[*pos] = c;
    (*pos)++;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(buf, n, &pos, *fmt);
            continue;
        }
        fmt++;
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else if (*fmt == '+') plus = true;
            else if (*fmt == ' ') space = true;
            else if (*fmt == '#') alt = true;
            else break;
        }
        int width = 0, prec = -1;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) left = true, width = -width;
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
            }
        }
        int len = 0; // 0 int, 1 long, 2 long long
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' || *fmt == 't' || *fmt == 'j') {
            if (*fmt == 'l') len++;
            if (*fmt == 'j') len = 2;
            fmt++;
        }
        char tmp[72];
        const char *s = tmp;
        int slen = 0;
        char sign = 0;
        const char *prefix = "";
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = len >= 2 ? va_arg(ap, long long) : len == 1 ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? 0ull - (unsigned long long) v : (unsigned long long) v;
            sign = v < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            char *e = tmp + sizeof(tmp);
            do *--e = (char) ('0' + u % 10); while (u /= 10);
            s = e;
            slen = (int) (tmp + sizeof(tmp) - e);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'p': {
            unsigned long long u;
            if (*fmt == 'p') u = (uintptr_t) va_arg(ap, void *), alt = true;
            else u = len >= 2 ? va_arg(ap, unsigned long long) : len == 1 ? va_arg(ap, unsigned long)
                                                                          : va_arg(ap, unsigned int);
            unsigned base = *fmt == 'o' ? 8 : *fmt == 'u' ? 10 : 16;
            const char *digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *e = tmp + sizeof(tmp);
            do *--e = digits[u % base]; while (u /= base);
            s = e;
            slen = (int) (tmp + sizeof(tmp) - e);
            if (alt && base == 16) prefix = *fmt == 'X' ? "0X" : "0x";
            break;
        }
        case 'c':
            tmp[0] = (char) va_arg(ap, int);
            slen = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            slen = 0;
            while (s[slen] && (prec < 0 || slen < prec)) slen++;
            prec = -1;
            break;
        case '%':
            tmp[0] = '%';
            slen = 1;
            break;
        default:
            continue;
        }
        int zeros = prec > slen ? prec - slen : 0;
        int total = slen + zeros + (sign ? 1 : 0) + (int) strlen(prefix);
        if (zero && !left && prec < 0 && width > total) zeros += width - total, total = width;
        if (!left) for (int i = total; i < width; i++) put(buf, n, &pos, ' ');
        if (sign) put(buf, n, &pos, sign);
        for (const char *p = prefix; *p; p++) put(buf, n, &pos, *p);
        for (int i = 0; i < zeros; i++) put(buf, n, &pos, '0');
        for (int i = 0; i < slen; i++) put(buf, n, &pos, s[i]);
        if (left) for (int i = total; i < width; i++) put(buf, n, &pos, ' ');
    }
    if (n) buf[pos < n ? pos : n - 1] = 0;
    return (int) pos;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, 0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

// ------------------------------------------------------------------------------------------------ math
// log2 / exp2 to about 1e-7 relative, which is what the sound driver needs.
double log2(double x) {
    if (x <= 0) return -1e308;
    union {
        double d;
        uint64_t u;
    } v = {x};
    int e = (int) ((v.u >> 52) & 0x7ff) - 1023;
    v.u = (v.u & 0x000fffffffffffffull) | 0x3ff0000000000000ull; // mantissa in [1, 2)
    double m = v.d;
    if (m > 1.41421356237) {
        m *= 0.5;
        e++;
    }
    double t = (m - 1) / (m + 1), t2 = t * t;
    double s = t * (2 + t2 * (2.0 / 3 + t2 * (2.0 / 5 + t2 * (2.0 / 7 + t2 * (2.0 / 9 + t2 * (2.0 / 11))))));
    return e + s * 1.4426950408889634;
}

double exp2(double x) {
    if (x > 1023) return 1e308;
    if (x < -1022) return 0;
    double fl = __builtin_floor(x);
    double f = (x - fl) * 0.6931471805599453; // fraction as a natural exponent
    double p = 1 + f * (1 + f * (0.5 + f * (1.0 / 6 + f * (1.0 / 24 + f * (1.0 / 120 + f * (1.0 / 720 + f / 5040))))));
    union {
        double d;
        uint64_t u;
    } v = {p};
    v.u += (uint64_t) (int64_t) fl << 52;
    return v.d;
}

double pow(double x, double y) {
    if (y == 0) return 1;
    if (x <= 0) return 0;
    return exp2(y * log2(x));
}
float powf(float x, float y) { return (float) pow(x, y); }
double log10(double x) { return log2(x) * 0.30102999566398120; }
