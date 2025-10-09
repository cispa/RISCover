#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/regs.h"

struct regs regs;

int main(int argc, char** argv) {
        if (argc != 2) {
            printf("Usage: %s <hex>\n", argv[0]);
            return EXIT_FAILURE;
        }

        uint8_t val;

        // Use sscanf to convert the hexadecimal string to an unsigned integer
        if (sscanf(argv[1], "%hhx", &val) != 1) {
            return EXIT_FAILURE;
        }

        memset(&regs, val, sizeof(regs));

        printf("Spinning regs on %016lx\n", *((uint64_t*)&regs));

        INCLUDE_REG_MACROS();
        RESTORE_STATE_EXTENDED(regs);

        // OS should just restore the regs, like that we spin on them
        while (1) {}
}
