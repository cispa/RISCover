#include <stdint.h>

#include "rng.h"

// NOTE: this function might return a bigger immediate then requested
// that is fine because we strip in the generation nevertheless
uint32_t random_unsigned_immediate(shared_rng* rng, unsigned bits) {
    switch (rng_next(rng) % 3) {
        case 0:
            return 0;
        case 1:
            return (1<<bits)-1;
        case 2:
            // Pick a small immediate in 90% of the random cases
            if (rng_randint(rng, 1, 10) <= 9) {
                return rng_randint(rng, 0, 16);
            } else {
                return rng_next(rng);
            }
    }
    return 0;
}

uint32_t random_signed_immediate(shared_rng* rng, unsigned bits) {
    switch (rng_next(rng) % 5) {
        case 0:
            return 0;
        case 1:
            return (uint32_t)-1;
        case 2:
            // min
            return (uint32_t)-(1<<(bits-1));
        case 3:
            // max
            return (uint32_t)((1<<(bits-1))-1);
        case 4:
            // Pick a small immediate in 90% of the random cases
            if (rng_randint(rng, 1, 10) <= 9) {
                return (uint32_t)rng_randint(rng, -8, 8);
            } else {
                return rng_next(rng);
            }
    }
    return 0;
}
