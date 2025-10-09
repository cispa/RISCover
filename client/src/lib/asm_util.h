#if defined(__riscv)
#define NOP_32 0x13
#define INVALID_32 0
#define TRAP_32 0x00100073
#elif defined(__aarch64__)
#define NOP_32 0xd503201f
#define INVALID_32 0
#define TRAP_32 0xd4200000
#endif
