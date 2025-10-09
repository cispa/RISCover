#pragma once

// TODO: remove contraint?
// for that we need a way to use offsetof below because on riscv float and vector regs are not joined
#if !defined(FLOATS) && defined(VECTOR)
    #error "vector -> floats on riscv64"
#endif

// TODO: why does lsp complain?
// stdint is thereo
// /nix/store/mxm0c62lhwvxy04pafciwx9f07hqgp91-riscv64-unknown-linux-gnu-gcc-12.3.0/lib/gcc/riscv64-unknown-linux-gnu/12.3.0/include
// but the included file is not
// is this a problem with stdint.h?
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "../util.h"
#include "constants.h"

// Higher than aarch64 because of instructions like vsse. Those can introduce big memory diffs.
// See client/misc/riscv64-lots-of-memory-diffs.yaml
#define CHECK_MEM_MAX_TRIES 50
#define CHECK_MEM_MAX_NUMBER_MEM_CHANGES 100

#define REGS_BEFORE_SP "ra"
#define REGS_AFTER_SP \
        "gp", "tp", "t0", "t1", "t2"
#define REGS_AFTER_S0 \
        "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", \
        "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", \
        "s11", "t3", "t4", "t5", "t6"
#define REGS_FP "ft0", "ft1", "ft2", "ft3",                            \
        "ft4", "ft5", "ft6", "ft7", "fs0", "fs1", "fa0", "fa1", "fa2", \
        "fa3", "fa4", "fa5", "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", \
        "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", \
        "ft10", "ft11"
#define REGS_VECTOR "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", \
        "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", \
        "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", \
        "v29", "v30", "v31"

/* // NOTE: gcc complains about sp and also about fp (s0) */
/* #define NORMAL_CLOBBERS "memory", REGS_BEFORE_SP, REGS_AFTER_SP, REGS_AFTER_S0 */

/* // NOTE: we can't add vector regs here */
/* #define ALL_CLOBBERS NORMAL_CLOBBERS, REGS_FP */

/* #define STORE_NORMAL_REGS \ */
/*             " sd  ra,     0(a5)\n\t" \ */
/*             " sd  sp,     8(a5)\n\t" \ */
/*             " sd  gp,    16(a5)\n\t" \ */
/*             " sd  tp,    24(a5)\n\t" \ */
/*             " sd  t0,    32(a5)\n\t" \ */
/*             " sd  t1,    40(a5)\n\t" \ */
/*             " sd  t2,    48(a5)\n\t" \ */
/*             " sd  s0,    56(a5)\n\t" \ */
/*             " sd  s1,    64(a5)\n\t" \ */
/*             " sd  a0,    72(a5)\n\t" \ */
/*             " sd  a1,    80(a5)\n\t" \ */
/*             " sd  a2,    88(a5)\n\t" \ */
/*             " sd  a3,    96(a5)\n\t" \ */
/*             " sd  a4,   104(a5)\n\t" \ */
/*             " sd  a5,   112(a5)\n\t" \ */
/*             " sd  a6,   120(a5)\n\t" \ */
/*             " sd  a7,   128(a5)\n\t" \ */
/*             " sd  s2,   136(a5)\n\t" \ */
/*             " sd  s3,   144(a5)\n\t" \ */
/*             " sd  s4,   152(a5)\n\t" \ */
/*             " sd  s5,   160(a5)\n\t" \ */
/*             " sd  s6,   168(a5)\n\t" \ */
/*             " sd  s7,   176(a5)\n\t" \ */
/*             " sd  s8,   184(a5)\n\t" \ */
/*             " sd  s9,   192(a5)\n\t" \ */
/*             " sd s10,   200(a5)\n\t" \ */
/*             " sd s11,   208(a5)\n\t" \ */
/*             " sd  t3,   216(a5)\n\t" \ */
/*             " sd  t4,   224(a5)\n\t" \ */
/*             " sd  t5,   232(a5)\n\t" \ */
/*             " sd  t6,   240(a5)\n\t" */

/* #ifdef VECTOR */
/* #define STORE_VECTOR_REGS \ */
/*             "vsetvli t6, x0, e8, m8\n\t" \ */
/*             /\* vsb.v   v0,(a5) *\/ \ */
/*             ".fill 1, 4, 0x02078027\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             /\* vsb.v   v8,(a5) *\/ \ */
/*             ".fill 1, 4, 0x02078427\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             /\* vsb.v   v16,(a5) *\/ \ */
/*             ".fill 1, 4, 0x02078827\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             /\* vsb.v   v24,(a5) *\/ \ */
/*             ".fill 1, 4, 0x02078c27\n\t" \ */
/*             "vsetvli t6, x0, e8, m1\n\t" */


/*             /\* "vsetvli x0, x0, e8, m1\n\t" \ *\/ */
/*             /\* /\\* vsb.v   v0,(a5) *\\/ \ *\/ */
/*             /\* ".fill 1, 4, 0x02078027\n\t" \ *\/ */



/*             /\* "lla  a5, "STR(regs)"\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vs8r.v v0, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vs8r.v v8, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vs8r.v v16, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vs8r.v v24, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" *\/ */

/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vs8r.v  v0,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v1,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v2,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v3,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v4,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v5,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v6,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v7,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v8,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v  v9,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v10,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v11,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v12,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v13,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v14,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v15,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v16,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v17,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v18,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v19,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v20,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v21,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v22,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v23,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v24,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v25,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v26,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v27,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v28,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v29,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v30,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vse64.v v31,  0(a5)\n\t" *\/ */
/* #else */
/* #define STORE_VECTOR_REGS */
/* #endif */

/* // TODO: we could theoretically use offsetof for all of this, but this did not work because we couldnt convert the output of offsetof to string (or if we did, we didnt have the number) */
/* #define SAVE_STATE(regs)             \ */
/*         asm volatile(                \ */
/*             STR(UNIQUE_LABEL(save_state_##regs))":\n\t" \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/* STORE_NORMAL_REGS \ */
/*         ::: "a5"); */

/* #define SAVE_STATE_EXTENDED(regs)             \ */
/*         asm volatile(                \ */
/*             STR(UNIQUE_LABEL(save_state_extended_##regs))":\n\t" \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/* STORE_NORMAL_REGS \ */
/*             "fsd  f0,   248(a5)\n\t" \ */
/*             "fsd  f1,   256(a5)\n\t" \ */
/*             "fsd  f2,   264(a5)\n\t" \ */
/*             "fsd  f3,   272(a5)\n\t" \ */
/*             "fsd  f4,   280(a5)\n\t" \ */
/*             "fsd  f5,   288(a5)\n\t" \ */
/*             "fsd  f6,   296(a5)\n\t" \ */
/*             "fsd  f7,   304(a5)\n\t" \ */
/*             "fsd  f8,   312(a5)\n\t" \ */
/*             "fsd  f9,   320(a5)\n\t" \ */
/*             "fsd f10,   328(a5)\n\t" \ */
/*             "fsd f11,   336(a5)\n\t" \ */
/*             "fsd f12,   344(a5)\n\t" \ */
/*             "fsd f13,   352(a5)\n\t" \ */
/*             "fsd f14,   360(a5)\n\t" \ */
/*             "fsd f15,   368(a5)\n\t" \ */
/*             "fsd f16,   376(a5)\n\t" \ */
/*             "fsd f17,   384(a5)\n\t" \ */
/*             "fsd f18,   392(a5)\n\t" \ */
/*             "fsd f19,   400(a5)\n\t" \ */
/*             "fsd f20,   408(a5)\n\t" \ */
/*             "fsd f21,   416(a5)\n\t" \ */
/*             "fsd f22,   424(a5)\n\t" \ */
/*             "fsd f23,   432(a5)\n\t" \ */
/*             "fsd f24,   440(a5)\n\t" \ */
/*             "fsd f25,   448(a5)\n\t" \ */
/*             "fsd f26,   456(a5)\n\t" \ */
/*             "fsd f27,   464(a5)\n\t" \ */
/*             "fsd f28,   472(a5)\n\t" \ */
/*             "fsd f29,   480(a5)\n\t" \ */
/*             "fsd f30,   488(a5)\n\t" \ */
/*             "fsd f31,   496(a5)\n\t" \ */
/*             "frcsr           t0\n\t" \ */
/*             " sd  t0,   504(a5)\n\t" \ */
/*             "addi a5, a5, 512\n\t" \ */
/* STORE_VECTOR_REGS \ */
/*             ::: "a5", "t0"); */

/* #define SAVE_STATE_VECTOR(regs)             \ */
/*         asm volatile(                \ */
/*             STR(UNIQUE_LABEL(save_state_vector_##regs))":\n\t" \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/*             "addi a5, a5, 512\n\t" \ */
/* STORE_VECTOR_REGS \ */
/*             ::: "a5"); */

// TODO
/* #define SAVE_META(meta)     \ */
/*     asm volatile(           \ */
/*         "rdcycle    %0\n\t" \ */
/*         "rdinstret  %1\n\t" \ */
/*     : "=r" (meta.cycle), "=r" (meta.instret)); */

/* #define LD_A0 " ld  a0,    72(a5)\n\t" */
/* #define LD_A1 " ld  a1,    80(a5)\n\t" */
/* #define LD_A2 " ld  a2,    88(a5)\n\t" */
/* #define LD_A5 " ld  a5,   112(a5)\n\t" */

/* #define LOAD_NORMAL_REGS \ */
/*             " ld  ra,     0(a5)\n\t" \ */
/*             " ld  sp,     8(a5)\n\t" \ */
/*             " ld  gp,    16(a5)\n\t" \ */
/*             " ld  tp,    24(a5)\n\t" \ */
/*             " ld  t0,    32(a5)\n\t" \ */
/*             " ld  t1,    40(a5)\n\t" \ */
/*             " ld  t2,    48(a5)\n\t" \ */
/*             " ld  s0,    56(a5)\n\t" \ */
/*             " ld  s1,    64(a5)\n\t" \ */
/*             LD_A0                    \ */
/*             LD_A1                    \ */
/*             LD_A2                    \ */
/*             " ld  a3,    96(a5)\n\t" \ */
/*             " ld  a4,   104(a5)\n\t" \ */
/*             " ld  a6,   120(a5)\n\t" \ */
/*             " ld  a7,   128(a5)\n\t" \ */
/*             " ld  s2,   136(a5)\n\t" \ */
/*             " ld  s3,   144(a5)\n\t" \ */
/*             " ld  s4,   152(a5)\n\t" \ */
/*             " ld  s5,   160(a5)\n\t" \ */
/*             " ld  s6,   168(a5)\n\t" \ */
/*             " ld  s7,   176(a5)\n\t" \ */
/*             " ld  s8,   184(a5)\n\t" \ */
/*             " ld  s9,   192(a5)\n\t" \ */
/*             " ld s10,   200(a5)\n\t" \ */
/*             " ld s11,   208(a5)\n\t" \ */
/*             " ld  t3,   216(a5)\n\t" \ */
/*             " ld  t4,   224(a5)\n\t" \ */
/*             " ld  t5,   232(a5)\n\t" \ */
/*             " ld  t6,   240(a5)\n\t" \ */
/*             LD_A5 */

/* /\* https://github.com/riscv/riscv-opcodes/commit/3f9532c0085800d49c963dc58d3bde451544d3ed *\/ */
/* /\* https://github.com/riscv/riscv-v-spec/releases *\/ */
/* /\* vmv.v.x would definitely work*\/ */
/* /\* http://riscv.epcc.ed.ac.uk/issues/compiling-vector/ *\/ */
/* // test what this does https://github.com/RISCVtestbed/rvv-rollback */

/* /\* vl8re8.v *\/ */
/* /\* vl8re16.v *\/ */
/* /\* vl8re32.v *\/ */
/* /\* vl8re64.v# Load v8-v15 with 8*VLEN/8 bytes from address in a0 *\/ */
/* /\* v8, (a0) *\/ */
/* /\* v8, (a0) *\/ */
/* /\* v8, (a0) *\/ */
/* /\* v8, (a0) *\/ */
/* /\* vs1r.v v3, (a1) *\/ */
/* /\* vs2r.v v2, (a1) *\/ */
/* /\* vs4r.v v4, (a1) *\/ */
/* /\* vs8r.v v8, (a1) *\/ */
/* /\* # Store v3 to address in a1 *\/ */
/* /\* # Store v2-v3 to address in a1 *\/ */
/* /\* # Store v4-v7 to address in a1 *\/ */
/* /\* # Store v8-v15 to address in a1 *\/ */


/* // TODO: docfuzz reports sigill for vlb_v but it does work when compiled with thead toolchain */
/* // check opcode */

/* /\* xuantie-toolchain/riscv64/bin/riscv64-unknown-elf-gcc main.c -o main -march=rv64gcv0p7 *\/ */
/*    /\* 1015e:       1207e007                vlw.v   v0,(a5) *\/ */
/*    /\* 1015e:       1207e087                vlw.v   v1,(a5) *\/ */
/*    /\* 10162:       1207e107                vlw.v   v2,(a5) *\/ */
/*    /\* 10166:       1207e187                vlw.v   v3,(a5) *\/ */
/*    /\* 1016a:       1207e207                vlw.v   v4,(a5) *\/ */
/*    /\* 1016e:       1207e287                vlw.v   v5,(a5) *\/ */
/*    /\* 10172:       1207e307                vlw.v   v6,(a5) *\/ */
/*    /\* 10176:       1207e387                vlw.v   v7,(a5) *\/ */
/*    /\* 1017a:       1207e407                vlw.v   v8,(a5) *\/ */
/*    /\* 1017e:       1207e487                vlw.v   v9,(a5) *\/ */
/*    /\* 10182:       1207e507                vlw.v   v10,(a5) *\/ */
/*    /\* 10186:       1207e587                vlw.v   v11,(a5) *\/ */
/*    /\* 1018a:       1207e607                vlw.v   v12,(a5) *\/ */
/*    /\* 1018e:       1207e687                vlw.v   v13,(a5) *\/ */
/*    /\* 10192:       1207e707                vlw.v   v14,(a5) *\/ */
/*    /\* 10196:       1207e787                vlw.v   v15,(a5) *\/ */
/*    /\* 1019a:       1207e807                vlw.v   v16,(a5) *\/ */
/*    /\* 1019e:       1207e887                vlw.v   v17,(a5) *\/ */
/*    /\* 101a2:       1207e907                vlw.v   v18,(a5) *\/ */
/*    /\* 101a6:       1207e987                vlw.v   v19,(a5) *\/ */
/*    /\* 101aa:       1207ea07                vlw.v   v20,(a5) *\/ */
/*    /\* 101ae:       1207ea87                vlw.v   v21,(a5) *\/ */
/*    /\* 101b2:       1207eb07                vlw.v   v22,(a5) *\/ */
/*    /\* 101b6:       1207eb87                vlw.v   v23,(a5) *\/ */
/*    /\* 101ba:       1207ec07                vlw.v   v24,(a5) *\/ */
/*    /\* 101be:       1207ec87                vlw.v   v25,(a5) *\/ */
/*    /\* 101c2:       1207ed07                vlw.v   v26,(a5) *\/ */
/*    /\* 101c6:       1207ed87                vlw.v   v27,(a5) *\/ */
/*    /\* 101ca:       1207ee07                vlw.v   v28,(a5) *\/ */
/*    /\* 101ce:       1207ee87                vlw.v   v29,(a5) *\/ */
/*    /\* 101d2:       1207ef07                vlw.v   v30,(a5) *\/ */
/*    /\* 101d6:       1207ef87                vlw.v   v31,(a5) *\/ */

/*    /\* 10150:       12078007                vlb.v   v0,(a5) *\/ */
/*    /\* 10154:       12078087                vlb.v   v1,(a5) *\/ */
/*    /\* 10158:       12078107                vlb.v   v2,(a5) *\/ */
/*    /\* 1015c:       12078187                vlb.v   v3,(a5) *\/ */
/*    /\* 10160:       12078207                vlb.v   v4,(a5) *\/ */
/*    /\* 10164:       12078287                vlb.v   v5,(a5) *\/ */
/*    /\* 10168:       12078307                vlb.v   v6,(a5) *\/ */
/*    /\* 1016c:       12078387                vlb.v   v7,(a5) *\/ */
/*    /\* 10170:       12078407                vlb.v   v8,(a5) *\/ */
/*    /\* 10174:       12078487                vlb.v   v9,(a5) *\/ */
/*    /\* 10178:       12078507                vlb.v   v10,(a5) *\/ */
/*    /\* 1017c:       12078587                vlb.v   v11,(a5) *\/ */
/*    /\* 10180:       12078607                vlb.v   v12,(a5) *\/ */
/*    /\* 10184:       12078687                vlb.v   v13,(a5) *\/ */
/*    /\* 10188:       12078707                vlb.v   v14,(a5) *\/ */
/*    /\* 1018c:       12078787                vlb.v   v15,(a5) *\/ */
/*    /\* 10190:       12078807                vlb.v   v16,(a5) *\/ */
/*    /\* 10194:       12078887                vlb.v   v17,(a5) *\/ */
/*    /\* 10198:       12078907                vlb.v   v18,(a5) *\/ */
/*    /\* 1019c:       12078987                vlb.v   v19,(a5) *\/ */
/*    /\* 101a0:       12078a07                vlb.v   v20,(a5) *\/ */
/*    /\* 101a4:       12078a87                vlb.v   v21,(a5) *\/ */
/*    /\* 101a8:       12078b07                vlb.v   v22,(a5) *\/ */
/*    /\* 101ac:       12078b87                vlb.v   v23,(a5) *\/ */
/*    /\* 101b0:       12078c07                vlb.v   v24,(a5) *\/ */
/*    /\* 101b4:       12078c87                vlb.v   v25,(a5) *\/ */
/*    /\* 101b8:       12078d07                vlb.v   v26,(a5) *\/ */
/*    /\* 101bc:       12078d87                vlb.v   v27,(a5) *\/ */
/*    /\* 101c0:       12078e07                vlb.v   v28,(a5) *\/ */
/*    /\* 101c4:       12078e87                vlb.v   v29,(a5) *\/ */
/*    /\* 101c8:       12078f07                vlb.v   v30,(a5) *\/ */
/*    /\* 101cc:       12078f87                vlb.v   v31,(a5) *\/ */
/*    /\* 101d0:       0001                    nop *\/ */
/*    /\* 101d2:       02078027                vsb.v   v0,(a5) *\/ */
/*    /\* 101d6:       020780a7                vsb.v   v1,(a5) *\/ */
/*    /\* 101da:       02078127                vsb.v   v2,(a5) *\/ */
/*    /\* 101de:       020781a7                vsb.v   v3,(a5) *\/ */
/*    /\* 101e2:       02078227                vsb.v   v4,(a5) *\/ */
/*    /\* 101e6:       020782a7                vsb.v   v5,(a5) *\/ */
/*    /\* 101ea:       02078327                vsb.v   v6,(a5) *\/ */
/*    /\* 101ee:       020783a7                vsb.v   v7,(a5) *\/ */
/*    /\* 101f2:       02078427                vsb.v   v8,(a5) *\/ */
/*    /\* 101f6:       020784a7                vsb.v   v9,(a5) *\/ */
/*    /\* 101fa:       02078527                vsb.v   v10,(a5) *\/ */
/*    /\* 101fe:       020785a7                vsb.v   v11,(a5) *\/ */
/*    /\* 10202:       02078627                vsb.v   v12,(a5) *\/ */
/*    /\* 10206:       020786a7                vsb.v   v13,(a5) *\/ */
/*    /\* 1020a:       02078727                vsb.v   v14,(a5) *\/ */
/*    /\* 1020e:       020787a7                vsb.v   v15,(a5) *\/ */
/*    /\* 10212:       02078827                vsb.v   v16,(a5) *\/ */
/*    /\* 10216:       020788a7                vsb.v   v17,(a5) *\/ */
/*    /\* 1021a:       02078927                vsb.v   v18,(a5) *\/ */
/*    /\* 1021e:       020789a7                vsb.v   v19,(a5) *\/ */
/*    /\* 10222:       02078a27                vsb.v   v20,(a5) *\/ */
/*    /\* 10226:       02078aa7                vsb.v   v21,(a5) *\/ */
/*    /\* 1022a:       02078b27                vsb.v   v22,(a5) *\/ */
/*    /\* 1022e:       02078ba7                vsb.v   v23,(a5) *\/ */
/*    /\* 10232:       02078c27                vsb.v   v24,(a5) *\/ */
/*    /\* 10236:       02078ca7                vsb.v   v25,(a5) *\/ */
/*    /\* 1023a:       02078d27                vsb.v   v26,(a5) *\/ */
/*    /\* 1023e:       02078da7                vsb.v   v27,(a5) *\/ */
/*    /\* 10242:       02078e27                vsb.v   v28,(a5) *\/ */
/*    /\* 10246:       02078ea7                vsb.v   v29,(a5) *\/ */
/*    /\* 1024a:       02078f27                vsb.v   v30,(a5) *\/ */
/*    /\* 1024e:       02078fa7                vsb.v   v31,(a5) *\/ */

/* // TODO: we can do multiple better things here, */
/* // either always add to a5, or find some way to store all with only one instruction */
/* // or one with immediate offset */
/* #ifdef VECTOR */
/* #define LOAD_VECTOR_REGS \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/*             "addi a5, a5, 512\n\t" \ */
/*             "vsetvli t6, x0, e8, m8\n\t" \ */
/*             "vle8.v  v0,   0(a5)\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             "vle8.v  v8,   0(a5)\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             "vle8.v  v16,   0(a5)\n\t" \ */
/*             "addi a5, a5, 128\n\t" \ */
/*             "vle8.v  v24,   0(a5)\n\t" \ */
/*             "vsetvli t6, x0, e8, m1\n\t" */

/*             /\* "lla  a5, "STR(regs)"\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vl8re64.v v0, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vl8re64.v v8, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vl8re64.v v16, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* "vl8re64.v v24, 0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" *\/ */

/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e007\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e087\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e107\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e187\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e207\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e287\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e307\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e387\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e407\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e487\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e507\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e587\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e607\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e687\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e707\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e787\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e807\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e887\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e907\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207e987\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ea07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ea87\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207eb07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207eb87\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ec07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ec87\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ed07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ed87\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ee07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ee87\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ef07\n\t" \ *\/ */
/*             /\* "addi a5, a5, 512\n\t" \ *\/ */
/*             /\* ".fill 1, 4, 0x1207ef87\n\t" *\/ */

/* // TODO: still gives SIGLL */
/* // this above is "vlw.v v1, (a5)\n" */





/*             /\* "vlb.v v0, 0(a5)\n\t" *\/ */


/*             /\* "vle64.v  v0,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v1,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v2,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v3,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v4,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v5,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v6,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v7,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v8,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v  v9,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v10,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v11,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v12,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v13,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v14,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v15,   0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v16,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v17,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v18,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v19,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v20,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v21,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v22,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v23,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v24,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v25,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v26,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v27,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v28,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v29,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v30,  0(a5)\n\t" \ *\/ */
/*             /\* "addi a5, a5, 64\n\t" \ *\/ */
/*             /\* "vle64.v v31,  0(a5)\n\t" *\/ */
/* /\* #define CLOBBERS_VECTOR , REGS_VECTOR *\/ */
/* // TODO: maybe we can just not set clobbers there */
/* // and we probably dont need it since it is not used */
/* #define CLOBBERS_VECTOR */
/* #else */
/* #define LOAD_VECTOR_REGS */
/* #define CLOBBERS_VECTOR */
/* #endif */

/* #define RESTORE_STATE(regs)     \ */
/*         asm volatile(                \ */
/*             STR(UNIQUE_LABEL(restore_state_##regs))":\n\t" \ */
/*             /\* Reset fcsr state too *\/ \ */
/*             "li a5, 0\n\t" \ */
/*             "fscsr a5\n\t" \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/* LOAD_NORMAL_REGS \ */
/*         ::: NORMAL_CLOBBERS); */

/* // TODO: fix inconsistency on who loads regs into a5 */
/* #define RESTORE_STATE_EXTENDED(regs)     \ */
/*         asm volatile(                \ */
/*             STR(UNIQUE_LABEL(restore_state_extended_##regs))":\n\t" \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/*             " ld  t0,   504(a5)\n\t" \ */
/*             "fscsr           t0\n\t" \ */
/*             "fld  f0,   248(a5)\n\t" \ */
/*             "fld  f1,   256(a5)\n\t" \ */
/*             "fld  f2,   264(a5)\n\t" \ */
/*             "fld  f3,   272(a5)\n\t" \ */
/*             "fld  f4,   280(a5)\n\t" \ */
/*             "fld  f5,   288(a5)\n\t" \ */
/*             "fld  f6,   296(a5)\n\t" \ */
/*             "fld  f7,   304(a5)\n\t" \ */
/*             "fld  f8,   312(a5)\n\t" \ */
/*             "fld  f9,   320(a5)\n\t" \ */
/*             "fld f10,   328(a5)\n\t" \ */
/*             "fld f11,   336(a5)\n\t" \ */
/*             "fld f12,   344(a5)\n\t" \ */
/*             "fld f13,   352(a5)\n\t" \ */
/*             "fld f14,   360(a5)\n\t" \ */
/*             "fld f15,   368(a5)\n\t" \ */
/*             "fld f16,   376(a5)\n\t" \ */
/*             "fld f17,   384(a5)\n\t" \ */
/*             "fld f18,   392(a5)\n\t" \ */
/*             "fld f19,   400(a5)\n\t" \ */
/*             "fld f20,   408(a5)\n\t" \ */
/*             "fld f21,   416(a5)\n\t" \ */
/*             "fld f22,   424(a5)\n\t" \ */
/*             "fld f23,   432(a5)\n\t" \ */
/*             "fld f24,   440(a5)\n\t" \ */
/*             "fld f25,   448(a5)\n\t" \ */
/*             "fld f26,   456(a5)\n\t" \ */
/*             "fld f27,   464(a5)\n\t" \ */
/*             "fld f28,   472(a5)\n\t" \ */
/*             "fld f29,   480(a5)\n\t" \ */
/*             "fld f30,   488(a5)\n\t" \ */
/*             "fld f31,   496(a5)\n\t" \ */
/* LOAD_VECTOR_REGS \ */
/*             "lla  a5, "STR(regs)"\n\t" \ */
/* LOAD_NORMAL_REGS \ */
/*         ::: NORMAL_CLOBBERS, REGS_FP CLOBBERS_VECTOR); */
/*         /\* "lui a5,"STR(FUZZING_PAGE_MID_NON_SHIFTED)"\n\t" \ *\/ */
/*         /\* "slli a5, a5,   24\n\t" \ *\/ */

typedef uint64_t reg;

struct gp {
    union {
        reg   ra;
        reg   x1;
    };
    union {
        reg   sp;
        reg   x2;
    };
    union {
        reg   gp;
        reg   x3;
    };
    union {
        reg   tp;
        reg   x4;
    };
    union {
        reg   t0;
        reg   x5;
    };
    union {
        reg   t1;
        reg   x6;
    };
    union {
        reg   t2;
        reg   x7;
    };
    union {
        reg   s0;
        reg   fp;
        reg   x8;
    };
    union {
        reg   s1;
        reg   x9;
    };
    union {
        reg   a0;
        reg   x10;
    };
    union {
        reg   a1;
        reg   x11;
    };
    union {
        reg   a2;
        reg   x12;
    };
    union {
        reg   a3;
        reg   x13;
    };
    union {
        reg   a4;
        reg   x14;
    };
    union {
        reg   a5;
        reg   x15;
    };
    union {
        reg   a6;
        reg   x16;
    };
    union {
        reg   a7;
        reg   x17;
    };
    union {
        reg   s2;
        reg   x18;
    };
    union {
        reg   s3;
        reg   x19;
    };
    union {
        reg   s4;
        reg   x20;
    };
    union {
        reg   s5;
        reg   x21;
    };
    union {
        reg   s6;
        reg   x22;
    };
    union {
        reg   s7;
        reg   x23;
    };
    union {
        reg   s8;
        reg   x24;
    };
    union {
        reg   s9;
        reg   x25;
    };
    union {
        reg  s10;
        reg  x26;
    };
    union {
        reg  s11;
        reg  x27;
    };
    union {
        reg   t3;
        reg   x28;
    };
    union {
        reg   t4;
        reg   x29;
    };
    union {
        reg   t5;
        reg   x30;
    };
    union {
        reg   t6;
        reg   x31;
    };
};

union fpv {
    float  flt;
    double dbl;
    uint64_t u;
};

struct fp {
    union {
        union fpv  f0;
        union fpv  ft0;
    };
    union {
        union fpv  f1;
        union fpv  ft1;
    };
    union {
        union fpv  f2;
        union fpv  ft2;
    };
    union {
        union fpv  f3;
        union fpv  ft3;
    };
    union {
        union fpv  f4;
        union fpv  ft4;
    };
    union {
        union fpv  f5;
        union fpv  ft5;
    };
    union {
        union fpv  f6;
        union fpv  ft6;
    };
    union {
        union fpv  f7;
        union fpv  ft7;
    };
    union {
        union fpv  f8;
        union fpv  fs0;
    };
    union {
        union fpv  f9;
        union fpv  fs1;
    };
    union {
        union fpv  f10;
        union fpv  fa0;
    };
    union {
        union fpv  f11;
        union fpv  fa1;
    };
    union {
        union fpv  f12;
        union fpv  fa2;
    };
    union {
        union fpv  f13;
        union fpv  fa3;
    };
    union {
        union fpv  f14;
        union fpv  fa4;
    };
    union {
        union fpv  f15;
        union fpv  fa5;
    };
    union {
        union fpv  f16;
        union fpv  fa6;
    };
    union {
        union fpv  f17;
        union fpv  fa7;
    };
    union {
        union fpv  f18;
        union fpv  fs2;
    };
    union {
        union fpv  f19;
        union fpv  fs3;
    };
    union {
        union fpv  f20;
        union fpv  fs4;
    };
    union {
        union fpv  f21;
        union fpv  fs5;
    };
    union {
        union fpv  f22;
        union fpv  fs6;
    };
    union {
        union fpv  f23;
        union fpv  fs7;
    };
    union {
        union fpv  f24;
        union fpv  fs8;
    };
    union {
        union fpv  f25;
        union fpv  fs9;
    };
    union {
        union fpv  f26;
        union fpv  fs10;
    };
    union {
        union fpv  f27;
        union fpv  fs11;
    };
    union {
        union fpv  f28;
        union fpv  ft8;
    };
    union {
        union fpv  f29;
        union fpv  ft9;
    };
    union {
        union fpv  f30;
        union fpv  ft10;
    };
    union {
        union fpv  f31;
        union fpv  ft11;
    };
};
typedef struct vv {
    uint8_t v[VEC_REG_SIZE];
} vv;
struct vec {
    vv  v0;
    vv  v1;
    vv  v2;
    vv  v3;
    vv  v4;
    vv  v5;
    vv  v6;
    vv  v7;
    vv  v8;
    vv  v9;
    vv v10;
    vv v11;
    vv v12;
    vv v13;
    vv v14;
    vv v15;
    vv v16;
    vv v17;
    vv v18;
    vv v19;
    vv v20;
    vv v21;
    vv v22;
    vv v23;
    vv v24;
    vv v25;
    vv v26;
    vv v27;
    vv v28;
    vv v29;
    vv v30;
    vv v31;
};

struct regs {
    struct gp gp;
    #ifdef FLOATS
    /* https://five-embeddev.com/riscv-isa-manual/latest/f.html */
    reg fcsr;
    struct fp fp;
    #endif
    #ifdef VECTOR
    struct vec vec;
    #endif
};

struct meta {
    reg cycle;
    reg instret;
};

// TODO: deduplicate
typedef uint64_t freg;

// TODO: move to shared header
#define getregindex(member) offsetof(struct regs, member)/sizeof(reg)
#define getregindex_float(member) offsetof(struct fp, member)/sizeof(union fpv)
#define getabiindex(member) offsetof(struct regs, member)/sizeof(reg)
#define getabiindex_float(member) getregindex(fp.f0)+getregindex_float(member)

#define LOOP_OVER_GP_DIFF(_regs_before, _regs_after, _code)                         \
    for (unsigned _i = getregindex(gp.x1); _i <= getregindex(gp.x31); _i++)         \
    {                                                                               \
        reg* before = &((reg*)&_regs_before->gp)[_i];                               \
        reg*  after =  &((reg*)&_regs_after->gp)[_i];                               \
        int abi_i   = _i + getabiindex(gp.x1);                                      \
        if (*before != *after) {                                                    \
            _code                                                                   \
        }                                                                           \
    }                                                                               \

#define LOOP_OVER_GP(_regs_before, _code)                                           \
    for (unsigned _i = getregindex(gp.x1); _i <= getregindex(gp.x31); _i++)         \
    {                                                                               \
        reg* val = &((reg*)&_regs_before->gp)[_i];                                  \
        int abi_i   = _i + getabiindex(gp.x1);                                      \
        _code                                                                       \
    }                                                                               \

#define LOOP_OVER_FP_DIFF(_regs_before, _regs_after, _code)                         \
    for (unsigned _i = getregindex_float(f0); _i <= getregindex_float(f31); _i++)   \
    {                                                                               \
        union fpv* before = &((union fpv*)&_regs_before->fp)[_i];                   \
        union fpv* after  =  &((union fpv*)&_regs_after->fp)[_i];                   \
        int         abi_i = _i + getabiindex_float(f0);                             \
        if (before->u != after->u) {                                                \
            _code                                                                   \
        }                                                                           \
    }

#define LOOP_OVER_FP(_regs_before, _code)                                           \
    for (unsigned _i = getregindex_float(f0); _i <= getregindex_float(f31); _i++)   \
    {                                                                               \
        union fpv* val = &((union fpv*)&_regs_before->fp)[_i];                      \
        int      abi_i = _i + getabiindex_float(f0);                                \
        _code                                                                       \
    }

const char* get_abi_name(unsigned i);
const char* get_abi_name_float(unsigned i);
void print_reg_diff(const struct regs* regs_before, const struct regs* regs_after);
void print_reg_diff_opts(FILE* file, const char* prefix, const struct regs* regs_before, const struct regs* regs_after, int color_on);
