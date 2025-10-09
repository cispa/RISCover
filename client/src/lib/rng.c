#include "rng.h"

void rng_init(shared_rng *rng, uint64_t seed) {
    rng->mt[0] = (uint32_t)(seed & 0xFFFFFFFF) ^ (uint32_t)(seed >> 32);
    for (int i = 1; i < MT19937_N; i++) {
        rng->mt[i] = (1812433253UL * (rng->mt[i - 1] ^ (rng->mt[i - 1] >> 30)) + i);
    }
    rng->index = MT19937_N;
}

static void rng_twist(shared_rng *rng) {
    for (int i = 0; i < MT19937_N; i++) {
        uint32_t y = (rng->mt[i] & MT19937_UPPER_MASK) | (rng->mt[(i + 1) % MT19937_N] & MT19937_LOWER_MASK);
        uint32_t xA = y >> 1;
        if (y % 2 != 0) {
            xA ^= 0x9908B0DF;
        }
        rng->mt[i] = rng->mt[(i + MT19937_M) % MT19937_N] ^ xA;
    }
    rng->index = 0;
}

uint64_t rng_next(shared_rng *rng) {
    if (rng->index >= MT19937_N) {
        rng_twist(rng);
    }

    uint32_t y = rng->mt[rng->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680;
    y ^= (y << 15) & 0xEFC60000;
    y ^= (y >> 18);

    return (uint64_t)y;
}

int64_t rng_randint(shared_rng *rng, int64_t a, int64_t b) {
    return a + (rng_next(rng) % (b - a + 1));
}
