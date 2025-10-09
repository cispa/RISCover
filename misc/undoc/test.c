#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ranges.h"

int main() {
    unsigned index = 0;
    uint64_t i = 0;
    int executed = 0;
    while (1) {
        for (int j = 0; j < ranges[index]; j++) {
            int instr = i+j;
            /* iterate ... */
        }
        assert(index < sizeof(ranges)/sizeof(ranges[0]));
        executed += ranges[index];
        i += ranges[index++];
        if (i >= (1ul<<32)) {
            break;
        }

        assert(index < sizeof(ranges)/sizeof(ranges[0]));
        i += ranges[index++];
        printf("%lx\n", i);
        if (i >= (1ul<<32)) {
            break;
        }
    }
    printf("ending i: %lx\n", i);
    printf("executed: %x this should be around half the space\n", executed);
    return 0;
}
