#include <float.h>
#include <stdint.h>
#include <tgmath.h>
#include <limits.h>

#include "fuzzing_value_map.h"

/*
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!                                                                                        !!
!!  NOTE: When modifiying this file, make sure to generate fuzzing_value_map.py with the  !!
!!  print-fuzing-value-map binary. E.g.:                                                  !!
!!  build-client --seq-len 1 --target print-fuzing-value-map --out print-fuzing-value-map !!
!!  qemu-riscv64 ./print-fuzing-value-map > pyutils/riscv/fuzzing_value_map.py            !!
!!  qemu-aarch64 ./print-fuzing-value-map > pyutils/arm/fuzzing_value_map.py              !!
!!                                                                                        !!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
*/

#define APPLY_SANDWICH_OFFSETS(base) \
    ((base)+1*page_size-1), ((base)+1*page_size-3), ((base)+1*page_size-4), ((base)+1*page_size-7), ((base)+1*page_size-8), ((base)+1*page_size-16)

#define APPLY_START_END_OFFSETS(base) \
    ((base)+0*page_size-1), ((base)+0*page_size-3), ((base)+0*page_size-4), ((base)+0*page_size-7), ((base)+0*page_size-8), ((base)+0*page_size-16), \
    ((base)+2*page_size-1), ((base)+2*page_size-3), ((base)+2*page_size-4), ((base)+2*page_size-7), ((base)+2*page_size-8), ((base)+2*page_size-16)

#define VALID_ADDR 0x800000

uint64_t filler_64 = 0xdeadbeefdeadbeef;

// NOTE: If compile-time flags are used here make sure to port them to python
// via print-fuzzing-value-map.c
uint64_t fuzzing_value_map_gp[] = {
 0,
(uint8_t)-1,
(uint16_t)-1,
(uint32_t)-1,
(uint64_t)-1,
 1,
 2,
 42,
 1337,

 INT8_MAX,
INT16_MAX,
INT32_MAX,
INT64_MAX,

 INT8_MIN,
INT16_MIN,
INT32_MIN,
INT64_MIN,

 UINT8_MAX,
UINT16_MAX,
UINT32_MAX,
UINT64_MAX,

NOP_32, NOP_32, NOP_32, NOP_32,
APPLY_SANDWICH_OFFSETS(VALID_ADDR), APPLY_START_END_OFFSETS(VALID_ADDR),
};
const size_t fuzzing_value_map_gp_size = sizeof(fuzzing_value_map_gp);

union fpv fuzzing_value_map_fp[] = {
{ .dbl =  0.0 },
{ .dbl = -0.0 },
{ .flt = -0.0 },
{ .dbl =  1.0 },
{ .flt =  1.0 },
{ .dbl =  2.0 },
{ .flt =  2.0 },
{ .dbl = 42.0 },
{ .flt = 42.0 },
{ .dbl = 1337.0 },
{ .flt = 1337.0 },

{ .flt = INFINITY },
{ .flt = -INFINITY },
{ .dbl = INFINITY },
{ .dbl = -INFINITY },
{ .flt = NAN },
{ .flt = -NAN },
{ .dbl = NAN },
{ .dbl = -NAN },

{ .flt = FLT_MAX },
{ .flt = -FLT_MAX },
{ .dbl = DBL_MAX },
{ .dbl = -DBL_MAX },

{ .flt = FLT_MIN },
{ .flt = -FLT_MIN },
{ .dbl = DBL_MIN },
{ .dbl = -DBL_MIN },

// subnormal
{ .flt = FLT_MIN/2 },
{ .flt = -FLT_MIN/2 },
{ .dbl = DBL_MIN/2 },
{ .dbl = -DBL_MIN/2 },
{ .flt = FLT_TRUE_MIN },
{ .flt = -FLT_TRUE_MIN },
{ .dbl = DBL_TRUE_MIN },
{ .dbl = -DBL_TRUE_MIN },
};
const size_t fuzzing_value_map_fp_size = sizeof(fuzzing_value_map_fp);

uint64_t fuzzing_value_map_gp_val_or_rand(shared_rng* rng) {
    unsigned n = sizeof(fuzzing_value_map_gp)/sizeof(fuzzing_value_map_gp[0]);
    unsigned i = rng_next(rng) % (n+1);
    // In one out of n+1 cases return random number
    if (i == n) {
        return rng_next(rng);
    } else {
        return fuzzing_value_map_gp[i];
    }
}

uint64_t fuzzing_value_map_fp_val_or_rand(shared_rng* rng) {
    unsigned n = sizeof(fuzzing_value_map_fp)/sizeof(fuzzing_value_map_fp[0]);
    unsigned i = rng_next(rng) % (n+1);
    // In one out of n+1 cases return random number
    if (i == n) {
        return rng_next(rng);
    } else {
        return fuzzing_value_map_fp[i].u;
    }
}

uint64_t fuzzing_value_any_val(shared_rng* rng) {
    if (rng_next(rng) % 2 == 0) {
        return fuzzing_value_map_fp_val_or_rand(rng);
    } else {
        return fuzzing_value_map_gp_val_or_rand(rng);
    }
}
