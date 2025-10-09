extern const char git_commit[];

#define _GNU_SOURCE
// strsignal
#include <string.h>
// itimer
#include <time.h>
// sigset
#include <signal.h>
#undef _GNU_SOURCE

#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>

#include "runner.h"
#include "util.h"
#include "log.h"
#include "hexdiff.h"
#include "maps.h"
#include "rng.h"
#include "fuzzing_value_map.h"
#include "disasm_opcodes.h"
// Early init needs heap base and CPU info helpers
#include "musl_heap_base.h"

#include <sys/auxv.h>
#include <asm/hwcap.h>

int global_signum;
uint64_t global_si_addr = 0;
uint64_t global_si_pc = 0;
int global_si_code = 0;

static int __vec_size_cached = -1;
int get_vec_size(void) {
    if (__vec_size_cached >= 0) return __vec_size_cached;
#if defined(__riscv)
  #if defined(VECTOR)
    __vec_size_cached = VEC_REG_SIZE;
  #else
    __vec_size_cached = 0;
  #endif
#elif defined(__aarch64__)
    unsigned long hwcaps = getauxval(AT_HWCAP);
    if (hwcaps & HWCAP_ASIMD) {
        __vec_size_cached = 16; // 128-bit SIMD registers
    } else {
        __vec_size_cached = 0;
    }
#else
    __vec_size_cached = 0;
#endif
    return __vec_size_cached;
}

// Shared helper to format the vector-size suffix for logs.
// Produces an empty string when vectors are not enabled.
void build_vec_suffix(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
#ifdef VECTOR
    snprintf(out, out_size, " VEC=%d", VEC_REG_SIZE);
#endif
}

// Forward declarations for local formatted printers used before definition
static void fprint_pref(FILE* f, const char* prefix, const char* fmt, ...);
static inline void fprint_aligned_label(FILE* f, const char* prefix, const char* label_with_colon);

// YAML helpers for client/device info blocks
static void log_microarchitecture_block(FILE* file, const char* ind, const cpu_info* cpu, int item) {
    if (!file || !cpu) return;
    // Nested indent within the microarchitecture mapping
    char nested[256];
    snprintf(nested, sizeof(nested), "%s  ", ind);

    char* vend = midr_to_vendor(cpu->midr);
    char* micro = midr_to_microarch(cpu->midr);
    fprintf(file, "%s%svendor: %s\n", ind, item ? "- " : "  ", vend ? vend : "unknown");
    fprintf(file, "%smodel_name: %s\n", nested, micro ? micro : "unknown");
    fprintf(file, "%smidr: 0x%x\n", nested, cpu->midr);
    fprintf(file, "%simplementer: 0x%x\n", nested, cpu->implementer);
    fprintf(file, "%spart: 0x%x\n", nested, cpu->part);
    fprintf(file, "%svariant: 0x%x\n", nested, cpu->variant);
    fprintf(file, "%srevision: 0x%x\n", nested, cpu->revision);
    fprintf(file, "%sflags: []\n", nested);
}

static void log_device_block(FILE* file, const char* ind) {
    if (!file) return;

    // Hostname first (preferred/Android-aware); first line starts at current cursor
    char hostname[256] = {0};
    detect_preferred_hostname(hostname, sizeof(hostname), NULL);
    fprintf(file, "hostname: %s\n", hostname[0] ? hostname : "unknown");

    // Device/runtime fields (decimal ints)
    int total_cpus = get_num_cpus();
    int vec = get_vec_size();
    fprintf(file, "%snum_cpus: %d\n", ind, total_cpus);
    // TODO: implement and then lo8
    /* fprintf(file, "%snum_cores: %d\n", ind, 1); */
    /* fprintf(file, "%snum_sockets: %d\n", ind, 1); */
    /* fprintf(file, "%sthreads_per_core: %d\n", ind, 1); */
    if (vec > 0) {
        fprintf(file, "%svec_size: %d\n", ind, vec);
    }

    // Android metadata (optional; omit if empty)
    char product[256] = {0}, model[256] = {0}, serialno[256] = {0};
    if (get_android_property("ro.product.name", product, sizeof(product)) > 0)
        fprintf(file, "%sandroid_product: %s\n", ind, product);
    if (get_android_property("ro.product.model", model, sizeof(model)) > 0)
        fprintf(file, "%sandroid_model: %s\n", ind, model);
    if (get_android_property("ro.serialno", serialno, sizeof(serialno)) > 0)
        fprintf(file, "%sandroid_serialno: %s\n", ind, serialno);
}

// TODO: think of moving these to a fixed location
// and then using li and shift to get address
// currently it seems like auipc with store can kill us and write into
// stored regs
// also think of having one set of saved registers we restore after running
// but that would need to tell the compiler to push everything on the stack before we go into runner
// but that should work with globbers all
struct regs __regs_before;
struct regs __regs_result;
struct regs __regs_restore;
#ifdef META
struct meta meta_pre;
struct meta meta_post;
#endif

int runner_running = 0;
int runner_tests_running = 1;
int exception_stack_count = 0;
int signal_handled = 0;

timer_t timerid;
struct itimerspec alarm_its = {
                        // After how many ms to interrupt
    .it_value.tv_nsec = 20*1000000
};
struct itimerspec alarm_pause_its = {0};

#if defined(GDB)
#define SET_ALARM() ;
#define UNSET_ALARM() ;
#else
#define SET_ALARM() timer_settime(timerid, 0, &alarm_its, NULL)
#define UNSET_ALARM() timer_settime(timerid, 0, &alarm_pause_its, NULL)
#endif

void runner();
void runner_code_start();
void runner_code_end();

uint32_t* runner_code_start_rw;

struct sigaction sa_catch_all;
struct sigaction sa_ignore;
sigset_t blocked_sigs_during_handling;
sigset_t all_signals;
void sig_handler(int signum, siginfo_t *info, void *context) {
    INCLUDE_REG_MACROS();

    #if defined(__riscv)
    // Restore thread pointer
    asm volatile(
        "la x29, __regs_restore\n\t"
        "LD_TP"
    ::: "x29");
    #endif

    exception_stack_count++;

    mcontext_t* mcontext = &((ucontext_t *)context)->uc_mcontext;

    // Copy over pc
    #if defined(__riscv)
    global_si_pc = mcontext->__gregs[0];
    #elif defined(__aarch64__)
    global_si_pc = mcontext->pc;
    #endif

    if (!runner_running || exception_stack_count > 1) {
        printf("\n");
        print_proc_self_maps();
        printf("\n");
        printf("Catched a signal that doesn't come from the instr seq:\n");
        printf("%d, %s, si_addr: %p, si_pc: %lx\n", signum, strsignal(signum), info->si_addr, global_si_pc);
        printf("runner_running: %d, exception_stack_count: %d\n", runner_running, exception_stack_count);
        printf("You most likely used the client compiled with VECTOR support on a non-vector machine. Or reading meta instructions are sigilling. Disassemble the client and check the si_addr or si_pc.\n");
        exit(EXIT_FAILURE);
        // TODO: reraise here
        /* signal(signum, SIG_DFL); */
        /* raise(signum); */
    }

    #if defined(__riscv)
    // Not sure why this is only needed on riscv. Probably a kernel diff.
    // NOTE: this changes meta but otherwise we can't debug
    // we could make this VERBOSE only
    sigprocmask(SIG_UNBLOCK, &all_signals, NULL);
    #endif

    #ifdef META
    SAVE_META(meta_post);
    #endif

    if (signum != SIGALRM) {
        UNSET_ALARM();
    }

    #if defined(__riscv) && defined(VECTOR)
    // Vector state is not saved by kernel
    SAVE_STATE_VECTOR(__regs_result);
    #endif

    global_signum = signum;
    global_si_addr = (uint64_t)info->si_addr;
    global_si_code = (uint64_t)info->si_code;

    // Copy over gp and fp state captured by kernel
    #if defined(__riscv)
    memcpy(&__regs_result.gp, &mcontext->__gregs[1], sizeof(__regs_result.gp));
    #ifdef FLOATS
    memcpy(&__regs_result.fp, &mcontext->__fpregs.__d.__f, sizeof(__regs_result.fp));
    __regs_result.fcsr = mcontext->__fpregs.__d.__fcsr;
    #endif
    #elif defined(__aarch64__)
    memcpy(&__regs_result.gp.x0, &mcontext->regs, sizeof(__regs_result.gp)-sizeof(__regs_result.gp.sp));
    memcpy(&__regs_result.gp.sp, &mcontext->sp, sizeof(__regs_result.gp.sp));
    // On apple, bit 0x1000 is always set here
    // Seems to be related to SPSR_EL2, Saved Program Status Register
    // Just zero it out
    // TODO(now): pstate
    __regs_result.pstate = mcontext->pstate & ~0x1000 & ~0x1000000;

    #if defined(FLOATS) || defined(VECTOR)
    __regs_result.fpsr = ((freg*)mcontext->__reserved)[1];
    #endif

    #ifdef FLOATS
    // TODO: doest his vary between kernels?
    // is there a better way?
    for (unsigned i = 0; i < sizeof(__regs_result.fp)/sizeof(__regs_result.fp.d0); i++) {
        ((freg*)&__regs_result.fp)[i] = ((freg*)mcontext->__reserved)[2+2*i];
    }
    #endif

    #ifdef VECTOR
    memcpy(&__regs_result.vec, &((vv*)mcontext->__reserved)[1], sizeof(__regs_result.vec));
    #endif

    // TODO: what are the first 8 bytes?
    /* print_hexbuf(&mcontext->__reserved, 1024); */
    #endif

    // TODO: refactor this change in the UI?
    // maybe rename to pc_offset?
    // TODO: ok, this isnt great. We want the absolute num.
    // Maybe report at start to server where beginning is?
    // we want both versions in optimal case
    /* global_si_pc -= (uint64_t)&runner_code_start; */

    #ifdef VERBOSE
    printf("child signal handler: %d, %s, si_addr: %lx, si_pc: %lx\n", signum, strsignal(signum), global_si_addr, global_si_pc);
    #endif

    // Discard potentially thrown alarm
    if (sigaction(SIGALRM, &sa_ignore, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGALRM, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }

    // Unblock
    sigprocmask(SIG_UNBLOCK, &blocked_sigs_during_handling, NULL);

    signal_handled = 1;
    asm volatile(
        "RESTORE_STATE __regs_restore\n\t"
        "ret");
}

// NOTE: this is set in main
static void* runner_code_start_page_aligned;

/*****************************************************************************/

void clear_cache_wrapper(void* start, void* end) {
    #if defined(__riscv)
    (void)start;
    (void)end;
    // NOTE: can't use __builtin___clear_cache because it uses vdso
    asm volatile("fence.i");
    #elif defined(__aarch64__)
    __builtin___clear_cache (start, end);
    #endif
}

void modify_code(const uint32_t* instr_seq, unsigned n) {
    memcpy(runner_code_start_rw, instr_seq, sizeof(*instr_seq)*n);
    // Reset the remaining instructions to a nop
    for (int i = n; i < MAX_SEQ_LEN; i++) {
        runner_code_start_rw[i] = NOP_32;
    }

    clear_cache_wrapper(runner_code_start, runner_code_end);
}

uint64_t instret_base_no_signal, instret_base_signal;
struct result run_with_instr_seq_no_check_mem(const uint32_t* instr, unsigned n, struct mapping* mappings, unsigned mappings_n, struct regs* regs_before) {
#if defined(__riscv)
    asm volatile("nop" ::: REGS_FP, REGS_VECTOR);
    uint32_t fcsr;
    asm volatile ("frcsr %0" : "=r"(fcsr));
    #elif defined(__aarch64__)
    // Make sure that fp and vec regs are saved by the compiler because we clobber those
    asm volatile("nop" ::: REGS_FP, REGS_VECTOR);
    uint32_t fpsr;
    asm volatile("mrs %0, fpsr" : "=r"(fpsr));
    #endif

    if (n > MAX_SEQ_LEN) {
        log_error("Max sequence length supported is %d. You tried %d. Increase the maximum length.", MAX_SEQ_LEN, n);
        exit(1);
    }

    memcpy(&__regs_before, regs_before, sizeof(__regs_before));

    modify_code(instr, n);
    load_memory_mappings(mappings, mappings_n);

    signal_handled = 0;
    runner_running = 1;
    exception_stack_count = 0;
    SET_ALARM();
    runner();
    if (!signal_handled) {
        UNSET_ALARM();
        global_signum = 0;
        global_si_pc = 0;
        global_si_addr = 0;
        global_si_code = 0;

        // Ignore changes to the scrap register as it is used
        // to store the state.
    #if defined(__riscv)
        __regs_result.gp.x29 = regs_before->gp.x29;
    #elif defined(__aarch64__)
        __regs_result.gp.x29 = regs_before->gp.x29;
    #endif
    }
    runner_running = 0;

    struct result r = {
            .signum = global_signum,
            .si_addrr = global_si_addr,
            .si_code = global_si_code,
            .si_pc = global_si_pc,
            .instr_idx = 0,
            #ifdef META
            .meta = {
                .cycle = meta_post.cycle-meta_pre.cycle,
                #if defined(__riscv)
                .instret = meta_post.instret-meta_pre.instret,
                #else
                .instret = 0,
                #endif
            },
            #endif
            #ifdef CHECK_MEM
            .n_mem_changes = 0,
            #endif
    };
    memcpy(&r.regs_result, &__regs_result, sizeof(__regs_result));

    // Derive 1-based faulting instruction index if PC points into runner code
    if (r.signum != 0 && r.si_pc != 0) {
        uintptr_t start = (uintptr_t)runner_code_start;
        uintptr_t end   = (uintptr_t)runner_code_end;
        uintptr_t pc    = (uintptr_t)r.si_pc;
        if (pc >= start && pc < end) {
            r.instr_idx = (uint32_t)(((pc - start) / sizeof(uint32_t)) + 1);
        } else {
            r.instr_idx = 0;
        }
    }

    // Subtract baseline instret overhead (measured in runner_init)
    #ifdef META
    {
        uint64_t base = (r.signum == 0) ? instret_base_no_signal : instret_base_signal;
        // Ensure we never underflow; if it does, something is off with baselines
        assert(r.meta.instret >= base);
        r.meta.instret -= base;
    }
    #endif

    // Restore non-clobbering regs
    #if defined(__riscv)
    asm volatile ("fscsr %0" :: "r"(fcsr));
    #elif defined(__aarch64__)
    asm volatile("msr fpsr, %0" :: "r"(fpsr));
    #endif

    return r;
}

struct result run_with_instr_seq_no_check_mem_extra_mappings(const uint32_t* instr, unsigned n, struct mapping* mappings, unsigned mappings_n, struct mapping* extra_mappings, unsigned extra_mappings_n, struct regs* regs_before) {
    load_memory_mappings(extra_mappings, extra_mappings_n);
    return run_with_instr_seq_no_check_mem(instr, n, mappings, mappings_n, regs_before);
}

#ifdef CHECK_MEM
int gen_random_prot(uint64_t target) {
    int prot = 0;

    shared_rng rng;
    rng_init(&rng, target);
    int x = rng_randint(&rng, 0, 7);

    // We don't randomize prot completely on ARM
    // because otherwise we get a lot of false positives
    // because of loads or stores cross non-accessible
    // page boundaries.
    // RISC-V kernels don't like WX.
    prot |= PROT_READ;
    if (x & 0x2) {
        prot |= PROT_WRITE;
    }
    if (x & 0x4) {
        prot |= PROT_EXEC;
    }

    return prot;
}
#endif

struct result run_with_instr_seq(const uint32_t* instr, unsigned n, struct mapping* mappings, unsigned mappings_n, struct regs* regs_before) {
    struct result r = run_with_instr_seq_no_check_mem(instr, n, mappings, mappings_n, regs_before);

#ifdef CHECK_MEM
    struct mapping auto_mappings[CHECK_MEM_MAX_TRIES];
    unsigned auto_mappings_n=0;

  #ifdef AUTO_MAP_MEM
    // Try to map pages until there is no segfault or max tries is reached
    if (r.signum == SIGSEGV || r.signum == SIGBUS) {
        struct result tmp_result = r;
        do {
            uint64_t target = (tmp_result.si_addrr & ~(page_size-1));

            // TODO(now): I think high addresses are also an issue because we cant map those
            // like that we have a lot of segfaults
            // can we fix that? maybe provide more lower pointers?
            #ifdef VERBOSE
            printf("trying to map at: %lx\n", target);
            #endif

            // Apple M2 and some RISC-V cores/kernels don't like mapping there
            if (target < 0x10000) {
                #ifdef VERBOSE
                printf("not mapping because too low\n");
                #endif
                break;
            }

            // Non-apple and some RISC-V devices dont like bigger vaddresses
            if (target >= 1ul<<38) {
                #ifdef VERBOSE
                printf("not mapping because too big\n");
                #endif
                break;
            }

            if (check_page_mapped((void*)target)) {
                #ifdef VERBOSE
                printf("msync failed\n");
                print_proc_self_maps();
                #endif
                break;
            }

            // NOTE: If we get weird errors here at some point because
            // mappings are not possible add failable logic into setup_memory_mapping.
            // Before we had mmap and if it failed we breaked here.

            struct mapping new_mapping;

            new_mapping.start = target;
            new_mapping.n = page_size;
        #ifndef AUTO_MAP_MEM_RW_PROT
            new_mapping.prot = gen_random_prot(target);
            #ifdef VERBOSE
            printf("Using random PROT: %x\n", new_mapping.prot);
            #endif
        #else
            new_mapping.prot = PROT_WRITE|PROT_READ;
        #endif
            // So its auto malloced
            new_mapping.val = 0;
            setup_memory_mapping(&new_mapping);

            // Fill page with random content
            shared_rng tmp_rng;
            rng_init(&tmp_rng, target);
            for (unsigned a=0; a<new_mapping.n/sizeof(uint64_t); a++) {
                ((uint64_t*)new_mapping.val)[a] = fuzzing_value_any_val(&tmp_rng);
            }

            int insert_at = auto_mappings_n;

            // NOTE: this code is commented out because we actually want to
            // see non-determinism in the emmited exceptions/mappings
            /* // Keep the regions sorted */
            /* while (insert_at > 0) { */
            /*     if (auto_mappings[insert_at-1].start < target) { */
            /*         break; */
            /*     } */
            /*     auto_mappings[insert_at] = auto_mappings[insert_at-1]; */
            /*     insert_at--; */
            /* } */

            auto_mappings[insert_at] = new_mapping;
            auto_mappings_n++;

            tmp_result = run_with_instr_seq_no_check_mem_extra_mappings(instr, n, mappings, mappings_n, auto_mappings, auto_mappings_n, regs_before);
        } while ((tmp_result.signum == SIGSEGV || tmp_result.signum == SIGBUS) && auto_mappings_n < CHECK_MEM_MAX_TRIES);
    }
#endif /* AUTO_MAP_MEM */

    // Run the sequence one more time
    r = run_with_instr_seq_no_check_mem_extra_mappings(instr, n, mappings, mappings_n, auto_mappings, auto_mappings_n, regs_before);
    // TODO: use linked list or something like that instead?
    // TODO: move the array to the struct?
    // we tested it. Seemed to be a bit slower (19.5k vs 20.5k instr/s). Probably because tmp_results is
    // is copied once above. So for now stay with malloc

    // TODO: rewrite to use 8-byte chunks?

    // Check memory mappings for changes
    int end_logging = 0;
    int range_started = 0;
    uint64_t range_start = 0;
    struct mapping* mapping = 0;
    uint8_t  cur_val[CHECK_MEM_CUT_AT];
    uint32_t cur_range_len = 0;
    // FNV-1a 32-bit checksum accumulator for the full changed range
    uint32_t cur_checksum = 0;
    for (unsigned m = 0; m < mappings_n+auto_mappings_n; m++) {
        if (end_logging) {
            break;
        }
        if (m < mappings_n) {
            mapping = &mappings[m];
        } else {
            mapping = &auto_mappings[m-mappings_n];
        }
        for (unsigned i=0; i<mapping->n; ) {
            // Fast-skip equal spans
            if (!range_started) {
                size_t eq = memcmp_common_prefix(mapping->rw_mapping + i, mapping->val + i, mapping->n - i);
                i += eq;
                if (i >= mapping->n) break;
            }
            // Now at a differing byte or continuing a diff range
            if (!range_started) {
                range_started = 1;
                range_start = mapping->start + i;
                cur_range_len = 0;
                // init FNV-1a 32-bit offset basis
                cur_checksum = 2166136261u;
            }
            // Consume differing bytes
            while (i < mapping->n && mapping->rw_mapping[i] != mapping->val[i]) {
                if (cur_range_len < CHECK_MEM_CUT_AT) {
                    cur_val[cur_range_len] = mapping->rw_mapping[i];
                }
                cur_range_len++;
                cur_checksum ^= mapping->rw_mapping[i];
                cur_checksum *= 16777619u;
                i++;
            }
            // End of diff range or end of mapping: potentially flush the range.
            // However, if we are exactly at the end of the current mapping and
            // the next mapping starts immediately after and continues the diff,
            // do NOT flush yet and continue the range across mappings.
            if (range_started && (i >= mapping->n || mapping->rw_mapping[i] == mapping->val[i])) {
                int should_flush = 1;
                if (i >= mapping->n) {
                    // We are at the boundary of the current mapping. Check next mapping.
                    unsigned total_mappings = mappings_n + auto_mappings_n;
                    if ((m + 1) < total_mappings) {
                        struct mapping* next_mapping;
                        if ((m + 1) < mappings_n) {
                            next_mapping = &mappings[m + 1];
                        } else {
                            next_mapping = &auto_mappings[m + 1 - mappings_n];
                        }
                        // Continue range if next mapping is exactly adjacent and starts with a differing byte
                        if (next_mapping->start == (mapping->start + mapping->n) && next_mapping->n > 0) {
                            if (next_mapping->rw_mapping[0] != next_mapping->val[0]) {
                                // Keep the current range open and continue in next mapping
                                should_flush = 0;
                                // Break out of the inner loop to advance to the next mapping
                                // with range_started still set.
                                break;
                            }
                        }
                    }
                }

                if (should_flush) {
                    struct mem_change mem_change = {
                        .start = range_start,
                        .n = cur_range_len,
                        .checksum = cur_checksum,
                    };
                    memcpy(mem_change.val, cur_val, sizeof(mem_change.val));
                    assert(r.n_mem_changes < CHECK_MEM_MAX_NUMBER_MEM_CHANGES);
                    r.mem_changes[r.n_mem_changes++] = mem_change;
                    range_started = 0;

                    if (r.n_mem_changes >= CHECK_MEM_MAX_NUMBER_MEM_CHANGES) {
                        end_logging = 1;
                        break;
                    }
                }
            }
        }
    }
    if (range_started && !end_logging) {
        struct mem_change mem_change = {
            .start = range_start,
            /* .n = (mapping->start+mapping->n)-range_start, */
            .n = cur_range_len,
            .checksum = cur_checksum,
        };
        memcpy(mem_change.val, cur_val, sizeof(mem_change.val));
        assert(r.n_mem_changes < CHECK_MEM_MAX_NUMBER_MEM_CHANGES);
        r.mem_changes[r.n_mem_changes++] = mem_change;
        range_started = 0;
    }

    // Unmap the auto mappings
    free_memory_mappings(auto_mappings, auto_mappings_n);

#endif /* CHECK_MEM */
    return r;
}

unsigned run_with_instr_seq_full_seq(const uint32_t* instr, unsigned l, struct mapping* mappings, unsigned mappings_n, struct result* results, struct regs* regs_before) {
    for (unsigned n = 0; n < l; n++) {
        struct result r = run_with_instr_seq(instr, n+1, mappings, mappings_n, regs_before);
        results[n] = r;
        if (r.signum != 0) {
            // Don't break here because that will not increment.
            return n+1;
        }
    }
    // Full sequence ran.
    return l;
}

struct result run_with_instr(const uint32_t instr, struct regs* regs_before) {
    return run_with_instr_seq(&instr, 1, 0, 0, regs_before);
}

void clean_memory_mappings() {
    // Unmap vdso since that introduces diffs between kernels if we read from there
    // also comes from shifting, same as stack
    unmap_section("[vdso]");
    unmap_section("[vvar]");
    unmap_section("[vdso_data]");

    // Unmap weird qemu-only section
    unmap_section("---p");
}

void runner_init() {
    // Set up a duplicate memory mapping for the runner page
    struct mapping runner_mapping;
    runner_code_start_page_aligned = (void*)(((size_t)runner_code_start) & (~(page_size-1)));
    void* runner_code_end_page_aligned = (void*)(((size_t)runner_code_end + page_size + 1) & (~(page_size-1)));
    runner_mapping.start = (uint64_t)runner_code_start_page_aligned;
    runner_mapping.n = runner_code_end_page_aligned-runner_code_start_page_aligned;
    runner_mapping.prot = PROT_EXEC|PROT_READ;

    // Save the original code temporarily
    runner_mapping.val = malloc(runner_mapping.n);
    assert(runner_mapping.val != 0);
    memcpy(runner_mapping.val, runner_code_start_page_aligned, runner_mapping.n);
    int res = munmap(runner_code_start_page_aligned, runner_mapping.n);
    if (res != 0) {
        log_perror("Couldn't unmap runner code for remapping.");
        exit(EXIT_FAILURE);
    }

    // Setup the duplicated mapping and save it for later use
    setup_memory_mapping(&runner_mapping);
    runner_code_start_rw = (uint32_t*)(runner_mapping.rw_mapping+((void*)runner_code_start-runner_code_start_page_aligned));

    // Load the memory mapping with the original content and discard it
    load_memory_mapping(&runner_mapping);
    free(runner_mapping.val);
    clear_cache_wrapper(runner_code_start_page_aligned, runner_code_end_page_aligned);

    // make sure code is 4-byte aligned
    assert((uint64_t)runner_code_start % 4 == 0);

    stack_t ss;
    #define ALT_STACK_SIZE 16384
    assert(SIGSTKSZ <= ALT_STACK_SIZE);
    static_assert(ALT_STACK_SIZE >= page_size, "alt stack should have maximum of all page sizes");
    check_and_map_with_additional_flags((void*)ALT_STACK_ADDR, ALT_STACK_SIZE, PROT_READ|PROT_WRITE, MAP_STACK);
    ss.ss_sp = (void*)ALT_STACK_ADDR;
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        log_perror("sigaltstack");
        exit(EXIT_FAILURE);
    }

    // Block alarm in signal handler
    sigemptyset(&blocked_sigs_during_handling);
    sigaddset(&blocked_sigs_during_handling, SIGALRM);
    sa_catch_all.sa_mask = blocked_sigs_during_handling;

    sigemptyset(&all_signals);
    sigaddset(&all_signals, SIGALRM);
    sigaddset(&all_signals, SIGILL);
    sigaddset(&all_signals, SIGSEGV);
    sigaddset(&all_signals, SIGTRAP);
    sigaddset(&all_signals, SIGBUS);
    sigaddset(&all_signals, SIGSYS);
    sigaddset(&all_signals, SIGFPE);

    sa_catch_all.sa_sigaction = (void *)sig_handler;
    sa_catch_all.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;

    if (sigaction(SIGILL, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGSEGV, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGALRM, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGTRAP, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGBUS, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGSYS, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }
    if (sigaction(SIGFPE, &sa_catch_all, NULL) == -1) {
        log_perror("signal");
        exit(1);
    }

    // Setup ignore handler for alarm
    sa_ignore.sa_handler = SIG_IGN;
    sa_ignore.sa_flags = 0;
    sigemptyset(&sa_ignore.sa_mask);

    if (timer_create(CLOCK_PROCESS_CPUTIME_ID, NULL, &timerid) == -1) {
        log_perror("timer_create");
        exit(1);
    }

#ifdef META
    // Compute baseline instret counts: one for normal execution, one for SIGILL path
    {
        struct result br;
        // No-signal baseline: run a single NOP
        {
            struct regs _local_regs = {0};
            br = run_with_instr(NOP_32, &_local_regs);
        }
        assert(br.signum == 0);
        // Minus 1 because we actually executed one instruction
        if (br.meta.instret != 0) {
            instret_base_no_signal = br.meta.instret-1;
        } else {
            instret_base_no_signal = 0;
        }

        // Signal baseline: invalid instruction to trigger SIGILL handler path
        {
            struct regs _local_regs = {0};
            br = run_with_instr(INVALID_32, &_local_regs);
        }
        assert(br.signum == SIGILL);
        instret_base_signal = br.meta.instret;

        if (instret_base_signal == 0) {
            log_warning("Collecting instret does not seem to work on this machine. Was 0.");
        }
    }
#endif

#if defined(__riscv)
    assert(strcmp(get_abi_name(getregindex(gp.ra)), "ra") == 0);
    assert(strcmp(get_abi_name(getregindex(gp.t6)), "t6") == 0);
    assert(strcmp(get_abi_name(getregindex(gp.a5)), "a5") == 0);
  #ifdef FLOATS
    assert(strcmp(get_abi_name(getabiindex(fcsr)), "fcsr") == 0);
    assert(strcmp(get_abi_name(getabiindex_float(f0)), "ft0") == 0);
  #endif
#elif defined(__aarch64__)
    assert(strcmp(get_abi_name(getregindex(gp.x0)), "x0") == 0);
    assert(strcmp(get_abi_name(getregindex(gp.x8)), "x8") == 0);
    assert(strcmp(get_abi_name(getregindex(gp.x30)), "x30") == 0);
    assert(strcmp(get_abi_name(getabiindex(gp.sp)), "sp") == 0);
    assert(strcmp(get_abi_name(getabiindex(pstate)), "pstate") == 0);
  #if defined(FLOATS) || defined(VECTOR)
    assert(strcmp(get_abi_name(getabiindex(fpsr)), "fpsr") == 0);
    assert(strcmp(get_abi_name_float(getabiindex_float(d0)), "d0") == 0);
  #endif
#endif
  #ifdef VECTOR
    assert(strcmp(get_abi_name(getabiindex_vec(v0)), "v0") == 0);
  #endif

    for (unsigned with_exception=0; with_exception < 2; with_exception++) {
        struct result r;
        struct regs _t_regs = {0};

        #if MAX_SEQ_LEN < 2
        // We can't test exception with only 1 instruction since we use
        // an illegal instruction after the valid one to do that
        if (with_exception) {
            continue;
        }
        #endif

        // Test simple addition
        uint32_t instr;
        #if defined(__riscv)
        // addi t0, t0, 1
        instr = 0x00128293;
        #elif defined(__aarch64__)
        // add x0, x1, 1
        instr = 0x91000420;
        #endif

        #ifdef VERBOSE
        log_info("Running simple addition test case. With exception: %d", with_exception);
        #endif
        if (!with_exception) {
            r = run_with_instr(instr, &_t_regs);
            assert(r.signum == 0);
        } else {
            uint32_t seq[2] = {instr, 0};
            r = run_with_instr_seq((uint32_t*)&seq, 2, 0, 0, &_t_regs);
            assert(r.signum == SIGILL);
            // Validate faulting-instruction index and PC mapping when the trap
            // occurs inside runner_code_start..runner_code_end.
            // The sequence is [valid, invalid], so the second instruction
            // should fault and be reported as index 2 with PC at
            // runner_code_start + 4.
            {
                uintptr_t expected_pc = (uintptr_t)runner_code_start + sizeof(uint32_t);
                // instr_idx is 1-based; ensure it points at the invalid instr
                assert(r.instr_idx == 2);
                assert(r.si_pc == expected_pc);
            }
        }

        #if defined(__riscv)
        assert(_t_regs.gp.t0+1 == r.regs_result.gp.t0);
        assert(_t_regs.gp.sp   == r.regs_result.gp.sp);
        assert(_t_regs.gp.tp   == r.regs_result.gp.tp);
        assert(_t_regs.gp.ra   == r.regs_result.gp.ra);
        #else
        assert(_t_regs.gp.x1+1 == r.regs_result.gp.x0);
        assert(_t_regs.gp.sp   == r.regs_result.gp.sp);
        assert(_t_regs.gp.x30  == r.regs_result.gp.x30);
        #endif

#ifdef VECTOR
        #ifdef VERBOSE
        printf("Running simple vector load test case. With exception: %d\n", with_exception);
        #endif

        // Test simple vector load
        vv v0;
        ASSIGN_16(v0, 0x0000000080800000, 0x00000000000000ff);
    #if defined(__riscv)
        _t_regs.gp.a5 = (uint64_t)&v0;
        // vle v0, 0(a5)
        instr = 0x2078007;
    #elif defined(__aarch64__)
        _t_regs.gp.x1 = (uint64_t)&v0;
        // ld1 { v0.16b }, [x1]
        instr = 0x4c407020;
    #endif

        if (!with_exception) {
            r = run_with_instr(instr, &_t_regs);
            assert(r.signum == 0);
        } else {
            uint32_t seq[2] = {instr, 0};
            r = run_with_instr_seq((uint32_t*)&seq, 2, 0, 0, &_t_regs);
            assert(r.signum == SIGILL);
        }

        assert(memcmp(&v0, &r.regs_result.vec.v0, sizeof(v0)) == 0);
#endif /* VECTOR */
    }

#if MAX_SEQ_LEN >= 2
    #ifdef VERBOSE
    printf("Running full-seq scan test.\n");
    #endif
    // Validate run_with_instr_seq_full_seq stops at first trapping instruction
    {
        uint32_t seq[3] = { NOP_32, INVALID_32, NOP_32 };
        struct result results[3] = {0};
        struct regs _t_regs2 = {0};
        unsigned stop = run_with_instr_seq_full_seq(seq, 3, 0, 0, results, &_t_regs2);
        assert(stop == 2);
        assert(results[0].signum == 0);
        assert(results[1].signum != 0);
        // For the second run (two instr), the invalid instruction is at index 2
        assert(results[1].instr_idx == 2);
    }
#endif

#ifdef VERBOSE
    printf("Running result_equal stability test.\n");
#endif
    // Two identical runs of a single NOP should compare equal
    {
        struct regs _t_regs3 = {0};
        struct result a = run_with_instr(NOP_32, &_t_regs3);
        assert(a.signum == 0);
        struct regs _t_regs4 = {0};
        struct result b = run_with_instr(NOP_32, &_t_regs4);
        assert(b.signum == 0);
        assert(result_equal(&a, &b));
    }

#ifdef VERBOSE
    printf("Running result_equal negative test.\n");
#endif
    // A NOP result should not equal a result that modified registers
    {
        struct regs _t_regs5 = {0};
        struct result a = run_with_instr(NOP_32, &_t_regs5);
        assert(a.signum == 0);
        uint32_t add_instr;
#if defined(__riscv)
        // addi t0, t0, 1
        add_instr = 0x00128293;
#elif defined(__aarch64__)
        // add x0, x1, 1
        add_instr = 0x91000420;
#endif
        struct regs _t_regs6 = {0};
        struct result b = run_with_instr(add_instr, &_t_regs6);
        assert(b.signum == 0);
        assert(!result_equal(&a, &b));
    }

#ifdef VERBOSE
    printf("Running find_mapping tests.\n");
#endif
    // Validate find_mapping for base, interior, boundary and miss cases
    {
        struct mapping maps[3] = {
            {.start = 0x1000, .n = 0x100},
            {.start = 0x2000, .n = 0x080},
            {.start = 0x3000, .n = 0x100},
        };
        // Miss before first mapping
        assert(find_mapping(maps, 3, (uintptr_t)0x0) == NULL);
        // Base of first mapping
        assert(find_mapping(maps, 3, (uintptr_t)0x1000) == &maps[0]);
        // Interior of first mapping (last byte)
        assert(find_mapping(maps, 3, (uintptr_t)(0x1000 + 0x0FF)) == &maps[0]);
        // End boundary is exclusive
        assert(find_mapping(maps, 3, (uintptr_t)(0x1000 + 0x100)) == NULL);
        // Interior of second mapping
        assert(find_mapping(maps, 3, (uintptr_t)(0x2000 + 0x010)) == &maps[1]);
        // Last byte of third mapping
        assert(find_mapping(maps, 3, (uintptr_t)(0x3000 + 0x0FF)) == &maps[2]);
        // End boundary of third mapping
        assert(find_mapping(maps, 3, (uintptr_t)(0x3000 + 0x100)) == NULL);
    }

    struct regs _t_regs7 = {0};
    uint32_t instr;
#if defined(CHECK_MEM) && defined(AUTO_MAP_MEM)
    #ifdef VERBOSE
    printf("Running check mem test case 1.\n");
    #endif

    // Test page-boundary store
    #if defined(__riscv)
                    // NOTE: randomly picked so that the hashing function
                    // gives two writable mappings
    uint64_t base = 0x14567e0000;
    #elif defined(__aarch64__)
    uint64_t base = 0x567e0000;
    #endif
    uint64_t target = base + (page_size - 1);
    uint64_t val = 0x81221483d22ef611;
    #if defined(__riscv)
    // sd x1,   1(x2)
    instr = 0x1130a3;
    #elif defined(__aarch64__)
    // str x1,  [x2, #1]
    instr = 0xf8001041;
    #endif
    _t_regs7.gp.x1 = val;
    _t_regs7.gp.x2 = target;
    struct result r = run_with_instr(instr, &_t_regs7);
    assert(r.signum == 0);
    assert(r.n_mem_changes == 1);
    assert(r.mem_changes[0].start == target + 1);
    assert(r.mem_changes[0].n == 8);
    assert(*(uint64_t*)r.mem_changes[0].val == val);

    static_assert(CHECK_MEM_MAX_NUMBER_MEM_CHANGES < 1<<(8*sizeof(r.n_mem_changes)), "check mem counter should not overflow 1 byte");

    #ifdef VERBOSE
    printf("Running check mem test case 2.\n");
    #endif

    // Test page-end store
    #if defined(__riscv)
    // sd x1,   0(x2)
    instr = 0x113023;
    #elif defined(__aarch64__)
    // str x1,  [x2, #0]
    instr = 0xf9000041;
    #endif
    target = base + (page_size - 8);
    _t_regs7.gp.x1 = val;
    _t_regs7.gp.x2 = target;
    r = run_with_instr(instr, &_t_regs7);
    assert(r.signum == 0);
    assert(r.n_mem_changes == 1);
    assert(r.mem_changes[0].start == target);
    assert(r.mem_changes[0].n == 8);
    assert(*(uint64_t*)r.mem_changes[0].val == val);
#endif /* CHECK_MEM */

    // Verify disasm_opcodes exact output for known instructions
    {
        char *dis = NULL;

        // NOP should disassemble to exactly "nop" on all supported arches
        const char *expected_nop = "nop";
        dis = disasm_opcodes(NOP_32, 0);
        if (!dis || strcmp(dis, expected_nop) != 0) {
            printf("disasm_opcodes(NOP_32): '%s' expected: '%s'\n",
                   dis ? dis : "(null)", expected_nop);
            if (dis) free(dis);
            exit(EXIT_FAILURE);
        }
        free(dis);

#if defined(__riscv)
        // addi t0, t0, 1
        const char *expected_add = "addi\tt0,t0,1";
        instr = 0x00128293;
        dis = disasm_opcodes(instr, 0);
        if (!dis || strcmp(dis, expected_add) != 0) {
            printf("disasm_opcodes(addi t0,t0,1): '%s' expected: '%s'\n",
                   dis ? dis : "(null)", expected_add);
            if (dis) free(dis);
            exit(EXIT_FAILURE);
        }
        free(dis);
#elif defined(__aarch64__)
        // add x0, x1, #1
        const char *expected_add = "add\tx0, x1, #0x1";
        instr = 0x91000420;
        dis = disasm_opcodes(instr, 0);
        if (!dis || strcmp(dis, expected_add) != 0) {
            printf("disasm_opcodes(add x0, x1, #1): '%s' expected: '%s'\n",
                   dis ? dis : "(null)", expected_add);
            if (dis) free(dis);
            exit(EXIT_FAILURE);
        }
        free(dis);
#endif
    }

    #if defined(__riscv) && defined(VECTOR)
    // TODO: refactor for riscv and remove this here?
    // Read out maximum physical vector size from the hardware
    uint64_t max_vec_size;
    asm volatile (
        "vsetvli %0, x0, e8, m1"
        : "=r"(max_vec_size) ::
    );
    // We check this because this introduces many differences with indexed loads, even when the vector
    // reg size is set in the runner. Normal vector instructions are fine.
    // See: https://old.reddit.com/r/RISCV/comments/1fmn0o1/vector_indexed_loads_with_varying_vlen/
    // Uncomment the check if you still want to fuzz clients of varying vector reg size.
    if (max_vec_size != VEC_REG_SIZE) {
        printf("The vector size mismatches the one of the fuzzer:\nHardware: %ld (%ldB) Fuzzer %d (%dB)\nYou can ignore this warning by commenting out the check but read the comment.\n", max_vec_size*8, max_vec_size, VEC_REG_SIZE*8, VEC_REG_SIZE);
        exit(EXIT_FAILURE);
    }
    #endif

    // FP status register preserved across run (we save/restore around runner)
    {
        struct result _r;
        (void)_r;
        #if defined(__riscv)
        uint32_t before, after;
        asm volatile("frcsr %0" : "=r"(before));
        _r = run_with_instr(NOP_32, &_t_regs7);
        asm volatile("frcsr %0" : "=r"(after));
        assert(before == after);
        #elif defined(__aarch64__)
        uint32_t before, after;
        asm volatile("mrs %0, fpsr" : "=r"(before));
        _r = run_with_instr(NOP_32, &_t_regs7);
        asm volatile("mrs %0, fpsr" : "=r"(after));
        assert(before == after);
        #endif
    }

    // For a single NOP under META, instret (after baseline) should be 1
    #ifdef META
    {
        struct result _r = run_with_instr(NOP_32, &_t_regs7);
        if (_r.meta.instret > 0) {
            assert(_r.signum == 0);
            assert(_r.meta.instret == 1);
            assert(_r.meta.cycle > 0);
        }
    }
    #endif

    // Unit tests for mem_change_equal helper
    #ifdef CHECK_MEM
    {
        struct mem_change a = {0}, b = {0};
        a.start = b.start = 0x1000;
        a.n = b.n = 32;
        for (unsigned i = 0; i < CHECK_MEM_CUT_AT; i++) {
            a.val[i] = (uint8_t)(i * 3 + 1);
            b.val[i] = a.val[i];
        }
        a.checksum = b.checksum = 0xdeadbeefU;
        assert(mem_change_equal(&a, &b));

        b.checksum = 0xabadcafeU;
        assert(!mem_change_equal(&a, &b));
        b.checksum = a.checksum;

        b.val[0] ^= 0xFF;
        assert(!mem_change_equal(&a, &b));
        b.val[0] ^= 0xFF;
        assert(mem_change_equal(&a, &b));

        b.n = 16;
        assert(!mem_change_equal(&a, &b));
        b.n = a.n;

        b.start = a.start + 8;
        assert(!mem_change_equal(&a, &b));
    }
    #endif

    runner_tests_running = 0;

#ifdef VERBOSE
    printf("Finished runner_init\n\n\n");
#endif
}


static inline const char* color_for_signum(int sig) {
    if (sig == 0) return COLOR_GREEN;
    switch (sig) {
        case SIGSEGV: return COLOR_RED;
        case SIGBUS:  return COLOR_RED;
        case SIGALRM: return COLOR_YELLOW;
        case SIGTRAP: return COLOR_CYAN;
        default:      return COLOR_YELLOW;
    }
}

void print_result_opt(FILE* file, const char* prefix, const struct regs* regs_before, struct result* r, int color_on) {
    if (!file) file = stdout;
    if (!prefix) prefix = "";

    if (r->signum != 0) {
        if (color_on) {
            const char* col = color_for_signum(r->signum);
            fprint_pref(file, prefix, "%s%s" COLOR_RESET " (%d)\n", col, strsignal(r->signum), r->signum);
        } else {
            fprint_pref(file, prefix, "%s (%d)\n", strsignal(r->signum), r->signum);
        }
        fprint_aligned_label(file, prefix, "si_code:"); fprint_pref(file, "", "%d\n", r->si_code);
        fprint_aligned_label(file, prefix, "si_addr:"); fprint_pref(file, "", "0x%lx\n", r->si_addrr);
        fprint_aligned_label(file, prefix, "si_pc:");   fprint_pref(file, "", "0x%lx\n", r->si_pc);
        if (r->instr_idx) { fprint_aligned_label(file, prefix, "instr_idx:"); fprint_pref(file, "", "%u\n", r->instr_idx); }
    } else {
        if (color_on) fprint_pref(file, prefix, COLOR_GREEN "OK" COLOR_RESET " (0)\n");
        else          fprint_pref(file, prefix, "OK (0)\n");
    }

    // Registers: use arch-specific helper with options
    print_reg_diff_opts(file, prefix, regs_before, &r->regs_result, color_on);

#ifdef META
    fprint_pref(file, prefix, "meta:\n");
    fprint_pref(file, prefix, "  instret: %lu\n", r->meta.instret);
    fprint_pref(file, prefix, "  cycle: %lu\n", r->meta.cycle);
#endif

#ifdef CHECK_MEM
    if (r->n_mem_changes == 0) {
        fprint_pref(file, prefix, "no mem changes recorded\n");
    } else {
        fprint_pref(file, prefix, "mem changes (%d):\n", r->n_mem_changes);
        fprint_pref(file, prefix, "base               n    val\n");
        for (int i = 0; i < r->n_mem_changes; i++) {
            uint32_t n_full = r->mem_changes[i].n;
            uint32_t n_cap = n_full < CHECK_MEM_CUT_AT ? n_full : CHECK_MEM_CUT_AT;
            fprint_pref(file, prefix, "0x%016lx %4d ", r->mem_changes[i].start, n_full);
            fprint_hex(file, r->mem_changes[i].val, n_cap);
            if (n_full > CHECK_MEM_CUT_AT) {
                fprintf(file, " (cut at %d bytes)", CHECK_MEM_CUT_AT);
            }
            fprintf(file, "\n");
        }
    }
#endif
}

void log_input(FILE* file, uint32_t* instr_seq, unsigned n, const struct regs *regs_before, struct mapping* mappings, unsigned mappings_n) {
    fprintf(file, "input:\n");
    fprintf(file, "  instr_seq:\n");
    for (unsigned i = 0; i < n; i++) {
        fprintf(file, "  - 0x%08x\n", instr_seq[i]);
    }
    fprintf(file, "  dis_opcodes:\n");
    for (unsigned i = 0; i < n; i++) {
        char* dis = disasm_opcodes(instr_seq[i], 0);
        assert(dis);
        fprintf(file, "  - '%s'\n", dis);
        free(dis);
    }
    fprintf(file, "  regs:\n");
    fprintf(file, "    gp:\n");
    LOOP_OVER_GP(regs_before,
        fprintf(file, "      %s: 0x%lx\n", get_abi_name(abi_i), *val);
    )
#ifdef FLOATS
    fprintf(file, "    fp:\n");
    LOOP_OVER_FP(regs_before,
        fprintf(file, "      %s: 0x%lx\n", get_abi_name_float(abi_i), val->u);
    )
#endif
#ifdef VECTOR
    fprintf(file, "    vec:\n");
    LOOP_OVER_VECTOR_REGS(regs_before,
        fprintf(file, "      %s: ", get_abi_name(abi_i));
        fprint_vec(file, (uint8_t*)val, sizeof(*val));
        fprintf(file, "\n");
    )
#endif

    if (mappings_n != 0) {
        assert(mappings);
        fprintf(file, "mappings:\n");
        for (unsigned i = 0; i < mappings_n; i++) {
            struct mapping* mapping = &mappings[i];
            fprintf(file, "- start: 0x%lx\n", mapping->start);
            fprintf(file, "  n: 0x%lx\n", mapping->n);
            fprintf(file, "  prot: 0x%x\n", mapping->prot);
            fprintf(file, "  val: \"");
            for (unsigned j = 0; j < mapping->n/sizeof(uint64_t); j++) {
                fprintf(file, "%016lx", ((uint64_t*)mapping->val)[j]);
            }
            fprintf(file, "\"\n");
        }
    } else {
        fprintf(file, "mappings: []\n");
    }
}

static void log_client_block_yaml(FILE* file, const char* prefix, const cpu_info* cpu) {
    if (!file || !cpu) return;
    fprintf(file, "%s  clients:\n", prefix);

    // Indentation for fields at the item level (4 spaces beyond prefix)
    char item_indent[64];
    snprintf(item_indent, sizeof(item_indent), "%s    ", prefix ? prefix : "");

    // Emit as list item: "- " then the block contents
    fprintf(file, "%s  - ", prefix);
    log_device_block(file, item_indent);

    fprintf(file, "%sn_core: %u\n", item_indent, cpu->processor);

    fprintf(file, "%smicroarchitecture:\n", item_indent);
    log_microarchitecture_block(file, item_indent, cpu, 0);
}

void log_client_info_yaml(FILE* file) {
    if (!file) return;

    // Device information at top-level
    log_device_block(file, "");

    // Detect and list all unique microarchitectures present
    cpu_info cpus[MAX_CPUS];
    unsigned n = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);
    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, n);

    if (num_core_types == 0) {
        fprintf(file, "microarchitectures: []\n");
        return;
    }

    fprintf(file, "microarchitectures:\n");
    for (unsigned i = 0; i < num_core_types; i++) {
        cpu_info* any = core_types[i].cpus[0];
        log_microarchitecture_block(file, "", any, 1);
    }
}

void log_result(FILE *file, struct result* result, const struct regs *regs_before, const char* prefix, const cpu_info* cpu) {
    struct regs* regs_after = &result->regs_result;

    fprintf(file, "%s- result:\n", prefix);
    fprintf(file, "%s    signum: 0x%x\n", prefix, result->signum);
    if (result->signum != 0) {
        fprintf(file, "%s    si_addr: 0x%lx\n", prefix, result->si_addrr);
        fprintf(file, "%s    si_pc: 0x%lx\n", prefix, result->si_pc);
        fprintf(file, "%s    si_code: 0x%x\n", prefix, result->si_code);
        if (result->instr_idx) {
            fprintf(file, "%s    instr_idx: %u\n", prefix, result->instr_idx);
        }
    }

    fprintf(file, "%s    regs_after:", prefix);
    int no_diff = 1;
    LOOP_OVER_GP_DIFF(regs_before, regs_after,
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      %s: 0x%lx\n", prefix, get_abi_name(abi_i), *after);
    )
#if defined(__aarch64__)
    if (regs_before->pstate != regs_after->pstate) {
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      pstate: 0x%lx\n", prefix, regs_after->pstate);
    }
    #if defined(FLOATS) || defined(VECTOR)
    if (regs_before->fpsr != regs_after->fpsr) {
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      %s: 0x%lx\n", prefix, get_abi_name(getregindex(fpsr)), regs_after->fpsr);
    }
    #endif
#elif defined(__riscv)
    #if defined(FLOATS)
    if (regs_before->fcsr != regs_after->fcsr) {
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      %s: 0x%lx\n", prefix, get_abi_name(getregindex(fcsr)), regs_after->fcsr);
    }
    #endif
#endif
#if defined(FLOATS)
    LOOP_OVER_FP_DIFF(regs_before, regs_after,
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      %s: 0x%lx\n", prefix, get_abi_name_float(abi_i), after->u);
    )
#endif
#ifdef VECTOR
    LOOP_OVER_VECTOR_REGS_DIFF(regs_before, regs_after,
        if (no_diff) {
            fprintf(file, "\n");
            no_diff = 0;
        }
        fprintf(file, "%s      %s: ", prefix, get_abi_name(abi_i));
        fprint_vec(file, (uint8_t*)after, sizeof(*after));
        fprintf(file, "\n");
    )
#endif
    if (no_diff) {
        fprintf(file, " []\n");
        no_diff = 0;
    }

#ifdef CHECK_MEM
    if (result->n_mem_changes == 0) {
        fprintf(file, "%s    mem_diffs: []\n", prefix);
    } else {
        if (result->n_mem_changes >= CHECK_MEM_MAX_NUMBER_MEM_CHANGES) {
            fprintf(file, "%s    mem_diffs_capped_at: %d\n", prefix, CHECK_MEM_MAX_NUMBER_MEM_CHANGES);
        }
        fprintf(file, "%s    mem_diffs:\n", prefix);
        for (int i = 0; i < result->n_mem_changes; i++) {
            uint32_t n_full = result->mem_changes[i].n;
            uint32_t n_cap = n_full < CHECK_MEM_CUT_AT ? n_full : CHECK_MEM_CUT_AT;
            fprintf(file, "%s    - start: 0x%016lx\n", prefix, result->mem_changes[i].start);
            fprintf(file, "%s      n: 0x%x\n", prefix, n_full);
            fprintf(file, "%s      val: ", prefix);
            fprint_hex(file, result->mem_changes[i].val, n_cap);
            fprintf(file, "\n");
            if (n_full > CHECK_MEM_CUT_AT) {
                fprintf(file, "%s      cut_at: 0x%x\n", prefix, CHECK_MEM_CUT_AT);
            }
            fprintf(file, "%s      checksum: 0x%08x\n", prefix, result->mem_changes[i].checksum);
        }
    }
#endif

#ifdef META
    fprintf(file, "%s    meta:\n", prefix);
    fprintf(file, "%s      cycle: %lu\n", prefix, result->meta.cycle);
    fprintf(file, "%s      instret: %lu\n", prefix, result->meta.instret);
#endif

    // Attach client block as sibling to 'result'
    log_client_block_yaml(file, prefix, cpu);
}

static void fprint_pref(FILE* f, const char* prefix, const char* fmt, ...) {
    if (!f) return;
    if (!prefix) prefix = "";
    va_list ap;
    va_start(ap, fmt);
    fputs(prefix, f);
    vfprintf(f, fmt, ap);
    va_end(ap);
}

static inline void fprint_aligned_label(FILE* f, const char* prefix, const char* label_with_colon) {
    fprint_pref(f, prefix, "%*s ", (int)LABEL_ALIGN_W, label_with_colon);
}

void print_result(struct regs* regs_before, struct result* r) {
    print_result_opt(stdout, "", regs_before, r, log_should_color(stdout));
}

void log_repro(FILE *file, result_cpu_pair* items, unsigned n_items, uint32_t* instr_seq, unsigned n, const struct regs *regs_before, struct mapping* mappings, unsigned mappings_n) {
    // Human-readable header (commented)
    char hostbuf[256];
    detect_preferred_hostname(hostbuf, sizeof(hostbuf), NULL);
    const char* prefix = "# ";

    // If at least two, show a concise diff first (top of file) with a header
    if (n_items >= 2) {
        fprint_pref(file, prefix, "=== Diff ===\n");
        print_result_diff(file, items[0].result, items[1].result, prefix, 0);
    }

    // Print per-result human-readable summaries (CPU followed by result)
    for (unsigned i = 0; i < n_items; i++) {
        cpu_info* cpu = items[i].cpu;
        fprint_pref(file, prefix, "=== Result %u ===\n", i);
        if (cpu) {
            char* vend = midr_to_vendor(cpu->midr);
            char* micro = midr_to_microarch(cpu->midr);
            char vecbuf[32];
            build_vec_suffix(vecbuf, sizeof(vecbuf));
            fprint_pref(file, prefix, "%s %s 0x%08x (%s%s)\n",
                        vend ? vend : "unknown", micro ? micro : "unknown",
                        cpu->midr, hostbuf[0] ? hostbuf : "unknown", vecbuf);
        }
        // Print result into the same FILE with comment prefix
        print_result_opt(file, prefix, regs_before, items[i].result, 0);
    }

    // YAML section
    log_input(file, instr_seq, n, regs_before, mappings, mappings_n);

    fprintf(file, "results:\n");
    for (unsigned i = 0; i < n_items; i++) {
        log_result(file, items[i].result, regs_before, "", items[i].cpu);
    }

#if !defined(VECTOR) && !defined(FLOATS) && !defined(CHECK_MEM) && !defined(AUTO_MAP_MEM) && !defined(META)
    fprintf(file, "flags: []\n");
#else
    fprintf(file, "flags:\n");
    #ifdef VECTOR
    fprintf(file, "- -DVECTOR\n");
    #endif
    #ifdef FLOATS
    fprintf(file, "- -DFLOATS\n");
    #endif
    #ifdef CHECK_MEM
    fprintf(file, "- -DCHECK_MEM\n");
    #endif
    #ifdef AUTO_MAP_MEM
    fprintf(file, "- -DAUTO_MAP_MEM\n");
    #endif
    #ifdef AUTO_MAP_MEM_RW_PROT
    fprintf(file, "- -DAUTO_MAP_MEM_RW_PROT\n");
    #endif
    #ifdef META
    fprintf(file, "- -DMETA\n");
    #endif
#endif

    #if defined(__aarch64__)
    fprintf(file, "arch: aarch64\n");
    #elif defined(__riscv)
    fprintf(file, "arch: riscv64\n");
    #endif
    fprintf(file, "git_commit: %s\n", git_commit);
}

// Early process init: unbuffered I/O, musl heap base, and detected microarchitectures
void early_init() {
    // Always flush stdout/err immediately
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Set musl heap base early (no-op with a warning on non-patched musl)
    musl_set_heap_base();

    // Get once to cache it because requires vdso
    (void)get_vec_size();

    // Detect CPUs and print one full info line per microarch (by MIDR), like pin logs
    cpu_info cpus[MAX_CPUS];
    unsigned n = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);

    // Hostname once (used in per-line suffix)
    char hostbuf[256];
    detect_preferred_hostname(hostbuf, sizeof(hostbuf), NULL);
    log_info("Hostname: %s", hostbuf[0] ? hostbuf : "unknown");

    // Group by MIDR using existing helper
    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, n);

    log_info("Detected microarchitectures:");
    const char* info_indent = "      "; // length("INFO: ") spaces for alignment
    for (unsigned i = 0; i < num_core_types; i++) {
        core_type* ct = &core_types[i];
        char* vend = midr_to_vendor(ct->midr);
        char* micro = midr_to_microarch(ct->midr);
        char vecbuf[32];
        build_vec_suffix(vecbuf, sizeof(vecbuf));
        const char* vendor = vend ? vend : "unknown";
        const char* microarch = micro ? micro : "unknown";
        const char* host = hostbuf[0] ? hostbuf : "unknown";
        // Example: "      - 4 x Arm Cortex-A76 0x4148d0b0 (lab39 VEC=16)"
        fprintf(stdout, "%s- %u x %s %s 0x%08x (%s%s)\n",
               info_indent, ct->count, vendor, microarch, ct->midr, host, vecbuf);
    }

    // Show current CPU context (we may repin later)
    cpu_info* cur = get_current_cpu_info();
    if (cur) {
        char* vend = midr_to_vendor(cur->midr);
        char* micro = midr_to_microarch(cur->midr);
        char vecbuf[32];
        build_vec_suffix(vecbuf, sizeof(vecbuf));
        log_info("Currently running on: %s %s 0x%08x (%s%s)",
               vend ? vend : "unknown", micro ? micro : "unknown",
               cur->midr, hostbuf[0] ? hostbuf : "unknown", vecbuf);
    }
}

static inline char* lstrip_ws(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static inline int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

#ifdef VECTOR
static void parse_hex_le_bytes(const char* hex, uint8_t* out, size_t out_len) {
    memset(out, 0, out_len);
    size_t len = strlen(hex);
    size_t pos = len;
    size_t off = 0;
    while (pos > 0 && off < out_len) {
        int lo = hex_nibble(hex[--pos]);
        int hi = 0;
        if (pos > 0) {
            int h = hex_nibble(hex[--pos]);
            if (h >= 0) hi = h;
        }
        if (lo < 0) lo = 0;
        out[off++] = (uint8_t)((hi << 4) | lo);
    }
}
#endif

void load_repro(char* content, struct regs* regs_before, uint32_t* instr_seq, unsigned* seq_n, struct mapping** mappings, unsigned* mappings_n, uint32_t** midrs_out, unsigned* midrs_out_n) {
    // Prepare optional MIDR list outputs
    if (midrs_out) *midrs_out = NULL;
    if (midrs_out_n) *midrs_out_n = 0;

    // Make a copy for a second pass (collecting MIDRs across nested YAML)
    char* content_copy = NULL;
    if (midrs_out && midrs_out_n && content) {
        content_copy = strdup(content);
    }
    char* saveptr = NULL;
    char* line = strtok_r(content, "\n", &saveptr);

    unsigned instr_index = 0;
    unsigned mapping_index = 0;
    unsigned mappings_cap = 0;
    if (mappings == NULL || mappings_n == NULL) {
        log_error("load_repro: invalid mappings output parameters");
        exit(EXIT_FAILURE);
    }
    *mappings_n = 0;
    *mappings = NULL;

    int res;
    enum {
        flag_vector     = 1 << 0,
        flag_floats     = 1 << 1,
        flag_check_mem  = 1 << 2,
        auto_map_mem    = 1 << 3,
        auto_map_mem_rw_prot = 1 << 4,
        flag_meta       = 1 << 5,
    };
    int parsed_mask = -1; // -1 means not seen; 0 means seen and empty
    while (line) {
        char* s = lstrip_ws(line);

        if (*s == '\0' || *s == '#') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (strncmp(s, "input:", 6) == 0 || strncmp(s, "results:", 8) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (strncmp(s, "instr_seq:", 10) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            while (line) {
                char* t = lstrip_ws(line);
                if (*t != '-') break;
                uint32_t instr;
                res = sscanf(t, "- %x", &instr);
                assert(res == 1);
                if (instr_index >= MAX_SEQ_LEN) {
                    log_error("Reproducer sequence length exceeds MAX_SEQ_LEN (%d). Got at least %u instructions.",
                            MAX_SEQ_LEN, instr_index + 1);
                    exit(EXIT_FAILURE);
                }
                instr_seq[instr_index++] = instr;
                line = strtok_r(NULL, "\n", &saveptr);
            }
            continue;
        }

        if (strncmp(s, "regs:", 5) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            while (line) {
                char* t = lstrip_ws(line);
                if (strncmp(t, "gp:", 3) == 0) {
                    line = strtok_r(NULL, "\n", &saveptr);
                    unsigned i = 0;
                    while (line) {
                        char* u = lstrip_ws(line);
                        if (*u == '\0') { line = strtok_r(NULL, "\n", &saveptr); continue; }
                        if (strncmp(u, "vec:", 4) == 0 || strncmp(u, "fp:", 3) == 0 || strncmp(u, "dis_", 4) == 0 || strncmp(u, "instr_seq:", 10) == 0 || strncmp(u, "mappings:", 9) == 0) break;
                        unsigned long val;
                        if (sscanf(u, "%*[^:]: %lx", &val) == 1) {
                            ((uint64_t*)&regs_before->gp)[i++] = val;
                        }
                        line = strtok_r(NULL, "\n", &saveptr);
                    }
                    continue;
                }
#ifdef VECTOR
                else if (strncmp(t, "vec:", 4) == 0) {
                    line = strtok_r(NULL, "\n", &saveptr);
                    unsigned i = 0;
                    while (line) {
                        char* u = lstrip_ws(line);
                        if (*u == '\0') { line = strtok_r(NULL, "\n", &saveptr); continue; }
                        if (u[0] != 'v') break;
                        char* p = strstr(u, "0x");
                        vv* regv = &((vv*)&regs_before->vec)[i];
                        memset(regv->v, 0, VEC_REG_SIZE);
                        if (p) {
                            p += 2;
                            char hexbuf[2*VEC_REG_SIZE + 1];
                            size_t k = 0;
                            while (isxdigit((unsigned char)p[k]) && k < sizeof(hexbuf) - 1) k++;
                            if (k > 0) {
                                memcpy(hexbuf, p, k);
                                hexbuf[k] = '\0';
                                parse_hex_le_bytes(hexbuf, regv->v, VEC_REG_SIZE);
                            }
                        }
                        i++;
                        line = strtok_r(NULL, "\n", &saveptr);
                    }
                    continue;
                }
#endif /* VECTOR */
#ifdef FLOATS
                else if (strncmp(t, "fp:", 3) == 0) {
                    line = strtok_r(NULL, "\n", &saveptr);
                    unsigned i = 0;
                    while (line) {
                        char* u = lstrip_ws(line);
                        if (*u == '\0') { line = strtok_r(NULL, "\n", &saveptr); continue; }
                        // Accept both aarch64 (dN) and riscv (fN/ftN/fxN) labels
                        if (u[0] != 'f' && u[0] != 'd') break;
                        unsigned long val;
                        if (sscanf(u, "%*[^:]: %lx", &val) == 1) {
                            ((union fpv*)&regs_before->fp)[i++].u = val;
                        }
                        line = strtok_r(NULL, "\n", &saveptr);
                    }
                    continue;
                }
#endif /* FLOATS */
                else {
                    break;
                }
            }
            continue;
        }

        if (strncmp(s, "mappings:", 9) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            while (line) {
                char* t = lstrip_ws(line);
                if (*t != '-') break;
                if (mapping_index >= mappings_cap) {
                    unsigned new_cap = mappings_cap ? (mappings_cap * 2) : 8;
                    struct mapping* new_buf = realloc(*mappings, new_cap * sizeof(**mappings));
                    if (!new_buf) {
                        log_error("Out of memory growing mappings to %u entries", new_cap);
                        exit(EXIT_FAILURE);
                    }
                    // Zero-initialize new tail
                    if (new_cap > mappings_cap) {
                        memset(new_buf + mappings_cap, 0, (new_cap - mappings_cap) * sizeof(*new_buf));
                    }
                    *mappings = new_buf;
                    mappings_cap = new_cap;
                }
                struct mapping* mapping = &(*mappings)[mapping_index];
                res = sscanf(t, "- start: %lx", &mapping->start);
                assert(res == 1);
                line = strtok_r(NULL, "\n", &saveptr);
                t = lstrip_ws(line);
                res = sscanf(t, "n: %lx", &mapping->n);
                assert(res == 1);
                line = strtok_r(NULL, "\n", &saveptr);
                t = lstrip_ws(line);
                res = sscanf(t, "prot: %x", &mapping->prot);
                assert(res == 1);
                line = strtok_r(NULL, "\n", &saveptr);
                t = lstrip_ws(line);
                mapping->val = malloc(mapping->n);
                assert(mapping->val);
                char* valp = strstr(t, "val:");
                if (valp) {
                    valp += 4;
                    while (*valp && isspace((unsigned char)*valp)) valp++;
                    if (*valp == '"') valp++;
                    if (valp[0] == '0' && (valp[1] == 'x' || valp[1] == 'X')) valp += 2;
                    size_t k = 0; char* q = valp;
                    while (*q && *q != '"' && isxdigit((unsigned char)*q)) { q++; k++; }
                    // Expect full 8-byte words serialized as 16 hex digits each
                    assert(mapping->n % 8 == 0);
                    assert(k == mapping->n * 2);
                    memset(mapping->val, 0, mapping->n);
                    // Decode left-to-right in 16-hex-digit groups (per 8-byte word),
                    // but within each group, fill bytes from least-significant to most
                    // (i.e., read pairs from right to left) to reconstruct little-endian memory.
                    size_t off = 0;
                    for (size_t w = 0; w < k; w += 16) {
                        const char* W = valp + w;
                        for (int p = 7; p >= 0; --p) {
                            int hi = hex_nibble(W[p*2]);
                            int lo = hex_nibble(W[p*2 + 1]);
                            assert(hi >= 0 && lo >= 0);
                            ((uint8_t*)mapping->val)[off++] = (uint8_t)((hi << 4) | lo);
                        }
                    }
                }
                mapping_index++;
                line = strtok_r(NULL, "\n", &saveptr);
            }
            continue;
        }

        // Only accept top-level flags (no leading indentation)
        if ((line == s) && strncmp(s, "flags:", 6) == 0) {
            // Initialize parsed_mask on first encounter
            parsed_mask = 0;
            if (strstr(s, "[]") == NULL) {
                // Consume subsequent list items
                line = strtok_r(NULL, "\n", &saveptr);
                while (line) {
                    char* t = lstrip_ws(line);
                    if (*t != '-') break;
                    char name[64] = {0};
                    res = sscanf(t, "- -D%63s", name);
                    assert(res == 1);
                    size_t len = strlen(name);
                    if (len > 0 && (name[len-1] == ',' || name[len-1] == '\r')) name[len-1] = '\0';
                    if (strcmp(name, "VECTOR") == 0) {
                        parsed_mask |= flag_vector;
                    } else if (strcmp(name, "FLOATS") == 0) {
                        parsed_mask |= flag_floats;
                    } else if (strcmp(name, "CHECK_MEM") == 0) {
                        parsed_mask |= flag_check_mem;
                    } else if (strcmp(name, "AUTO_MAP_MEM") == 0) {
                        parsed_mask |= auto_map_mem;
                    } else if (strcmp(name, "AUTO_MAP_MEM_RW_PROT") == 0) {
                        parsed_mask |= auto_map_mem_rw_prot;
                    } else if (strcmp(name, "META") == 0) {
                        parsed_mask |= flag_meta;
                    } else if (strcmp(name, "VERBOSE") == 0) {
                        // Ignore VERBOSE in reproducer compatibility
                    } else {
                        log_error("Unknown flag in YAML: %s", name);
                        assert(0);
                    }
                    line = strtok_r(NULL, "\n", &saveptr);
                }
            }

            // Compute expected flags from compile-time settings (excluding VERBOSE)
            int expected_mask = 0;
            #ifdef VECTOR
            expected_mask |= flag_vector;
            #endif
            #ifdef FLOATS
            expected_mask |= flag_floats;
            #endif
            #ifdef CHECK_MEM
            expected_mask |= flag_check_mem;
            #endif
            #ifdef AUTO_MAP_MEM
            expected_mask |= auto_map_mem;
            #endif
            #ifdef AUTO_MAP_MEM_RW_PROT
            expected_mask |= auto_map_mem_rw_prot;
            #endif
            #ifdef META
            expected_mask |= flag_meta;
            #endif

            if (parsed_mask >= 0 && parsed_mask != expected_mask) {
                log_error("Reproducer flags mismatch with current build.");
                log_error_noprefix("Expected: ");
                if (expected_mask == 0) fprintf(stderr, "[]");
                if (expected_mask & flag_vector) fprintf(stderr, "-DVECTOR ");
                if (expected_mask & flag_floats) fprintf(stderr, "-DFLOATS ");
                if (expected_mask & flag_check_mem) fprintf(stderr, "-DCHECK_MEM ");
                if (expected_mask & auto_map_mem) fprintf(stderr, "-DAUTO_MAP_MEM ");
                if (expected_mask & auto_map_mem_rw_prot) fprintf(stderr, "-DAUTO_MAP_MEM_RW_PROT ");
                if (expected_mask & flag_meta) fprintf(stderr, "-DMETA ");
                fprintf(stderr, "\n");
                fprintf(stderr, "Found:    ");
                if (parsed_mask == 0) fprintf(stderr, "[]");
                if (parsed_mask & flag_vector) fprintf(stderr, "-DVECTOR ");
                if (parsed_mask & flag_floats) fprintf(stderr, "-DFLOATS ");
                if (parsed_mask & flag_check_mem) fprintf(stderr, "-DCHECK_MEM ");
                if (parsed_mask & auto_map_mem) fprintf(stderr, "-DAUTO_MAP_MEM ");
                if (parsed_mask & auto_map_mem_rw_prot) fprintf(stderr, "-DAUTO_MAP_MEM_RW_PROT ");
                if (parsed_mask & flag_meta) fprintf(stderr, "-DMETA ");
                fprintf(stderr, "\n");
                exit(EXIT_FAILURE);
            }

            // If we parsed a multi-line block, 'line' now points to next section
            continue;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }
    *mappings_n = mapping_index;
    *seq_n = instr_index;

    // Second pass: collect all MIDRs appearing in the file (nested or top-level)
    if (content_copy && midrs_out && midrs_out_n) {
        uint32_t* list = NULL;
        unsigned n_list = 0;
        size_t cap = 0;

        char* saveptr2 = NULL;
        char* ln = strtok_r(content_copy, "\n", &saveptr2);
        while (ln) {
            // Skip commented lines
            char* t = lstrip_ws(ln);
            if (*t == '#') { ln = strtok_r(NULL, "\n", &saveptr2); continue; }
            // Find "midr:" token anywhere in the line
            char* pos = strstr(t, "midr:");
            if (pos) {
                // parse after colon
                pos += 5;
                while (*pos == ' ' || *pos == '\t') pos++;
                unsigned long v = strtoul(pos, NULL, 0);
                if (v != 0) {
                    uint32_t val = (uint32_t)v;
                    // dedupe
                    int seen = 0;
                    for (unsigned i=0; i<n_list; i++) if (list[i] == val) { seen = 1; break; }
                    if (!seen) {
                        if (n_list == cap) {
                            cap = cap ? cap*2 : 4;
                            list = (uint32_t*)realloc(list, cap*sizeof(uint32_t));
                            assert(list);
                        }
                        list[n_list++] = val;
                    }
                }
            }
            ln = strtok_r(NULL, "\n", &saveptr2);
        }
        *midrs_out = list;
        *midrs_out_n = n_list;
        free(content_copy);
    }
}

void load_repro_from_file(const char* path,
                          struct regs* regs_before,
                          uint32_t* instr_seq,
                          unsigned* seq_n,
                          struct mapping** mappings,
                          unsigned* mappings_n,
                          uint32_t** midrs_out,
                          unsigned* midrs_out_n) {
    char* content = read_file((char*)path);
    if (!content) {
        log_error("Failed to read YAML: %s", path);
        exit(EXIT_FAILURE);
    }
    load_repro(content, regs_before, instr_seq, seq_n, mappings, mappings_n, midrs_out, midrs_out_n);
    free(content);
}

#ifdef CHECK_MEM
int mem_change_equal(const struct mem_change* m1, const struct mem_change* m2) {
    if (m1->start != m2->start) {
        return 0;
    } else if (m1->n != m2->n) {
        return 0;
    } else {
        // Require checksum equality so we detect diffs beyond the first bytes
        if (m1->checksum != m2->checksum) return 0;
        // Compare only the captured prefix to avoid overruns
        uint32_t n_cap = m1->n < CHECK_MEM_CUT_AT ? m1->n : CHECK_MEM_CUT_AT;
        if (memcmp(m1->val, m2->val, n_cap) != 0) return 0;
        return 1;
    }
}
#endif

int result_equal(const struct result* r1, const struct result* r2) {
    if (r1->signum != r2->signum) {
        return 0;
    } else if (r1->si_addrr != r2->si_addrr) {
        return 0;
    } else if (r1->si_pc != r2->si_pc) {
        return 0;
    } else if (r1->si_code != r2->si_code) {
        return 0;
    } else if (memcmp(&r1->regs_result, &r2->regs_result, sizeof(r1->regs_result)) != 0) {
        return 0;
    }

#ifdef CHECK_MEM
    if (r1->n_mem_changes != r2->n_mem_changes) {
        return 0;
    }

    for (unsigned i = 0; i < r1->n_mem_changes; i++) {
        if (!mem_change_equal(&r1->mem_changes[i], &r2->mem_changes[i])) {
            return 0;
        }
    }
#endif
    return 1;
}

void print_result_diff(FILE *file, struct result* a, struct result* b, const char* prefix, int color_on) {
    if (!a || !b) return;
    if (!prefix) prefix = "";
    int any = 0;

    if (a->signum != b->signum) {
        fprint_pref(file, prefix, "signum: ");
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)a->signum, (unsigned long)b->signum, color_on); fprintf(file, "\n");
        any = 1;
    }
    if (a->si_addrr != b->si_addrr) {
        fprint_pref(file, prefix, "si_addrr: ");
        fprint_hex_pair_diff_0x_ul(file, a->si_addrr, b->si_addrr, color_on); fprintf(file, "\n");
        any = 1;
    }
    if (a->si_pc != b->si_pc) {
        fprint_pref(file, prefix, "si_pc: ");
        fprint_hex_pair_diff_0x_ul(file, a->si_pc, b->si_pc, color_on); fprintf(file, "\n");
        any = 1;
    }
    if (a->si_code != b->si_code) {
        fprint_pref(file, prefix, "si_code: ");
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)a->si_code, (unsigned long)b->si_code, color_on); fprintf(file, "\n");
        any = 1;
    }

    const struct regs* ra = &a->regs_result;
    const struct regs* rb = &b->regs_result;
    int any_reg = 0;
    LOOP_OVER_GP_DIFF(ra, rb,
        if (!any_reg) { fprint_pref(file, prefix, "gp diffs:\n"); any = any_reg = 1; }
        fprint_pref(file, prefix, "  %s: ", get_abi_name(abi_i));
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)*before, (unsigned long)*after, color_on); fprintf(file, "\n");
    )
#if defined(__aarch64__)
    if (ra->pstate != rb->pstate) {
        if (!any_reg) { fprint_pref(file, prefix, "gp diffs:\n"); any = any_reg = 1; }
        fprint_pref(file, prefix, "  pstate: ");
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)ra->pstate, (unsigned long)rb->pstate, color_on); fprintf(file, "\n");
    }
  #if defined(FLOATS) || defined(VECTOR)
    if (ra->fpsr != rb->fpsr) {
        if (!any_reg) { fprint_pref(file, prefix, "gp diffs:\n"); any = any_reg = 1; }
        fprint_pref(file, prefix, "  %s: ", get_abi_name(getregindex(fpsr)));
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)ra->fpsr, (unsigned long)rb->fpsr, color_on); fprintf(file, "\n");
    }
  #endif
#elif defined(__riscv)
  #if defined(FLOATS)
    if (ra->fcsr != rb->fcsr) {
        if (!any_reg) { fprint_pref(file, prefix, "gp diffs:\n"); any = any_reg = 1; }
        fprint_pref(file, prefix, "  %s: ", get_abi_name(getregindex(fcsr)));
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)ra->fcsr, (unsigned long)rb->fcsr, color_on); fprintf(file, "\n");
    }
  #endif
#endif

#if defined(FLOATS) || (defined(__aarch64__) && defined(VECTOR))
    int any_fp = 0;
    LOOP_OVER_FP_DIFF(ra, rb,
        if (!any_fp) {
        #if defined(__aarch64__) && defined(VECTOR)
            fprint_pref(file, prefix, "fp diffs (subset of vector):\n");
        #else
            fprint_pref(file, prefix, "fp diffs:\n");
        #endif
            any = any_fp = 1;
        }
        fprint_pref(file, prefix, "  %s: ", get_abi_name_float(abi_i));
        fprint_hex_pair_diff_0x_ul(file, (unsigned long)before->u, (unsigned long)after->u, color_on); fprintf(file, "\n");
    )
#endif
#ifdef VECTOR
    int any_vec = 0;
    LOOP_OVER_VECTOR_REGS_DIFF(ra, rb,
        if (!any_vec) { fprint_pref(file, prefix, "vec diffs:\n"); any = any_vec = 1; }
        fprint_pref(file, prefix, "  %s: ", get_abi_name(abi_i));
        fprint_vec_pair_diff_hex(file, (const uint8_t*)before, (const uint8_t*)after, (int)sizeof(*before), color_on); fprintf(file, "\n");
    )
#endif

#ifdef META
    {
        int any_meta = 0;
        if (a->meta.instret != b->meta.instret || a->meta.cycle != b->meta.cycle) {
            fprint_pref(file, prefix, "meta:\n");
            any = any_meta = 1;
        }
        if (a->meta.instret != b->meta.instret) {
            fprint_pref(file, prefix, "  instret: ");
            fprintf(file, "%lu -> %lu\n", (unsigned long)a->meta.instret, (unsigned long)b->meta.instret);
        }
        if (a->meta.cycle != b->meta.cycle) {
            fprint_pref(file, prefix, "  cycle: ");
            fprintf(file, "%lu -> %lu\n", (unsigned long)a->meta.cycle, (unsigned long)b->meta.cycle);
        }
    }
#endif

#ifdef CHECK_MEM
    {
        int any_mem = 0;
        if (a->n_mem_changes != b->n_mem_changes) {
            fprint_pref(file, prefix, "mem_diffs: count %d -> %d\n", a->n_mem_changes, b->n_mem_changes);
            any = any_mem = 1;
        }

        int min_mc = (a->n_mem_changes < b->n_mem_changes) ? a->n_mem_changes : b->n_mem_changes;
        for (int i = 0; i < min_mc; i++) {
            const struct mem_change *ma = &a->mem_changes[i];
            const struct mem_change *mb = &b->mem_changes[i];
            if (!mem_change_equal(ma, mb)) {
                if (!any_mem) { fprint_pref(file, prefix, "mem_diffs:\n"); any = any_mem = 1; }
                fprint_pref(file, prefix, "  - index %d\n", i);
                if (ma->start != mb->start) {
                    fprint_pref(file, prefix, "      start: ");
                    fprint_hex_pair_diff_0x_ul(file, ma->start, mb->start, 0); fprintf(file, "\n");
                }
                if (ma->n != mb->n) {
                    fprint_pref(file, prefix, "      n: ");
                    fprint_hex_pair_diff_0x_ul(file, (unsigned long)ma->n, (unsigned long)mb->n, 0); fprintf(file, "\n");
                }

                // Print value prefixes (capped)
                uint32_t n_min = (ma->n < mb->n) ? ma->n : mb->n;
                uint32_t n_cap = (n_min < CHECK_MEM_CUT_AT) ? n_min : CHECK_MEM_CUT_AT;
                fprint_pref(file, prefix, "      val: ");
                fprint_bytes_pair_diff_hex(file, (const uint8_t*)ma->val, (const uint8_t*)mb->val, (int)n_cap, 0);
                fprintf(file, "\n");

                // Indicate if we cut
                if (ma->n > CHECK_MEM_CUT_AT || mb->n > CHECK_MEM_CUT_AT) {
                    fprint_pref(file, prefix, "      cut_at: 0x%x\n", CHECK_MEM_CUT_AT);
                } else if (ma->n != mb->n) {
                    // Both are fully captured, show tails
                    if (ma->n > n_min) {
                        fprint_pref(file, prefix, "      val_tail_before: ");
                        fprint_hex(file, (uint8_t*)ma->val + n_min, ma->n - n_min);
                        fprintf(file, "\n");
                    }
                    if (mb->n > n_min) {
                        fprint_pref(file, prefix, "      val_tail_after:  ");
                        fprint_hex(file, (uint8_t*)mb->val + n_min, mb->n - n_min);
                        fprintf(file, "\n");
                    }
                }

                // Checksums for visibility beyond the first bytes
                if (ma->checksum != mb->checksum) {
                    fprint_pref(file, prefix, "      checksum: 0x%08x -> 0x%08x\n", ma->checksum, mb->checksum);
                }
            }
        }

        // Extra entries on either side
        if (a->n_mem_changes > min_mc) {
            if (!any_mem) { fprint_pref(file, prefix, "mem_diffs:\n"); any = any_mem = 1; }
            for (int i = min_mc; i < a->n_mem_changes; i++) {
                const struct mem_change *m = &a->mem_changes[i];
                uint32_t n_cap = (m->n < CHECK_MEM_CUT_AT) ? m->n : CHECK_MEM_CUT_AT;
                fprint_pref(file, prefix, "  - index %d (only in A)\n", i);
                fprint_pref(file, prefix, "      start: 0x%016lx\n", m->start);
                fprint_pref(file, prefix, "      n: 0x%x\n", m->n);
                fprint_pref(file, prefix, "      val: "); fprint_hex(file, (uint8_t*)m->val, n_cap); fprintf(file, "\n");
                if (m->n > CHECK_MEM_CUT_AT) fprint_pref(file, prefix, "      cut_at: 0x%x\n", CHECK_MEM_CUT_AT);
                fprint_pref(file, prefix, "      checksum: 0x%08x\n", m->checksum);
            }
        }
        if (b->n_mem_changes > min_mc) {
            if (!any_mem) { fprint_pref(file, prefix, "mem_diffs:\n"); any = any_mem = 1; }
            for (int i = min_mc; i < b->n_mem_changes; i++) {
                const struct mem_change *m = &b->mem_changes[i];
                uint32_t n_cap = (m->n < CHECK_MEM_CUT_AT) ? m->n : CHECK_MEM_CUT_AT;
                fprint_pref(file, prefix, "  - index %d (only in B)\n", i);
                fprint_pref(file, prefix, "      start: 0x%016lx\n", m->start);
                fprint_pref(file, prefix, "      n: 0x%x\n", m->n);
                fprint_pref(file, prefix, "      val: "); fprint_hex(file, (uint8_t*)m->val, n_cap); fprintf(file, "\n");
                if (m->n > CHECK_MEM_CUT_AT) fprint_pref(file, prefix, "      cut_at: 0x%x\n", CHECK_MEM_CUT_AT);
                fprint_pref(file, prefix, "      checksum: 0x%08x\n", m->checksum);
            }
        }
    }
#endif

    if (!any) {
        fprint_pref(file, prefix, "no result differences\n");
    }
}

void setup_memory_mapping(struct mapping* mapping) {
    mapping->fd = portable_shmem_create("memory_mapping", mapping->n);
    check_and_map_shmem_fixed((void*)mapping->start, mapping->n, mapping->prot, mapping->fd);
    mapping->rw_mapping = (uint8_t*)check_and_map_shmem(0, mapping->n, PROT_READ|PROT_WRITE, mapping->fd, 0);
    if (!mapping->val) {
        mapping->val = malloc(mapping->n);
        assert(mapping->val);
    }
}

void setup_memory_mappings(struct mapping* mappings, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        struct mapping* mapping = &mappings[i];
        setup_memory_mapping(mapping);
    }
}

void free_memory_mapping(struct mapping* mapping) {
    int res = munmap(mapping->rw_mapping, mapping->n);
    assert(res == 0);
    mapping->rw_mapping = NULL;
    res = munmap((void*)mapping->start, mapping->n);
    assert(res == 0);
    assert(mapping->val);
    free(mapping->val);
    mapping->val = NULL;
    close(mapping->fd);
    mapping->fd = 0;
}

void free_memory_mappings(struct mapping* mappings, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        struct mapping* mapping = &mappings[i];
        free_memory_mapping(mapping);
    }
}

void load_memory_mapping(struct mapping* mapping) {
    assert(mapping->rw_mapping && mapping->val);
    memcpy((void*)mapping->rw_mapping, mapping->val, mapping->n);
    if ((mapping->prot & PROT_EXEC) != 0) {
        clear_cache_wrapper(mapping->rw_mapping, mapping->rw_mapping+mapping->n);
    }
}

void load_memory_mappings(struct mapping* mappings, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        struct mapping* mapping = &mappings[i];
        load_memory_mapping(mapping);
    }
}

struct mapping* find_mapping(struct mapping* mappings, unsigned n, uintptr_t addr) {
    for (unsigned k=0; k<n; k++) {
        if ((addr - mappings[k].start) < mappings[k].n) {
            return &mappings[k];
        }
    }
    return 0;
}

// Pinning policy (mutually exclusive preference):
// 1) core (>=0)
// 2) uarch_override (exact match by name -> core)
// 3) midr_override (single MIDR)
// 4) midrs list
// 5) random core
void common_pin(int core,
                const uint32_t* midrs,
                unsigned midrs_n,
                uint32_t midr_override,
                const char* uarch_override) {
    if (core >= 0) {
        pin_to_core_verbose((unsigned)core);
    } else if (uarch_override && *uarch_override) {
        int resolved = resolve_uarch_to_core_strict(uarch_override);
        if (resolved < 0) {
            log_error("Requested uarch '%s' not found on this machine.", uarch_override);
            exit(EXIT_FAILURE);
        }
        pin_to_core_verbose((unsigned)resolved);
    } else if (midr_override) {
        (void)pin_to_midr(midr_override);
    } else if (midrs && midrs_n) {
        unsigned pinned_core = pin_to_midr_list(midrs, midrs_n);
        if (pinned_core == UINT_MAX) {
            pin_to_random_core();
        }
    } else {
        pin_to_random_core();
    }
}

// Execute a fully specified reproducer in a unified way used by both
// repro-template-sig and repro-runner.
void repro_run_common(struct regs* regs_before,
                      uint32_t* instr_seq,
                      unsigned seq_n,
                      struct mapping* mappings,
                      unsigned mappings_n,
                      int core,
                      const uint32_t* midrs,
                      unsigned midrs_n,
                      uint32_t midr_override,
                      const char* uarch_override) {
    common_pin(core, midrs, midrs_n, midr_override, uarch_override);

    runner_init();

    if (mappings && mappings_n) {
        setup_memory_mappings(mappings, mappings_n);
        load_memory_mappings(mappings, mappings_n);
    }

    printf("\n");
    log_info("Running instruction sequence...\n");
    struct result r = run_with_instr_seq(instr_seq, seq_n, mappings, mappings_n, regs_before);

    printf("Result:\n");
    print_result(regs_before, &r);
}

void repro_run_common_test(struct regs* regs_before,
                      uint32_t* instr_seq,
                      unsigned seq_n,
                      struct mapping* mappings,
                      unsigned mappings_n,
                      int core,
                      const uint32_t* midrs,
                      unsigned midrs_n,
                      uint32_t midr_override,
                      const char* uarch_override) {
    // Pinning policy: if a fixed core is specified, use it and ignore MIDRs.
    // Else, try the MIDR list in order; if none match, pick a random core.
    if (core >= 0) {
        pin_to_core_verbose((unsigned)core);
    } else if (uarch_override && *uarch_override) {
        int resolved = resolve_uarch_to_core_strict(uarch_override);
        if (resolved < 0) {
            log_error("Requested uarch '%s' not found on this machine.", uarch_override);
            exit(EXIT_FAILURE);
        }
        pin_to_core_verbose((unsigned)resolved);
    } else if (midr_override) {
        (void)pin_to_midr(midr_override);
    } else if (midrs && midrs_n) {
        unsigned pinned_core = pin_to_midr_list(midrs, midrs_n);
        if (pinned_core == UINT_MAX) {
            pin_to_random_core();
        }
    } else {
        pin_to_random_core();
    }

    runner_init();

    if (mappings && mappings_n) {
        setup_memory_mappings(mappings, mappings_n);
        load_memory_mappings(mappings, mappings_n);
    }

    /* dbg(regs_before->gp.ra); */

    printf("\n");
    log_info("Running instruction sequence...\n");
    struct result r = run_with_instr_seq(instr_seq, seq_n, mappings, mappings_n, regs_before);

    printf("Result:\n");
    print_result(regs_before, &r);

    /* struct result r2 = run_with_instr_seq(instr_seq, seq_n, mappings, mappings_n); */

    struct result r2 = run_with_instr_seq(instr_seq, seq_n, mappings, mappings_n, regs_before);
    /* r2.regs_result.gp.x1++; */
    /* r2.regs_result.gp.x3 = 42; */
    /* r2.regs_result.fp.ft0.u = 3; */
    /* r2.regs_result.fp.fs2.u = 33; */
    /* ASSIGN_32(r2.regs_result.vec.v1, 5, 5, 5, 5); */

#ifdef VECTOR
    ASSIGN_16(r2.regs_result.vec.v1, 5, 5);
#endif

    /* *r2.mem_changes[10].val = 0x01; */

    print_result_diff(stdout, &r, &r2, "", log_should_color(stdout));


    // Use current CPU info for repro logging
    cpu_info* cur_cpu = get_current_cpu_info();

    /* struct mapping mappingss[1]; */
    /* uint8_t* a = malloc(4096); */
    /* struct mapping ab = {.start = page_size*100, .n=4096, .val=a}; */
    /* mappingss[0] = ab; */
    /* mappings_n = 1; */
    /* mappings = &mappingss; */

    result_cpu_pair results[2] = {{&r, cur_cpu}, {&r2, cur_cpu}};
    FILE* fpp = fopen("repro-multi.yaml", "w");
    log_repro(fpp, results, asizeof(results), instr_seq, seq_n, regs_before, mappings, mappings_n);

    result_cpu_pair resultss[1] = {{&r, cur_cpu}};
    FILE* fppp = fopen("repro.yaml", "w");
    log_repro(fppp, resultss, asizeof(resultss), instr_seq, seq_n, regs_before, mappings, mappings_n);

    FILE* fpppp = fopen("client.yaml", "w");
    log_client_info_yaml(fpppp);
}
