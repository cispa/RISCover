// TODO: stp
.macro STORE_GP
    str x0,    [x29,   #0]
    str x1,    [x29,   #8]
    str x2,    [x29,  #16]
    str x3,    [x29,  #24]
    str x4,    [x29,  #32]
    str x5,    [x29,  #40]
    str x6,    [x29,  #48]
    str x7,    [x29,  #56]
    str x8,    [x29,  #64]
    str x9,    [x29,  #72]
    str x10,   [x29,  #80]
    str x11,   [x29,  #88]
    str x12,   [x29,  #96]
    str x13,   [x29, #104]
    str x14,   [x29, #112]
    str x15,   [x29, #120]
    str x16,   [x29, #128]
    str x17,   [x29, #136]
    str x18,   [x29, #144]
    str x19,   [x29, #152]
    str x20,   [x29, #160]
    str x21,   [x29, #168]
    str x22,   [x29, #176]
    str x23,   [x29, #184]
    str x24,   [x29, #192]
    str x25,   [x29, #200]
    str x26,   [x29, #208]
    str x27,   [x29, #216]
    str x28,   [x29, #224]
    str x30,   [x29, #240]
    mov  x0,   sp
    str  x0,   [x29, #248]
.endm

// TODO: are others important?
// nzcv, fpsr, fpcr https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers
// CNTVCT_EL0
// cntfrq_el0
// tpidr_el0
// tpidrro_el0

// TODO: d21 is broken for some reason
.macro STORE_FP
    str  d0,   [x29,  #272]
    str  d1,   [x29,  #280]
    str  d2,   [x29,  #288]
    str  d3,   [x29,  #296]
    str  d4,   [x29,  #304]
    str  d5,   [x29,  #312]
    str  d6,   [x29,  #320]
    str  d7,   [x29,  #328]
    str  d8,   [x29,  #336]
    str  d9,   [x29,  #344]
    str d10,   [x29,  #352]
    str d11,   [x29,  #360]
    str d12,   [x29,  #368]
    str d13,   [x29,  #376]
    str d14,   [x29,  #384]
    str d15,   [x29,  #392]
    str d16,   [x29,  #400]
    str d17,   [x29,  #408]
    str d18,   [x29,  #416]
    str d19,   [x29,  #424]
    str d20,   [x29,  #432]
    str d21,   [x29,  #440]
    str d22,   [x29,  #448]
    str d23,   [x29,  #456]
    str d24,   [x29,  #464]
    str d25,   [x29,  #472]
    str d26,   [x29,  #480]
    str d27,   [x29,  #488]
    str d28,   [x29,  #496]
    str d29,   [x29,  #504]
    str d30,   [x29,  #512]
    str d31,   [x29,  #520]
.endm

.macro STORE_VEC
    add x29, x29, #272
    st1  {v0.16b}, [x29], #16
    st1  {v1.16b}, [x29], #16
    st1  {v2.16b}, [x29], #16
    st1  {v3.16b}, [x29], #16
    st1  {v4.16b}, [x29], #16
    st1  {v5.16b}, [x29], #16
    st1  {v6.16b}, [x29], #16
    st1  {v7.16b}, [x29], #16
    st1  {v8.16b}, [x29], #16
    st1  {v9.16b}, [x29], #16
    st1 {v10.16b}, [x29], #16
    st1 {v11.16b}, [x29], #16
    st1 {v12.16b}, [x29], #16
    st1 {v13.16b}, [x29], #16
    st1 {v14.16b}, [x29], #16
    st1 {v15.16b}, [x29], #16
    st1 {v16.16b}, [x29], #16
    st1 {v17.16b}, [x29], #16
    st1 {v18.16b}, [x29], #16
    st1 {v19.16b}, [x29], #16
    st1 {v20.16b}, [x29], #16
    st1 {v21.16b}, [x29], #16
    st1 {v22.16b}, [x29], #16
    st1 {v23.16b}, [x29], #16
    st1 {v24.16b}, [x29], #16
    st1 {v25.16b}, [x29], #16
    st1 {v26.16b}, [x29], #16
    st1 {v27.16b}, [x29], #16
    st1 {v28.16b}, [x29], #16
    st1 {v29.16b}, [x29], #16
    st1 {v30.16b}, [x29], #16
    st1 {v31.16b}, [x29], #16
    sub x29, x29, #512
    sub x29, x29, #272
.endm

.macro SAVE_STATE regs
save_state_\regs\()_\()__COUNTER__:
    ldr  x29, =\regs
    STORE_GP
.endm

.macro SAVE_STATE_EXTENDED regs
save_state_extended_\regs\()_\()__COUNTER__:
    ldr  x29, =\regs
    STORE_GP

    // pstate
    mrs  x0,   nzcv
    str  x0,   [x29, #256]

#if defined(FLOATS) || defined(VECTOR)
    // fpsr
    mrs  x0,   fpsr
    str  x0,   [x29, #264]
#endif
#ifdef FLOATS
    STORE_FP
#endif
#ifdef VECTOR
    STORE_VEC
#endif
.endm

#ifdef VECTOR
.macro SAVE_STATE_VECTOR regs
save_state_vector_\regs\()_\()__COUNTER__:
    ldr  x29, =\regs
    STORE_VEC
.endm
#endif

.macro LD_X0
    ldr x0,    [x29,  #0]
.endm
.macro LD_X29
    ldr x29,   [x29, #232]
.endm
.macro LD_X2
    ldr x2,    [x29,  #16]
.endm

.macro LOAD_GP
    ldr  x1,   [x29,   #8]
    ldr  x2,   [x29,  #16]
    ldr  x3,   [x29,  #24]
    ldr  x4,   [x29,  #32]
    ldr  x5,   [x29,  #40]
    ldr  x6,   [x29,  #48]
    ldr  x7,   [x29,  #56]
    ldr  x8,   [x29,  #64]
    ldr  x9,   [x29,  #72]
    ldr x10,   [x29,  #80]
    ldr x11,   [x29,  #88]
    ldr x12,   [x29,  #96]
    ldr x13,   [x29, #104]
    ldr x14,   [x29, #112]
    ldr x15,   [x29, #120]
    ldr x16,   [x29, #128]
    ldr x17,   [x29, #136]
    ldr x18,   [x29, #144]
    ldr x19,   [x29, #152]
    ldr x20,   [x29, #160]
    ldr x21,   [x29, #168]
    ldr x22,   [x29, #176]
    ldr x23,   [x29, #184]
    ldr x24,   [x29, #192]
    ldr x25,   [x29, #200]
    ldr x26,   [x29, #208]
    ldr x27,   [x29, #216]
    ldr x28,   [x29, #224]
    ldr x30,   [x29, #240]
    ldr  x0,   [x29, #248]
    mov sp, x0
    LD_X0
    LD_X29
.endm

.macro LOAD_FP
    ldr  d0,   [x29,  #272]
    ldr  d1,   [x29,  #280]
    ldr  d2,   [x29,  #288]
    ldr  d3,   [x29,  #296]
    ldr  d4,   [x29,  #304]
    ldr  d5,   [x29,  #312]
    ldr  d6,   [x29,  #320]
    ldr  d7,   [x29,  #328]
    ldr  d8,   [x29,  #336]
    ldr  d9,   [x29,  #344]
    ldr d10,   [x29,  #352]
    ldr d11,   [x29,  #360]
    ldr d12,   [x29,  #368]
    ldr d13,   [x29,  #376]
    ldr d14,   [x29,  #384]
    ldr d15,   [x29,  #392]
    ldr d16,   [x29,  #400]
    ldr d17,   [x29,  #408]
    ldr d18,   [x29,  #416]
    ldr d19,   [x29,  #424]
    ldr d20,   [x29,  #432]
    ldr d21,   [x29,  #440]
    ldr d22,   [x29,  #448]
    ldr d23,   [x29,  #456]
    ldr d24,   [x29,  #464]
    ldr d25,   [x29,  #472]
    ldr d26,   [x29,  #480]
    ldr d27,   [x29,  #488]
    ldr d28,   [x29,  #496]
    ldr d29,   [x29,  #504]
    ldr d30,   [x29,  #512]
    ldr d31,   [x29,  #520]
.endm

.macro LOAD_VEC
    add x29, x29, #272
    ld1  {v0.16b}, [x29], #16
    ld1  {v1.16b}, [x29], #16
    ld1  {v2.16b}, [x29], #16
    ld1  {v3.16b}, [x29], #16
    ld1  {v4.16b}, [x29], #16
    ld1  {v5.16b}, [x29], #16
    ld1  {v6.16b}, [x29], #16
    ld1  {v7.16b}, [x29], #16
    ld1  {v8.16b}, [x29], #16
    ld1  {v9.16b}, [x29], #16
    ld1 {v10.16b}, [x29], #16
    ld1 {v11.16b}, [x29], #16
    ld1 {v12.16b}, [x29], #16
    ld1 {v13.16b}, [x29], #16
    ld1 {v14.16b}, [x29], #16
    ld1 {v15.16b}, [x29], #16
    ld1 {v16.16b}, [x29], #16
    ld1 {v17.16b}, [x29], #16
    ld1 {v18.16b}, [x29], #16
    ld1 {v19.16b}, [x29], #16
    ld1 {v20.16b}, [x29], #16
    ld1 {v21.16b}, [x29], #16
    ld1 {v22.16b}, [x29], #16
    ld1 {v23.16b}, [x29], #16
    ld1 {v24.16b}, [x29], #16
    ld1 {v25.16b}, [x29], #16
    ld1 {v26.16b}, [x29], #16
    ld1 {v27.16b}, [x29], #16
    ld1 {v28.16b}, [x29], #16
    ld1 {v29.16b}, [x29], #16
    ld1 {v30.16b}, [x29], #16
    ld1 {v31.16b}, [x29], #16
    sub x29, x29, #512
    sub x29, x29, #272
.endm

.macro RESTORE_STATE regs
restore_state_\regs\()_\()__COUNTER__:
    ldr  x29, =\regs
    LOAD_GP
.endm

.macro RESTORE_STATE_EXTENDED regs
restore_state_\regs\()_\()__COUNTER__:
    ldr x29, =\regs
    // Zero out pstate
    // TODO: we currently only set nzcv
    mov x0, #0
    msr nzcv, x0
#if defined(FLOATS) || defined(VECTOR)
    // Zero out fpsr
    msr fpsr, x0
#endif
#ifdef FLOATS
    LOAD_FP
#endif
#ifdef VECTOR
    LOAD_VEC
#endif
    LOAD_GP
.endm

.macro SAVE_META meta
    ldr x29, =\meta
    mrs x0, cntvct_el0
    str x0,   [x29]
.endm

.macro FILL_INVALID
    // Make sure that this is bigger than 1/2 * 1/4 * page size.
    //                                    (we fill twice and the instructions are 4 byte wide)
    .fill 0x800, 4, INVALID_32
.endm
