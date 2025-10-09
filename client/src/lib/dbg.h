#ifndef DBG_H
#define DBG_H

/* #define DEBUG */

#include <stdio.h>

#ifdef DEBUG

/* Helper implementations for printing without newline */
static inline void dbg_print_int(int x) { fprintf(stderr, "%d (0x%x)", x, x); }
static inline void dbg_print_long(long x) { fprintf(stderr, "%ld (0x%lx)", x, x); }
static inline void dbg_print_unsigned(unsigned x) { fprintf(stderr, "%u (0x%x)", x, x); }
static inline void dbg_print_unsigned_long(unsigned long x) { fprintf(stderr, "%lu (0x%lx)", x, x); }
static inline void dbg_print_float(float x) { fprintf(stderr, "%f", x); }
static inline void dbg_print_double(double x) { fprintf(stderr, "%f", x); }
static inline void dbg_print_str(const char *x) { fprintf(stderr, "\"%s\"", x); }
static inline void dbg_print_ptr(const void *x) { fprintf(stderr, "%p", x); }

/* Choose appropriate print function based on type */
#define dbg_print(x) _Generic((x), \
    int: dbg_print_int, \
    long: dbg_print_long, \
    unsigned: dbg_print_unsigned, \
    unsigned long: dbg_print_unsigned_long, \
    float: dbg_print_float, \
    double: dbg_print_double, \
    char *: dbg_print_str, \
    const char *: dbg_print_str, \
    default: dbg_print_ptr \
)(x)

/* Print a single argument: omit name if it's a compile-time constant */
#define _DBG_PRINT_ARG(x) \
    do { \
        if (__builtin_constant_p(x)) { \
            dbg_print(x); \
        } else { \
            fprintf(stderr, "%s = ", #x); \
            dbg_print(x); \
        } \
    } while (0)

#define _STRX(x) #x
#define _STR(x) _STRX(x)
#define HYP_START "\x1b]8;;" __FILE__ ":" _STR(__LINE__) "\x1b\\"
#define HYP_END   "\x1b]8;;\x1b\\"

/* Select dbg implementation by arg count (0–5) */
#define _DBG_CHOOSER(_0,_1,_2,_3,_4,_5,NAME,...) NAME
#define dbg(...) _DBG_CHOOSER(dummy, ##__VA_ARGS__, _dbg_5, _dbg_4, _dbg_3, _dbg_2, _dbg_1, _dbg_0)(__VA_ARGS__)

#define _dbg_0() \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " (reached)\n", __FILE__, __LINE__, __func__); \
        fflush(stderr); \
    } while (0)

/* Single-arg: always just print the value */
#define _dbg_1(x) \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " ", __FILE__, __LINE__, __func__); \
        _DBG_PRINT_ARG(x); \
        fprintf(stderr, "\n"); \
        fflush(stderr); \
    } while (0)

#define _dbg_2(x,y) \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " ", __FILE__, __LINE__, __func__); \
        _DBG_PRINT_ARG(x); \
        fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(y); \
        fprintf(stderr, "\n"); \
        fflush(stderr); \
    } while (0)

#define _dbg_3(x,y,z) \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " ", __FILE__, __LINE__, __func__); \
        _DBG_PRINT_ARG(x); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(y); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(z); \
        fprintf(stderr, "\n"); \
        fflush(stderr); \
    } while (0)

#define _dbg_4(a,b,c,d) \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " ", __FILE__, __LINE__, __func__); \
        _DBG_PRINT_ARG(a); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(b); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(c); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(d); \
        fprintf(stderr, "\n"); \
        fflush(stderr); \
    } while (0)

#define _dbg_5(a,b,c,d,e) \
    do { \
        fprintf(stderr, HYP_START "[%s:%-4d %s]" HYP_END " ", __FILE__, __LINE__, __func__); \
        _DBG_PRINT_ARG(a); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(b); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(c); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(d); fprintf(stderr, "; "); \
        _DBG_PRINT_ARG(e); \
        fprintf(stderr, "\n"); \
        fflush(stderr); \
    } while (0)

#else /* DEBUG */

#define dbg(...) ((void)0)

#endif /* DEBUG */

#endif /* DBG_H */

