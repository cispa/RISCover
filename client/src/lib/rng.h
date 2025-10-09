#pragma once

#include <stdint.h>

#define MT19937_N 624
#define MT19937_M 397
#define MT19937_UPPER_MASK 0x80000000UL
#define MT19937_LOWER_MASK 0x7fffffffUL

typedef struct {
    uint32_t mt[MT19937_N];
    int index;
} shared_rng;

void rng_init(shared_rng *rng, uint64_t seed);
uint64_t rng_next(shared_rng *rng);
int64_t rng_randint(shared_rng *rng, int64_t a, int64_t b);
