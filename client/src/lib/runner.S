#include "asm_util.h"

#if defined(__riscv)
#include "riscv64/runner.S"
#elif defined(__aarch64__)
#include "aarch64/runner.S"
#endif

runner:
    SAVE_STATE __regs_restore
#ifdef META
    SAVE_META meta_pre
#endif
    RESTORE_STATE_EXTENDED __regs_before
#if defined(__riscv)
    la   x29, __regs_before
    la   a0, runner_start
    jalr x0, a0
#elif defined(__aarch64__)
    ldr x29, =__regs_before
    ldr x0, =runner_start
    br  x0
#endif

.section ".runner_end", "ax"
runner_end:
    SAVE_STATE_EXTENDED __regs_result
#ifdef META
    SAVE_META meta_post
#endif
    RESTORE_STATE __regs_restore
    ret

/******************************** Runner Sandbox *********************************/

.section ".runner", "ax"
runner_sandbox:
    FILL_INVALID
runner_start:
#if defined(__riscv)
    LD_A0
    LD_X29
#elif defined(__aarch64__)
    LD_X0
    LD_X29
#endif
    // NOTE: this is power of two (4 byte aligned)
    .p2align 2
runner_code_start:
    // Fill seq-len nops
    .fill MAX_SEQ_LEN, 4, NOP_32
runner_code_end:
#if defined(__riscv)
    li x29, RUNNER_END_ADDR
    jalr x0, x29
#elif defined(__aarch64__)
    // Use absolute address so that we can jump anywhere (pc-relative is <4GB)
    movz x29, RUNNER_END_ADDR_32, lsl #32
    movk x29, RUNNER_END_ADDR_16, lsl #16
    br   x29
#endif
    FILL_INVALID

.global runner
.global runner_code_start
.global runner_code_end
// NOTE: this label is here to prevent garbage collection in Makefile (--require-defined)
.global runner_end
