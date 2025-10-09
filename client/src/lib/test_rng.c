#include "rng.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    shared_rng rng;
    shared_rng rng2;
    rng_init(&rng, 101);
    rng_init(&rng2, 100);
    for (int i = 0; i < 10; i++) {
        printf("%lx, ", rng_randint(&rng, 0, 7));
    }
    printf("\n");
    for (int i = 0; i < 10; i++) {
        printf("%lx, ", rng_randint(&rng2, 0, 7));
    }
    printf("\n");
    for (int i = 0; i < 10; i++) {
        printf("%lx, ", rng_randint(&rng, 0, 1ul<<32));
    }
    printf("\n");
    for (int i = 0; i < 10; i++) {
        printf("%lx, ", rng_randint(&rng2, 0, 1ul<<32));
    }
    return 0;
}
