#pragma once

#if defined(__riscv)
#include "lib/riscv64/regs.h"
#elif defined(__aarch64__)
#include "lib/aarch64/regs.h"
#endif

#define getregindex_vec(member) offsetof(struct vec, member)/sizeof(vv)
#define getabiindex_vec(member) getregindex(vec.v0)+getregindex_vec(member)

#define LOOP_OVER_VECTOR_REGS_DIFF(_regs_before, _regs_after, _code)                \
    for (unsigned _i = getregindex_vec(v0); _i <= getregindex_vec(v31); _i++)       \
    {                                                                               \
        /* We need padding here for the regs that are before */                     \
        vv* before = &((vv*)&_regs_before->vec)[_i];                                \
        vv*  after =  &((vv*)&_regs_after->vec)[_i];                                \
        int  abi_i = _i + getabiindex_vec(v0);                                      \
        if (memcmp(before, after, sizeof(*after)) != 0) {                           \
            _code                                                                   \
        }                                                                           \
    }

#define LOOP_OVER_VECTOR_REGS(_regs_before, _code)                                  \
    for (unsigned _i = getregindex_vec(v0); _i <= getregindex_vec(v31); _i++)       \
    {                                                                               \
        /* We need padding here for the regs that are before */                     \
        vv* val   = &((vv*)&_regs_before->vec)[_i];                                 \
        int abi_i = _i + getabiindex_vec(v0);                                       \
        _code                                                                       \
    }
