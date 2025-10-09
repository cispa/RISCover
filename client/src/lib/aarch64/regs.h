#pragma once

#if defined(FLOATS) && defined(VECTOR)
    #error "vector and float are exclusive on aarch64"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "../util.h"

#define VEC_REG_SIZE 16
typedef struct vv {
    uint8_t v[VEC_REG_SIZE];
} vv;

#define CHECK_MEM_MAX_TRIES 5
#define CHECK_MEM_MAX_NUMBER_MEM_CHANGES 32

#define REGS_GP "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", \
                "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", \
                "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", \
                "x28", "x29", "x30"
#define REGS_FP "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", \
                "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", \
                "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27", \
                "d28", "d29", "d30", "d31"
#define REGS_VECTOR "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", \
        "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", \
        "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", \
        "v29", "v30", "v31"

typedef uint64_t reg;

struct gp {
    reg   x0;
    reg   x1;
    reg   x2;
    reg   x3;
    reg   x4;
    reg   x5;
    reg   x6;
    reg   x7;
    reg   x8;
    reg   x9;
    reg  x10;
    reg  x11;
    reg  x12;
    reg  x13;
    reg  x14;
    reg  x15;
    reg  x16;
    reg  x17;
    reg  x18;
    reg  x19;
    reg  x20;
    reg  x21;
    reg  x22;
    reg  x23;
    reg  x24;
    reg  x25;
    reg  x26;
    reg  x27;
    reg  x28;
    reg  x29;
    union {
        reg  x30;
        reg   lr;
    };
    reg sp;
};

typedef uint64_t freg;
union fpv {
    float  flt;
    double dbl;
    uint64_t u;
#ifdef VECTOR
    // Vector and floating point registers are merged on aarch64
    vv v;
#endif
};
struct fp {
    union fpv  d0;
    union fpv  d1;
    union fpv  d2;
    union fpv  d3;
    union fpv  d4;
    union fpv  d5;
    union fpv  d6;
    union fpv  d7;
    union fpv  d8;
    union fpv  d9;
    union fpv d10;
    union fpv d11;
    union fpv d12;
    union fpv d13;
    union fpv d14;
    union fpv d15;
    union fpv d16;
    union fpv d17;
    union fpv d18;
    union fpv d19;
    union fpv d20;
    union fpv d21;
    union fpv d22;
    union fpv d23;
    union fpv d24;
    union fpv d25;
    union fpv d26;
    union fpv d27;
    union fpv d28;
    union fpv d29;
    union fpv d30;
    union fpv d31;
};

struct vec {
    vv  v0;
    vv  v1;
    vv  v2;
    vv  v3;
    vv  v4;
    vv  v5;
    vv  v6;
    vv  v7;
    vv  v8;
    vv  v9;
    vv v10;
    vv v11;
    vv v12;
    vv v13;
    vv v14;
    vv v15;
    vv v16;
    vv v17;
    vv v18;
    vv v19;
    vv v20;
    vv v21;
    vv v22;
    vv v23;
    vv v24;
    vv v25;
    vv v26;
    vv v27;
    vv v28;
    vv v29;
    vv v30;
    vv v31;
};

struct regs {
    struct gp gp;
    reg pstate;
#if defined(FLOATS) || defined(VECTOR)
    reg fpsr;
    union {
        // NOTE: vector and fp registers are shared
        struct fp fp;
  #ifdef VECTOR
        struct vec vec;
  #endif
    };
#endif
};

struct meta {
    reg cycle;
    reg instret;
};

// TODO: move to shared header
#define getregindex(member) offsetof(struct regs, member)/sizeof(reg)
#define getregindex_float(member) offsetof(struct fp, member)/sizeof(union fpv)
#define getregindex_vec(member) offsetof(struct vec, member)/sizeof(vv)
#define getabiindex(member) offsetof(struct regs, member)/sizeof(reg)
#define getabiindex_float(member) getregindex(fp.d0)+getregindex_float(member)
#define getabiindex_vec(member) getregindex(vec.v0)+getregindex_vec(member)

#define LOOP_OVER_GP_DIFF(_regs_before, _regs_after, _code)                         \
    for (unsigned _i = getregindex(gp.x0); _i <= getregindex(gp.sp); _i++)          \
    {                                                                               \
        reg* before = &((reg*)&_regs_before->gp)[_i];                               \
        reg*  after =  &((reg*)&_regs_after->gp)[_i];                               \
        int abi_i   = _i + getabiindex(gp.x0);                                      \
        if (*before != *after) {                                                    \
            _code                                                                   \
        }                                                                           \
    }                                                                               \

#define LOOP_OVER_GP(_regs_before, _code)                                           \
    for (unsigned _i = getregindex(gp.x0); _i <= getregindex(gp.sp); _i++)          \
    {                                                                               \
        reg* val  = &((reg*)&_regs_before->gp)[_i];                                 \
        int abi_i = _i + getabiindex(gp.x0);                                        \
        _code                                                                       \
    }                                                                               \

#define LOOP_OVER_FP(_regs_before, _code)                                           \
    for (unsigned _i = getregindex_float(d0); _i <= getregindex_float(d31); _i++)   \
    {                                                                               \
        union fpv* val = &((union fpv*)&_regs_before->fp)[_i];                      \
        int      abi_i = _i + getabiindex_float(d0);                                \
        _code                                                                       \
    }

#define LOOP_OVER_FP_DIFF(_regs_before, _regs_after, _code)                         \
    for (unsigned _i = getregindex_float(d0); _i <= getregindex_float(d31); _i++)   \
    {                                                                               \
        union fpv* before = &((union fpv*)&_regs_before->fp)[_i];                   \
        union fpv* after  =  &((union fpv*)&_regs_after->fp)[_i];                   \
        int         abi_i = _i + getabiindex_float(d0);                             \
        if (before->u != after->u) {                                                \
            _code                                                                   \
        }                                                                           \
    }

const char* get_abi_name(unsigned i);
const char* get_abi_name_float(unsigned i);
void print_reg_diff(const struct regs* regs_before, const struct regs* regs_after);
void print_reg_diff_opts(FILE* file, const char* prefix, const struct regs* regs_before, const struct regs* regs_after, int color_on);
