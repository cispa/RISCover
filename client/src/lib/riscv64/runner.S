#include "constants.h"

.macro STORE_GP
    sd  ra,     0(x29)
    sd  sp,     8(x29)
    sd  gp,    16(x29)
    sd  tp,    24(x29)
    sd  t0,    32(x29)
    sd  t1,    40(x29)
    sd  t2,    48(x29)
    sd  s0,    56(x29)
    sd  s1,    64(x29)
    sd  a0,    72(x29)
    sd  a1,    80(x29)
    sd  a2,    88(x29)
    sd  a3,    96(x29)
    sd  a4,   104(x29)
    sd  a5,   112(x29)
    sd  a6,   120(x29)
    sd  a7,   128(x29)
    sd  s2,   136(x29)
    sd  s3,   144(x29)
    sd  s4,   152(x29)
    sd  s5,   160(x29)
    sd  s6,   168(x29)
    sd  s7,   176(x29)
    sd  s8,   184(x29)
    sd  s9,   192(x29)
    sd s10,   200(x29)
    sd s11,   208(x29)
    sd  t3,   216(x29)
    sd  t4,   224(x29)
    sd  t5,   232(x29)
    sd  t6,   240(x29)
.endm

.macro STORE_FP
    fsd  f0,   256(x29)
    fsd  f1,   264(x29)
    fsd  f2,   272(x29)
    fsd  f3,   280(x29)
    fsd  f4,   288(x29)
    fsd  f5,   296(x29)
    fsd  f6,   304(x29)
    fsd  f7,   312(x29)
    fsd  f8,   320(x29)
    fsd  f9,   328(x29)
    fsd f10,   336(x29)
    fsd f11,   344(x29)
    fsd f12,   352(x29)
    fsd f13,   360(x29)
    fsd f14,   368(x29)
    fsd f15,   376(x29)
    fsd f16,   384(x29)
    fsd f17,   392(x29)
    fsd f18,   400(x29)
    fsd f19,   408(x29)
    fsd f20,   416(x29)
    fsd f21,   424(x29)
    fsd f22,   432(x29)
    fsd f23,   440(x29)
    fsd f24,   448(x29)
    fsd f25,   456(x29)
    fsd f26,   464(x29)
    fsd f27,   472(x29)
    fsd f28,   480(x29)
    fsd f29,   488(x29)
    fsd f30,   496(x29)
    fsd f31,   504(x29)
.endm

.macro STORE_VEC
    addi x29, x29, 512
    // NOTE: This tries to set the vector reg size but we still introduce diffs
    // if the physical vector reg size is not equal on machines (see comment in runner.c)
    li t0, VEC_REG_SIZE
    vsetvli x0, t0, e8, m1
    vse8.v  v0, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v1, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v2, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v3, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v4, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v5, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v6, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v7, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v8, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v9, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v10, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v11, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v12, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v13, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v14, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v15, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v16, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v17, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v18, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v19, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v20, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v21, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v22, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v23, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v24, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v25, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v26, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v27, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v28, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v29, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v30, 0(x29)
    addi    x29,   x29, 16
    vse8.v  v31, 0(x29)
    addi    x29,   x29, 16
    addi x29, x29, -512
    addi x29, x29, -512
.endm

.macro SAVE_STATE regs
save_state_\regs\()_\()__COUNTER__:
    la  x29, \regs
    STORE_GP
.endm

.macro SAVE_STATE_EXTENDED regs
save_state_extended_\regs\()_\()__COUNTER__:
    la  x29, \regs
    STORE_GP

#ifdef FLOATS
    STORE_FP

    frcsr t0
    sd t0, 248(x29)
#endif
#ifdef VECTOR
    STORE_VEC
#endif
.endm

.macro SAVE_STATE_VECTOR regs
save_state_vector_\regs\()_\()__COUNTER__:
    la  x29, \regs
    STORE_VEC
.endm

.macro LD_A0
    ld  a0,    72(x29)
.endm
.macro LD_X29
    ld  x29,   224(x29)
.endm
.macro LD_TP
    ld  tp,    24(x29)
.endm

.macro LOAD_GP
    ld  ra,     0(x29)
    ld  sp,     8(x29)
    ld  gp,    16(x29)
    LD_TP
    ld  t0,    32(x29)
    ld  t1,    40(x29)
    ld  t2,    48(x29)
    ld  s0,    56(x29)
    ld  s1,    64(x29)
    LD_A0
    ld  a1,    80(x29)
    ld  a2,    88(x29)
    ld  a3,    96(x29)
    ld  a4,   104(x29)
    ld  a5,   112(x29)
    ld  a6,   120(x29)
    ld  a7,   128(x29)
    ld  s2,   136(x29)
    ld  s3,   144(x29)
    ld  s4,   152(x29)
    ld  s5,   160(x29)
    ld  s6,   168(x29)
    ld  s7,   176(x29)
    ld  s8,   184(x29)
    ld  s9,   192(x29)
    ld s10,   200(x29)
    ld s11,   208(x29)
    ld  t3,   216(x29)
    ld  t5,   232(x29)
    ld  t6,   240(x29)
    LD_X29
.endm

#ifdef FLOATS
.macro LOAD_FP
    fld  f0,   256(x29)
    fld  f1,   264(x29)
    fld  f2,   272(x29)
    fld  f3,   280(x29)
    fld  f4,   288(x29)
    fld  f5,   296(x29)
    fld  f6,   304(x29)
    fld  f7,   312(x29)
    fld  f8,   320(x29)
    fld  f9,   328(x29)
    fld f10,   336(x29)
    fld f11,   344(x29)
    fld f12,   352(x29)
    fld f13,   360(x29)
    fld f14,   368(x29)
    fld f15,   376(x29)
    fld f16,   384(x29)
    fld f17,   392(x29)
    fld f18,   400(x29)
    fld f19,   408(x29)
    fld f20,   416(x29)
    fld f21,   424(x29)
    fld f22,   432(x29)
    fld f23,   440(x29)
    fld f24,   448(x29)
    fld f25,   456(x29)
    fld f26,   464(x29)
    fld f27,   472(x29)
    fld f28,   480(x29)
    fld f29,   488(x29)
    fld f30,   496(x29)
    fld f31,   504(x29)
.endm
#endif

.macro LOAD_VEC regs
    addi x29, x29, 512
    // NOTE: This tries to set the vector reg size but we still introduce diffs
    // if the physical vector reg size is not equal on machines (see comment in runner.c)
    li t0, VEC_REG_SIZE
    vsetvli x0, t0, e8, m1
    vle8.v  v0, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v1, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v2, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v3, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v4, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v5, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v6, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v7, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v8, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v9, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v10, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v11, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v12, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v13, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v14, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v15, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v16, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v17, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v18, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v19, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v20, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v21, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v22, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v23, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v24, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v25, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v26, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v27, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v28, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v29, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v30, 0(x29)
    addi    x29,   x29, 16
    vle8.v  v31, 0(x29)
    addi    x29,   x29, 16
    addi x29, x29, -512
    addi x29, x29, -512
.endm

.macro RESTORE_STATE regs
restore_state_\regs\()_\()__COUNTER__:
    // TODO: do we want to reset vector too?
#ifdef FLOATS
    // Reset fcsr to zero
    li x29, 0
    fscsr x29
#endif
    la  x29, \regs
    LOAD_GP
.endm

.macro RESTORE_STATE_EXTENDED regs
restore_state_extended_\regs\()_\()__COUNTER__:
#ifdef FLOATS
    // Reset fcsr to zero
    li x29, 0
    fscsr x29
#endif
    la  x29, \regs
#ifdef FLOATS
    LOAD_FP
#endif
#ifdef VECTOR
    LOAD_VEC
#endif
LOAD_GP
.endm

.macro SAVE_META meta
    la x29, \meta
    rdinstret t0
    sd t0, 8(x29)
    rdcycle t0
    sd t0, 0(x29)
.endm

.macro FILL_INVALID
    // 0x100000/2=0x80000
    .fill 0x80000, 4, INVALID_32
.endm

// TODO: delete stuff in old file but move comments
