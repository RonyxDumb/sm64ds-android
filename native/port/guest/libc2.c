// More of the guest's C library: what only the game's code calls (Wi-Fi string parsing, square roots).
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "string.h"
#include "stdlib.h"

float sqrtf(float x) { return __builtin_sqrtf(x); }
double sqrt(double x) { return __builtin_sqrt(x); }

char *strstr(const char *h, const char *n) {
    size_t ln = strlen(n);
    if (!ln) return (char *) h;
    for (; *h; h++) {
        if (*h == *n && !strncmp(h, n, ln)) return (char *) h;
    }
    return NULL;
}

double atof(const char *s) {
    while (*s == ' ') s++;
    bool neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    double v = 0, scale = 1;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale /= 10;
            v += (*s++ - '0') * scale;
        }
    }
    return neg ? -v : v;
}

// %d %i %u %x %s %c with widths, enough for the Wi-Fi code's protocol parsing.
int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int count = 0;
    while (*fmt) {
        if (*fmt == ' ') {
            while (*str == ' ' || *str == '\t' || *str == '\n') str++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*str != *fmt) break;
            str++, fmt++;
            continue;
        }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        char conv = *fmt++;
        if (conv != 'c') {
            while (*str == ' ') str++;
        }
        if (!*str) break;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x') {
            char *end;
            long v = conv == 'x' ? (long) strtoul(str, &end, 16) : strtol(str, &end, conv == 'i' ? 0 : 10);
            if (end == str) break;
            str = end;
            *va_arg(ap, int *) = (int) v;
            count++;
        } else if (conv == 's') {
            char *out = va_arg(ap, char *);
            int n = 0;
            while (*str && *str != ' ' && (!width || n < width)) out[n++] = *str++;
            out[n] = 0;
            count++;
        } else if (conv == 'c') {
            *va_arg(ap, char *) = *str++;
            count++;
        } else {
            break;
        }
    }
    va_end(ap);
    return count;
}
