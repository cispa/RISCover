#define _GNU_SOURCE
// sched_setaffinity
#include <sched.h>
// popen
#include <stdio.h>
#undef _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/user.h>
#include <asm/ptrace.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <stdbool.h>
#include <sys/personality.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <linux/auxvec.h>
#include <asm/hwcap.h>
#include <string.h>
#include <zlib.h>

// Normally defined in sys/prctl.h but can't import
// both at the same time for some reason
int prctl (int, ...);
#include <linux/prctl.h>

#include "lib/connection.h"
#include "lib/runner.h"
#include "lib/util.h"
#include "lib/log.h"
#include "lib/maps.h"
#include "lib/cpuinfo.h"
#include "lib/rng.h"
#include "lib/dbg.h"

/* #define INTRODUCE_RANDOM_DIFFS */

#ifdef JUST_SEQ_NUM
    #include "lib/randomdifffuzzgenerator.h"
#endif

// TODO: .rept instead of .fill, Static assert
// TODO: repro: fix include path?
// TODO: add floating point flag like vector
// TODO: only change stuff in linker script
// TODO: try the before after macro thing, where we have a recursive loop that goes over all registers and does before regname after

// li t0, 0xf000
#define LI_t0_1        0x0000f2b7
// lb x11, 0(x12)
#define LB_x11_x12     0x00060583

// NOTE: for this to fail set regs.f0=0 after generating inputs
#define FDIVD_F1_F1_F0  0x1a00f0d3

__attribute__((section(".elfhash"))) const char elf_hash[] = "00000000000000000000000000000000";
__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

// Flexible metadata tags
typedef struct tag {
    char *key;
    char *value;
} tag;

tag global_tag_list[10];
int global_tag_idx = 0;

#define ADD_TAG(k, v) \
    global_tag_list[global_tag_idx].key = k; \
    global_tag_list[global_tag_idx].value = v; \
    global_tag_idx++;

/******************************* CLI Parsing *********************************/

struct arguments {
    char     connect_ip[100];
    uint16_t connect_port;
    char     hostname[100];
} __attribute__((packed));

static struct arguments arguments = {
    .connect_ip = {0},
    .hostname = {0},
};

/*****************************************************************************/

#include "lib/regs.h"
#include "lib/fuzzing_value_map.h"

/******************************** Protection *********************************/

int check_ptrs_safe(uint64_t* ptrs, unsigned n) {
    int err = 0;
    for (unsigned i = 0; i < n; ++i) {
        // TODO: also check shifted targets?
        uint64_t target = ptrs[i];
        uint64_t page_start = target & ~(page_size - 1);
        // TODO: which offset? for now we just use a crazy big number
        // jalr x4, 2047(x28), 2048 is max offset
        for (int j=-1000; j<=1000; j++) {
            // we want to wrap around
            uint64_t t = page_start+j*page_size;
            if (check_page_mapped((void*)t)) {
                log_warning("%08lx (%08lx) is unsafe at offset %d (%08lx)", target, page_start, j, t);
                err = 1;
            }
        }
    }
    return err;
}

void check_fuzzing_values_safe() {
    int err = 0;
    err |= check_ptrs_safe((uint64_t*)fuzzing_value_map_gp, fuzzing_value_map_gp_size/sizeof(*fuzzing_value_map_gp));
    #ifdef FLOATS
    err |= check_ptrs_safe((uint64_t*)fuzzing_value_map_fp, fuzzing_value_map_fp_size/sizeof(*fuzzing_value_map_fp));
    #endif
    if (err) {
        print_proc_self_maps();
        exit(EXIT_FAILURE);
    }
}

#define RESULT_TAG_SINGLE 0
#define RESULT_TAG_MULTI  1

static int pack_inner_result(uint8_t* out, struct result* r, const struct regs* regs_before) {
    uint64_t out_i = 0;

    out[out_i++] = r->signum;

    #ifdef META
    *((uint16_t*)&out[out_i]) = r->meta.cycle;
    out_i+=sizeof(uint16_t);
        #if defined(__riscv)
    *((uint16_t*)&out[out_i]) = r->meta.instret;
    out_i+=sizeof(uint16_t);
        #endif
    #endif

    // skip for regs_diff
    uint8_t* regs_diff_out = &out[out_i++];
    uint8_t regs_diff = 0;

    LOOP_OVER_GP_DIFF((regs_before), (&r->regs_result),
        regs_diff++;
        out[out_i++]=abi_i;
        *((reg*)&out[out_i])=*after;
        out_i+=sizeof(*after);
    )
#if defined(__aarch64__)
    if (regs_before->pstate != r->regs_result.pstate) {
        regs_diff++;
        out[out_i++]=getabiindex(pstate);
        *((reg*)&out[out_i])=r->regs_result.pstate;
        out_i+=sizeof(r->regs_result.pstate);
    }
#endif
#if defined(__riscv) && defined(FLOATS)
    if (regs_before->fcsr != r->regs_result.fcsr) {
        regs_diff++;
        out[out_i++]=getabiindex(fcsr);
        *((reg*)&out[out_i])=r->regs_result.fcsr;
        out_i+=sizeof(r->regs_result.fcsr);
    }
#endif
#if defined(__aarch64__) && (defined(FLOATS) || defined(VECTOR))
    if (regs_before->fpsr != r->regs_result.fpsr) {
        regs_diff++;
        out[out_i++]=getabiindex(fpsr);
        *((reg*)&out[out_i])=r->regs_result.fpsr;
        out_i+=sizeof(r->regs_result.fpsr);
    }
#endif
  #ifdef FLOATS
    LOOP_OVER_FP_DIFF((regs_before), (&r->regs_result),
        regs_diff++;
        out[out_i++]=abi_i;
        *((freg*)&out[out_i])=after->u;
        out_i+=sizeof(after->u);
    )
  #endif
  #ifdef VECTOR
    LOOP_OVER_VECTOR_REGS_DIFF((regs_before), (&r->regs_result),
        regs_diff++;
        out[out_i++]=abi_i;
        memcpy(&out[out_i], after, sizeof(*after));
        out_i+=sizeof(*after);
    )
  #endif
    *regs_diff_out=regs_diff;

    if (r->signum != 0) {
        *((uint64_t*)&out[out_i]) = r->si_addrr;
        out_i+=sizeof(r->si_addrr);
        *((uint64_t*)&out[out_i]) = r->si_pc;
        out_i+=sizeof(r->si_pc);
        *((int*)&out[out_i]) = r->si_code;
        out_i+=sizeof(r->si_code);
    }

    #ifdef CHECK_MEM
    *((uint8_t*)&out[out_i]) = r->n_mem_changes;
    out_i+=sizeof(r->n_mem_changes);

    for (int i = 0; i < r->n_mem_changes; i++) {
        *((uint64_t*)&out[out_i]) = r->mem_changes[i].start;
        out_i+=sizeof(r->mem_changes[i].start);
        *((uint32_t*)&out[out_i]) = r->mem_changes[i].n;
        out_i+=sizeof(r->mem_changes[i].n);
        uint32_t n_cap = r->mem_changes[i].n < CHECK_MEM_CUT_AT ? r->mem_changes[i].n : CHECK_MEM_CUT_AT;
        memcpy(&out[out_i], r->mem_changes[i].val, n_cap);
        out_i+=n_cap;
        *((uint32_t*)&out[out_i]) = r->mem_changes[i].checksum;
        out_i+=sizeof(r->mem_changes[i].checksum);
    }
    #endif

    return out_i;
}

static int pack_result(uint8_t* out, struct result* r, const struct regs* regs_before) {
    uint64_t out_i = 0;

    uint16_t* result_bytes = (uint16_t*)&out[out_i];
    out_i += sizeof(*result_bytes);

    // Single result
    out[out_i++] = RESULT_TAG_SINGLE;

    unsigned body = pack_inner_result(&out[out_i], r, regs_before);
    out_i += body;

    assert(out_i-sizeof(*result_bytes) <= UINT16_MAX);
    *result_bytes = out_i-sizeof(*result_bytes);

    return out_i;
}

static int pack_results(uint8_t* out, struct result* results, unsigned max_seq_len, const struct regs* regs_before) {
    uint64_t out_i = 0;

    uint16_t* result_bytes = (uint16_t*)&out[out_i];
    out_i += sizeof(*result_bytes);

    // Multi result
    out[out_i++] = RESULT_TAG_MULTI;

    out[out_i++] = max_seq_len;

    for (unsigned l = 0; l < max_seq_len; l++) {
        struct result* r = &results[l];
        unsigned body = pack_inner_result(&out[out_i], r, regs_before);
        out_i += body;
    }

    assert(out_i-sizeof(*result_bytes) <= UINT16_MAX);
    *result_bytes = out_i-sizeof(*result_bytes);

    return out_i;
}

char* get_lscpu() {
    FILE *fp;
    char* buffer = malloc(4096);
    size_t bytes_read;
    char *result = NULL;
    size_t total_length = 0;
    fp = popen("lscpu", "r");
    if (fp == NULL) {
        log_perror("popen failed");
        exit(1);
    }
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        result = realloc(result, total_length + bytes_read + 1);
        if (result == NULL) {
            log_perror("realloc failed");
            exit(1);
        }
        memcpy(result + total_length, buffer, bytes_read);
        total_length += bytes_read;
    }
    if (result == NULL) {
        log_perror("Getting lscpu output failed");
        exit(1);
    }
    result[total_length] = '\0';
    pclose(fp);
    // Make sure that no difference comes from that buffer
    memset(buffer, 0, 4096);
    free(buffer);
    return result;
}

void set_buffer_sizes(int socket) {
    int buf_size = 1 << 20;
    int result;
    // Set send buffer size
    result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    if (result < 0) {
        log_perror("setsockopt SO_SNDBUF");
        exit(EXIT_FAILURE);
    }
    // Set receive buffer size
    result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    if (result < 0) {
        log_perror("setsockopt SO_RCVBUF");
        exit(EXIT_FAILURE);
    }

    // Verify the buffer sizes
    int actual_size;
    socklen_t optlen = sizeof(actual_size);

    // Verify send buffer size
    result = getsockopt(socket, SOL_SOCKET, SO_SNDBUF, &actual_size, &optlen);
    if (result == 0) {
        if (actual_size < buf_size) {
            log_warning("Couldn't set send buffer size to %d, got %d", buf_size, actual_size);
        }
    } else {
        log_perror("getsockopt SO_SNDBUF");
        exit(EXIT_FAILURE);
    }

    // Verify receive buffer size
    result = getsockopt(socket, SOL_SOCKET, SO_RCVBUF, &actual_size, &optlen);
    if (result == 0) {
        if (actual_size < buf_size) {
            log_warning("Couldn't set receive buffer size to %d, got %d", buf_size, actual_size);
        }
    } else {
        log_perror("getsockopt SO_RCVBUF");
        exit(EXIT_FAILURE);
    }
}

// NOTE: hardcoding here is not optimal but we assert below so hopefully fine
#define OUT_BIGBUFFER_SIZE (10ul<<20)
/* #ifdef VECTOR */
/* #define OUT_BIGBUFFER_SIZE (128*8+32*VEC_REG_SIZE)*max_batch_n+100*8 */
/* #else */
/* #define OUT_BIGBUFFER_SIZE 128*8*max_batch_n+100*8 */
/* #endif */

int core;
int num_cpus;
char* hostname;
char* lscpu;
char* proc_cpuinfo;
char* sys_possible;
#if defined(__riscv)
  #if defined(VECTOR)
int vec_size = VEC_REG_SIZE;
  #else
int vec_size = 0;
  #endif
#elif defined(__aarch64__)
int vec_size = 0;
int sve_max_size = 0;
int sme_max_size = 0;
int vector_enabled = 0;
#endif
void start_client() {
    log_info("Connecting to %s:%u...", arguments.connect_ip, arguments.connect_port);

    connection_context connection;
    connection.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connection.socket == -1) {
        log_perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    set_buffer_sizes(connection.socket);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(arguments.connect_port);
    server_address.sin_addr.s_addr = inet_addr(arguments.connect_ip);

    connect_with_retry(&connection, &server_address);
    log_info("Connected.");

    send_string(&connection, hostname);
    send_msg(&connection, (uint8_t*)&num_cpus, sizeof(num_cpus));
    send_msg(&connection, (uint8_t*)&core, sizeof(core));
    send_string(&connection, lscpu);
    send_string(&connection, proc_cpuinfo);
    send_string(&connection, sys_possible);
    send_msg(&connection, (uint8_t*)&vec_size, sizeof(vec_size));
#if defined(__aarch64__)
    send_msg(&connection, (uint8_t*)&sve_max_size, sizeof(sve_max_size));
    send_msg(&connection, (uint8_t*)&sme_max_size, sizeof(sme_max_size));
    // TODO: maybe set them to zero, but sve_max_size we need
    // also maybe we should use a different resetting routine depending on sve_max_size
    // maybe set to zero depending on vec_size?
    // TODO: do we want vec_size sent to the server? its 0 or 16 and will probably always be 16
    /* vec_size = 0; */
    /* sme_max_size = 0; */
    /* sve_max_size = 0; */
#endif
    send_msg(&connection, (uint8_t*)&global_tag_idx, sizeof(global_tag_idx));
    for (int i=global_tag_idx-1; i >= 0; i--) {
        send_string(&connection, global_tag_list[i].key);
        send_string(&connection, global_tag_list[i].value);
        free(global_tag_list[i].value);
    }
    memset(global_tag_list, 0, sizeof(global_tag_list));
    memset(&global_tag_idx, 0, sizeof(global_tag_idx));
    send_string(&connection, (char*)elf_hash);
    // Make sure that no differences comes from the buffers
    memset(lscpu, 0, strlen(lscpu));
    free(lscpu);
    memset(proc_cpuinfo, 0, strlen(proc_cpuinfo));
    free(proc_cpuinfo);
    memset(sys_possible, 0, strlen(sys_possible));
    free(sys_possible);
    memset(hostname, 0, strlen(hostname));
    free(hostname);
    num_cpus = 0;
    core = 0;
    memset(&arguments, 0, sizeof(arguments));

    unsigned max_batch_n;
    recv_msg_n(&connection, (uint8_t*)&max_batch_n, sizeof(max_batch_n));

    uint64_t seed;
    recv_msg_n(&connection, (uint8_t*)&seed, sizeof(seed));

#ifdef JUST_SEQ_NUM
    random_difffuzz_generator generator;
    random_difffuzz_generator_init(&generator, seed);
#else
    (void)seed;
#endif

    uint64_t bigbuffer_out_i = 0;
    uint8_t* out_bigbuffer = malloc(OUT_BIGBUFFER_SIZE);
    if (!out_bigbuffer) {
        log_perror("out_bigbuffer malloc failed");
        exit(EXIT_FAILURE);
    }

    struct regs regs;

#ifdef JUST_SEQ_NUM
    struct input {
        uint64_t seq_num;
        uint16_t n;
        uint8_t seq_len;
        uint8_t full_seq;
    } __attribute__((packed));
#elifdef WITH_REGS
    struct input {
        uint8_t  reg_select_gp[sizeof(regs.gp)/sizeof(regs.gp.x1)];
    #ifdef FLOATS
        #if defined(__riscv)
        uint8_t  reg_select_fp[sizeof(regs.fp)/sizeof(regs.fp.f0)];
        #elif defined(__aarch64__)
        uint8_t  reg_select_fp[sizeof(regs.fp)/sizeof(regs.fp.d0)];
        #endif
    #endif /* FLOATS */
    #ifdef VECTOR
        uint8_t  reg_select_vec[VEC_REG_SIZE/8*sizeof(regs.vec)/sizeof(regs.vec.v0)];
    #endif /* VECTOR */
        uint8_t n_instrs;
        uint8_t full_seq;
        uint32_t instr_seq[1] __attribute__((aligned(4)));
    } __attribute__((packed));
#elifdef WITH_FULL_REGS
    struct input {
        struct regs regs;
        uint8_t n_instrs;
        uint8_t full_seq;
        uint32_t instr_seq[1] __attribute__((aligned(4)));
    } __attribute__((packed));
#else
    struct input {
        uint8_t n_instrs;
        uint8_t full_seq;
        uint32_t instr_seq[1] __attribute__((aligned(4)));
    } __attribute__((packed));
#endif

    struct input* inputs_buf;
#ifdef JUST_SEQ_NUM
    unsigned max_input_size = sizeof(*inputs_buf);
#else
    unsigned max_input_size = sizeof(*inputs_buf)+(MAX_SEQ_LEN-1)*sizeof(*inputs_buf->instr_seq);
#endif
    inputs_buf = malloc(max_batch_n*max_input_size);

    // safety check, do this at the last end where memory could get allocated
    check_fuzzing_values_safe();

    init_comp(&connection, OUT_BIGBUFFER_SIZE, max_batch_n*max_input_size);

    while (true) {
        uint32_t* instr_seq;
        uint8_t  seq_len;

        unsigned n;
        recv_msg_n(&connection, (uint8_t*)&n, sizeof(n));
        if (n > max_batch_n) {
            log_error("n > max_batch_n");
            exit(EXIT_FAILURE);
        }

        #ifdef COMPRESS_RECV
        recv_msg_compressed(&connection, (uint8_t*)inputs_buf, max_input_size*n);
        #else
        recv_msg(&connection, (uint8_t*)inputs_buf, max_input_size*n);
        #endif

        struct input* input = inputs_buf;
        for (unsigned b = 0; b < n; b++) {
#ifdef JUST_SEQ_NUM
            for (unsigned a = 0; a < input->n; a++) {
                uint32_t _instr_seq[MAX_SEQ_LEN];
                generate_input(&generator, input->seq_num+a, (uint32_t*)&_instr_seq, input->seq_len, &regs);

                instr_seq = (uint32_t*)&_instr_seq;
                seq_len = input->seq_len;

            // TODO: to which value are regs set when not defined?
#elifdef WITH_REGS
            for (unsigned i = 0; i < sizeof(input->reg_select_gp)/sizeof(*input->reg_select_gp); i++) {
                ((reg*) &regs.gp)[i] = fuzzing_value_map_gp[input->reg_select_gp[i]];
            }
  #ifdef FLOATS
            for (unsigned i = 0; i < sizeof(input->reg_select_fp)/sizeof(*input->reg_select_fp); i++) {
                ((reg*) &regs.fp)[i] = fuzzing_value_map_fp[input->reg_select_fp[i]].u;
            }
  #endif /* FLOATS */
    #ifdef VECTOR
            for (unsigned i = 0; i < 32; i++) {
                for (unsigned j = 0; j < VEC_REG_SIZE/8; j++) {
                    ((uint64_t*)&((vv*)&regs.vec)[i])[j] = fuzzing_value_map_gp[input->reg_select_vec[i*VEC_REG_SIZE/8+j]];
                }
            }
    #endif /* VECTOR */
#elifdef WITH_FULL_REGS
            memcpy(&regs, &input->regs, sizeof(regs));
#else
            static_assert(sizeof(regs) % sizeof(filler_64) == 0, "regs should have the size of the filler value");
            for (unsigned i = 0; i < sizeof(regs)/sizeof(filler_64); i++) {
                ((uint64_t*)&regs)[i] = filler_64;
            }
#endif

#ifndef WITH_FULL_REGS
    #if defined(__riscv)
        #if defined(FLOATS)
            // We zero it out in the runner
            regs.fcsr = 0;
        #endif
    #elif defined(__aarch64__)
            // We zero it out in the runner
            regs.pstate = 0;
        #if defined(FLOATS) || defined(VECTOR)
            // We zero it out in the runner
            regs.fpsr = 0;
        #endif /* FLOATS */
    #endif
#endif

#ifndef JUST_SEQ_NUM
            instr_seq = input->instr_seq;
            seq_len = input->n_instrs;
#endif

            #ifdef VERBOSE
            log_info("Running instr seq:");
            for (unsigned i = 0; i<seq_len; i++) {
                log_info_noprefix(" 0x%08x", instr_seq[i]);
            }
            log_info_noprefix("");
            #endif

            if (input->full_seq) {
                struct result results[MAX_SEQ_LEN];
                unsigned stopped_at = run_with_instr_seq_full_seq(instr_seq, seq_len, 0, 0, (struct result*)&results, &regs);

#ifdef INTRODUCE_RANDOM_DIFFS
                if (rand() % 1000 == 0) {
                    results[stopped_at-1].regs_result.gp.x11++;
                }
#endif

                #ifdef VERBOSE
                log_info("Finished instr seq; full seq");
                /* print_result(&regs, &r); */
                #endif

                bigbuffer_out_i += pack_results(&out_bigbuffer[bigbuffer_out_i], (struct result*)results, stopped_at, &regs);
            } else {
                struct result r = run_with_instr_seq(instr_seq, seq_len, 0, 0, &regs);

#ifdef INTRODUCE_RANDOM_DIFFS
                if (rand() % 1000 == 0) {
                    r.regs_result.gp.x11++;
                }
#endif

                #ifdef VERBOSE
                log_info("Finished instr seq");
                print_result(&regs, &r);
                #endif

                bigbuffer_out_i += pack_result(&out_bigbuffer[bigbuffer_out_i], &r, &regs);
            }
            assert(bigbuffer_out_i < OUT_BIGBUFFER_SIZE);

#ifndef JUST_SEQ_NUM
            input = (struct input*) (((uint64_t) input)+sizeof(*input)+(input->n_instrs-1)*sizeof(*input->instr_seq));
#else
            }
            input = (struct input*) (((uint64_t) input)+sizeof(*input));
#endif
        }

        send_msg_compressed(&connection, out_bigbuffer, bigbuffer_out_i);
        bigbuffer_out_i = 0;
    }
}

/*****************************************************************************/

int set_and_get_sve(int set) {
    int ret = prctl(PR_SVE_SET_VL, set);
    int verify = prctl(PR_SVE_GET_VL, 0);
    assert(ret == verify);
    return verify;
}

int set_and_get_sme(int set) {
    int ret = prctl(PR_SME_SET_VL, set);
    int verify = prctl(PR_SME_GET_VL, 0);
    assert(ret == verify);
    return verify;
}

__attribute__((noreturn)) void fuzzer();
int main(int argc, char* argv[]) {
    early_init();

    /* https://askubuntu.com/a/1355819 */
    const int old_personality = personality(ADDR_NO_RANDOMIZE);
    if (!(old_personality & ADDR_NO_RANDOMIZE)) {
        const int new_personality = personality(ADDR_NO_RANDOMIZE);
        if (new_personality & ADDR_NO_RANDOMIZE) {
            log_info("Disabled ASLR with personality syscall.");
            execv(argv[0], argv);
        } else {
            log_error("Failed to disable ASLR with personality syscall.");
            log_info_noprefix("Try to disable system wide:");
            log_info_noprefix("sudo sysctl kernel.randomize_va_space=0");
            log_info_noprefix("or");
            log_info_noprefix("echo 0 | sudo tee /proc/sys/kernel/randomize_va_space");
            exit(1);
        }
    }

#ifdef INTRODUCE_RANDOM_DIFFS
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned int _seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ getpid());
    srand(_seed);
#endif

    // Arg parsing
    if (argc < 3) {
        log_error("Usage: %s ip port [hostname]", argv[0]);
        exit(EXIT_FAILURE);
    }
    strcpy(arguments.connect_ip, argv[1]);
    arguments.connect_port = strtoul(argv[2], NULL, 10);
    if (argc > 3) {
        strcpy(arguments.hostname, argv[3]);
    }

    #define HOSTNAME_SIZE 256
    hostname = malloc(HOSTNAME_SIZE);
    detect_preferred_hostname(hostname, HOSTNAME_SIZE, arguments.hostname[0] ? (const char*)arguments.hostname : NULL);
    log_info("Hostname: %s", hostname);

    // Collect Android-related properties
    char *serialno = malloc(256);
    get_android_property("ro.serialno", serialno, 256);
    ADD_TAG("android_serialno", serialno);

    char *model_name = malloc(256);
    get_android_property("ro.product.model", model_name, 256);
    ADD_TAG("android_model", model_name);

    char *product_name = malloc(256);
    get_android_property("ro.product.name", product_name, 256);
    ADD_TAG("android_product", product_name);

    lscpu = get_lscpu();
    proc_cpuinfo = read_file("/proc/cpuinfo");
    sys_possible = read_file("/sys/devices/system/cpu/possible");

#if defined(__aarch64__)
    unsigned long hwcaps = getauxval(AT_HWCAP);

    // The vector size of SIMD
    #define AARCH64_VEC_SIZE (128/8)

    if (hwcaps & HWCAP_SVE) {
        log_info("SVE available.");
                                       // architectural maximum of 2048-bit
        sve_max_size = set_and_get_sve(2048/8);
        assert(sve_max_size != 0);

        // Set the actual sve vector size to match SIMD
        vec_size = AARCH64_VEC_SIZE;
        assert(sve_max_size >= vec_size);
        int verify = set_and_get_sve(vec_size);
        assert(verify == vec_size);
    }
    if (set_and_get_sme(2048/8) != -1) {
        log_info("SME available.");
                                       // architectural maximum of 2048-bit
        sme_max_size = set_and_get_sme(2048/8);
        assert(sme_max_size != 0);
        // also SVE -> SME
        assert(sve_max_size == sme_max_size);

        // Try to set the actual sme vector size to match SIMD
        assert(sme_max_size >= vec_size);
        int verify = set_and_get_sme(vec_size);
        // But sme seems to always be maximum (TODO: maybe a QEMU bug)
        assert(verify == sme_max_size);
    }

    if (hwcaps & HWCAP_ASIMD) {
        log_info("SIMD available.");
        vec_size = AARCH64_VEC_SIZE;
    }
#endif

#ifndef SINGLE_THREAD
    // Detach the session. This is needed whenever the client is started
    // detached. Not sure if this code is perfect but it worked.
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        log_perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Infinitely wait, so Android app does not die
        while (1) {
            sleep(10000000);
        }
    }
    // Make child process the session leader
    if (setsid() < 0) {
        log_perror("setsid");
        exit(EXIT_FAILURE);
    }

    num_cpus = get_num_cpus();
    if (strncmp(hostname, "qemu", 4) == 0) {
        if (num_cpus >= 8) {
            // Restrict to 8 cores on qemu
            num_cpus = 8;
        }
    }
    log_info("Spawning %d threads...", num_cpus);
    for (core = 0; core < num_cpus; core++) {
        if (fork() == 0) {
            goto thread_setup;
        }
    }
    // quit parent
    exit(0);
thread_setup:
#else
    core = 0;
    num_cpus = 1;
#endif /* SINGLE_THREAD */

    pin_to_cpu(core);

    clean_memory_mappings();
    switch_stack(STACK_ADDR, &fuzzer);
}

__attribute__((noreturn)) void fuzzer() {
    runner_init();

    start_client();

    exit(0);
}
