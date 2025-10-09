__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

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
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include "lib/runner.h"
#include "lib/util.h"
#include "lib/log.h"
#include "lib/maps.h"
#include "lib/regs.h"
#include "lib/fuzzing_value_map.h"
#include "lib/cpuinfo.h"

extern char _binary_out_ranges_start[];
extern char _binary_out_ranges_size[];

static void *g_ranges_buf;
static size_t g_ranges_size;

static void decompress_ranges_or_die(void) {
    if (g_ranges_buf) {
        log_error("Ranges already decompressed");
        exit(EXIT_FAILURE);
    }

    const void *compressed = (const void *)_binary_out_ranges_start;
    size_t compressed_size = (size_t)_binary_out_ranges_size;

    struct archive *a = archive_read_new();
    if (!a) {
        log_error("archive_read_new failed");
        exit(EXIT_FAILURE);
    }
    archive_read_support_filter_gzip(a);
    archive_read_support_format_raw(a);

    if (archive_read_open_memory(a, compressed, compressed_size) != ARCHIVE_OK) {
        log_error("archive_read_open_memory failed: %s", archive_error_string(a));
        exit(EXIT_FAILURE);
    }

    struct archive_entry *entry = NULL;
    if (archive_read_next_header(a, &entry) != ARCHIVE_OK) {
        log_error("archive_read_next_header failed: %s", archive_error_string(a));
        exit(EXIT_FAILURE);
    }

    size_t cap = 1 << 20; // start with 1 MiB, grow as needed
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) {
        log_perror("malloc");
        exit(EXIT_FAILURE);
    }
    size_t total = 0;

    for (;;) {
        // Ensure some free space
        if (cap - total < (1 << 16)) { // keep at least 64 KiB headroom
            size_t new_cap = cap << 1;
            uint8_t *new_out = (uint8_t *)realloc(out, new_cap);
            if (!new_out) {
                log_perror("realloc");
                free(out);
                exit(EXIT_FAILURE);
            }
            out = new_out;
            cap = new_cap;
        }

        ssize_t n = archive_read_data(a, out + total, cap - total);
        if (n > 0) {
            total += (size_t)n;
            continue;
        } else if (n == 0) {
            // End of entry
            break;
        } else {
            log_error("archive_read_data failed: %s", archive_error_string(a));
            free(out);
            exit(EXIT_FAILURE);
        }
    }

    archive_read_close(a);
    archive_read_free(a);

    g_ranges_buf = out;
    g_ranges_size = total;
    log_info("Loaded compressed ranges: %zu bytes decompressed", g_ranges_size);
}

#define OUTDIR "undoc-enum-results"

struct stats {
    uint32_t progress;
    uint32_t executed;
    uint64_t undoc_counter;
};
void worker(uint64_t start, uint64_t end, struct stats* stats, cpu_info* cpu) {
    runner_init();

    struct regs regs = {0};
    log_info("Core %u: Starting worker %lx-%lx", cpu->processor, start, end);

    #if defined(__aarch64__)
    uint32_t* ranges = (uint32_t*)g_ranges_buf;
    #elif defined(__riscv)
    uint8_t* ranges = (uint8_t*)g_ranges_buf;
    #endif
    size_t ranges_n = g_ranges_size/sizeof(ranges[0]);

    for (unsigned i = 0; i < sizeof(regs)/sizeof(filler_64); i++) {
        ((uint64_t*)&regs)[i] = filler_64;
    }
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

    unsigned index = 0;
    uint64_t i = 0;

    int cur_range_undef = 1;
    while (i + ranges[index] < start) {
        /* printf("joo %d\n", index); */
        i += ranges[index++];
        cur_range_undef = !cur_range_undef;
    }
    log_info("Core %u skipped to %lx", cpu->processor, i);

    // Create archive
    struct archive *archive;
    archive = archive_write_new();
    archive_write_set_format_pax_restricted(archive);
    archive_write_add_filter_gzip(archive);
    char archive_path[64];
    snprintf(archive_path, sizeof(archive_path), "%s/worker_%08x_%02d.tar.gz", OUTDIR, cpu->midr, cpu->processor);
    archive_write_open_filename(archive, archive_path);

    while (i < end) {
        if (cur_range_undef) {
            uint64_t range_start = i;
            if (i < start) {
                i = start;
            }
            for (; i < range_start+ranges[index]; i++) {
                if (i >= end) {
                    break;
                }
                if (i % (1<<18) == 0) {
                    stats->progress = (i < end ? i : end)-start;
                }

                #ifdef VERBOSE
                log_info("Core %u: running instr: 0x%08lx", cpu->processor, i);
                #endif

                #if MAX_SEQ_LEN != 1
                #error For undocumented enumeration we only need sequences of 1.
                #endif

                #ifdef CHECK_MEM
                #error For undocumented enumeration we want segfaults. We want to see that an instruction accessed memory.
                #endif

                struct result r;
                // try 10 times to get a result, if it still is alarm then we might
                // have found an undocumented looping instruction
                for (int j = 0; j < 10; j++) {
                    r = run_with_instr(i, &regs);
                    if (r.signum != SIGALRM) {
                        break;
                    }
                }
                stats->executed++;
                // TODO: we should include a flag that says the instruction did something to memory
                // TODO: like that we can see loads or cache instructions
                // at least we should assert that no check mem is used

                #ifdef VERBOSE
                log_info("Core %u: finished instr", cpu->processor);
                print_result(&regs, &r);
                #endif

                if (r.signum != 4) {
                    char logpath[64];
                    snprintf(logpath, sizeof(logpath), "%08lx.yaml", i);
                    log_info("Core %u: Discovered %s %s (%d)", cpu->processor, logpath, my_strsignal(r.signum), r.signum);

                    int pipefd[2];
                    if (pipe(pipefd) == -1) {
                        log_perror("pipe");
                        return;
                    }

                    uint32_t seq[] = {i};
                    FILE* a = fdopen(pipefd[1], "w");

                    result_cpu_pair items[] = {{ .result = &r, .cpu = cpu }};
                    log_repro(a, (result_cpu_pair*)&items, asizeof(items), (uint32_t*)&seq, 1, &regs, 0, 0);
                    /* fclose(a); */
                    close(pipefd[1]);

                    char buffer[10*4096];
                    ssize_t bytes_read;
                    ssize_t total_bytes = 0;
                    // Read data from the pipe and track length
                    while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
                        total_bytes += bytes_read;
                    }
                    close(pipefd[0]);

                    struct archive_entry* entry = archive_entry_new();
                    archive_entry_set_pathname(entry, logpath);
                    archive_entry_set_size(entry, total_bytes);
                    archive_entry_set_filetype(entry, AE_IFREG);
                    archive_entry_set_perm(entry, 0640);
                    archive_write_header(archive, entry);

                    archive_write_data(archive, buffer, total_bytes);
                    archive_entry_free(entry);

                    stats->undoc_counter++;
                }
            }
        } else {
            i += ranges[index];
        }
        assert(index < ranges_n);
        index++;
        cur_range_undef = !cur_range_undef;

        stats->progress = (i < end ? i : end)-start;
    }

    archive_write_close(archive);
    archive_write_free(archive);

    log_info("Core %u: Finished. Quitting...", cpu->processor);
    exit(0);
    /* printf("executed: %x this should be around half the space\n", executed); */
}

void start_client() {
    prepare_result_dir(OUTDIR);

    copy_file("/proc/cpuinfo", OUTDIR"/cpuinfo");
    copy_file("/sys/devices/system/cpu/possible", OUTDIR"/possible");

    decompress_ranges_or_die();

    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);

    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, _num_cpus);

    log_info("Found %u processors grouped into %u unique core types.", _num_cpus, num_core_types);

    uint64_t start = 0;
    uint64_t end = 1ul<<32;
    /* uint64_t end = 1ul<<28; */

    /* num_cpus = 3; */

    struct stats* stats = mmap(NULL, _num_cpus*sizeof(*stats), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
    memset(stats, 0, _num_cpus*sizeof(*stats));
    log_info("Spawning %u cores...", _num_cpus);
    cpu_info* cpu;
    uint64_t tmp_start;
    uint64_t tmp_end;
    for (unsigned i = 0; i < num_core_types; i++) {
        core_type *ct = &core_types[i];

        // One thread per core in core class
        for (unsigned j = 0; j < ct->count; j++) {
            if (fork() == 0) {
                cpu = ct->cpus[j];
                pin_to_cpu(ct->cpus[j]->processor);

                tmp_start = start+(end-start+1)/ct->count*j;
                if (j+1 == ct->count) {
                    tmp_end = end;
                } else {
                    tmp_end = start+(end-start+1)/ct->count*(j+1);
                }
                goto thread_setup;
            }
        }
    }

    FILE *status = fopen(OUTDIR"/status", "w");
    if (!status) {
        log_perror("fopen status");
        exit(EXIT_FAILURE);
    }
    while(1) {
        fseek(status, 0, 0);

        unsigned finished_groups = 0;

            printf("\n");

        // Print stats for each core type
        for (unsigned i = 0; i < num_core_types; i++) {
            core_type *ct = &core_types[i];

            char* vend = midr_to_vendor(ct->midr);
            char* micro = midr_to_microarch(ct->midr);
            printf("Core type 0x%08x (%s %s) - %u cores:\n",
                   ct->midr,
                   vend ? vend : "unknown",
                   micro ? micro : "unknown",
                   ct->count);
            fprintf(status, "core type %08x (%s %s):\n", ct->midr, vend ? vend : "unknown", micro ? micro : "unknown");
            uint64_t progress = 0;
            uint64_t undoc_counter = 0;
            uint64_t executed = 0;
            for (unsigned j = 0; j < ct->count; j++) {
                unsigned core = ct->cpus[j]->processor;

                progress += stats[core].progress;
                undoc_counter += stats[core].undoc_counter;
                executed += stats[core].executed;
                uint64_t per_span = (end - start) / ct->count;
                double pct = per_span ? (stats[core].progress * 100.0) / per_span : 0.0;
                uint64_t abs_prog = start + per_span * j + stats[core].progress;
                printf("  Core %2u: 0x%08lx (%4.1f%%)\n", core, abs_prog, pct);
                fprintf(status, "progress %d 0x%08lx\n", core, start+(end-start+1)/ct->count*j+stats[core].progress);
            }
            printf("Total progress 0x%08lx %.1f%%  found: %ld  executed: 0x%08lx (%.1f%%)\n",
                   progress, (progress*100.0)/(end-start), undoc_counter, executed, (executed*100.0)/(end-start));
            fprintf(status, "Total progress 0x%08lx %.01f%% found: %ld executed: 0x%08lx %f\n", progress, (progress*100.0)/(end-start), undoc_counter, executed, (executed*100.0)/(end-start));

            if (progress >= end-start) {
                finished_groups += 1;
            }
        }

        if (finished_groups == num_core_types) {
            log_info("All workers finished successfully. Quitting...");
            fprintf(status, "All workers finished successfully. Quitting...\n");
            exit(0);
        }
        fflush(status);
        sleep(10);
    }

thread_setup:
    worker(tmp_start, tmp_end, &stats[cpu->processor], cpu);
}


/*****************************************************************************/

__attribute__((noreturn)) void fuzzer();
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    early_init();

    start_client();

    exit(0);
}
