#include <unistd.h>
#include <ctype.h>

#include "cpuinfo.h"
#include "regs.h"
#include "util.h"
#include "log.h"
#include "runner.h"

#if defined(__aarch64__)
#include "aarch64/generated_midr_db.h"
#elif defined(__riscv)
#include "riscv64/empty_midr_db.h"
#endif
#include <limits.h>
#include <string.h>
#include <errno.h>
// for sched_getcpu
#define _GNU_SOURCE
#include <sched.h>
#undef _GNU_SOURCE

uint32_t build_midr(cpu_info* cpu)
{
    uint32_t midr = 0;
    midr |= cpu->implementer   << MIDR_IMPLEMENTOR_SHIFT;
    midr |= cpu->architecture  << MIDR_ARCHITECTURE_SHIFT;
    midr |= cpu->variant       << MIDR_VARIANT_SHIFT;
    midr |= cpu->part          << MIDR_PARTNUM_SHIFT;
    midr |= cpu->revision      << MIDR_REVISION_SHIFT;
    return midr;
}

char* midr_to_microarch(uint32_t midr)
{
    uint32_t implementer = (midr >> MIDR_IMPLEMENTOR_SHIFT) & 0xFF;
    uint32_t part = (midr >> MIDR_PARTNUM_SHIFT) & 0xFFF;
    for (unsigned i = 0; i < midr_name_table_len; i++) {
        if ((uint32_t)midr_name_table[i].implementer == implementer &&
            (uint32_t)midr_name_table[i].part == part) {
            return (char*)midr_name_table[i].name;
        }
    }
    return 0;
}

static int cpuinfo_cached = 0;
static cpu_info cached_cpus[MAX_CPUS];
static unsigned cached_n_cpus = 0;

cpu_info* get_current_cpu_info(void)
{
    if (!cpuinfo_cached) {
        cached_n_cpus = parse_cpuinfo((cpu_info*)&cached_cpus, MAX_CPUS);
        if (cached_n_cpus == 0) {
            log_error("get_current_cpu_info: failed to parse /proc/cpuinfo");
            exit(EXIT_FAILURE);
        }
        cpuinfo_cached = 1;
    }

    int cur = sched_getcpu();
    if (cur < 0) {
        log_perror("sched_getcpu");
        exit(EXIT_FAILURE);
    }

    // Find matching entry by processor id
    for (unsigned i = 0; i < cached_n_cpus; i++) {
        if ((int)cached_cpus[i].processor == cur) {
            return &cached_cpus[i];
        }
    }

    log_error("get_current_cpu_info: current CPU %d not found in parsed cpuinfo (n=%u)", cur, cached_n_cpus);
    exit(EXIT_FAILURE);
}

char* midr_to_vendor(uint32_t midr)
{
    uint32_t implementer = (midr >> MIDR_IMPLEMENTOR_SHIFT) & 0xFF;
    for (unsigned i = 0; i < midr_vendor_table_len; i++) {
        if ((uint32_t)midr_vendor_table[i].implementer == implementer) {
            return (char*)midr_vendor_table[i].vendor;
        }
    }
    return 0;
}

unsigned parse_cpu_possible() {
    FILE *fp = fopen("/sys/devices/system/cpu/possible", "r");
    if (!fp) {
        log_perror("fopen");
        exit(EXIT_FAILURE);
    }

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        log_perror("fgets");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    unsigned total = 0;
    // Tokenize the string using comma as delimiter.
    char *token = strtok(buf, ",");
    while (token) {
        // Trim any leading or trailing whitespace.
        while (*token == ' ' || *token == '\t')
            token++;

        char *dash = strchr(token, '-');
        if (dash) {
            // Range found, e.g., "3-10"
            int start, end;
            if (sscanf(token, "%d-%d", &start, &end) != 2) {
                log_error("Error parsing range: %s", token);
                exit(EXIT_FAILURE);
            }
            total += (end - start + 1);
        } else {
            // Single CPU id, count as 1.
            total += 1;
        }
        token = strtok(NULL, ",");
    }

    return total;
}

unsigned parse_cpuinfo(cpu_info* cpus, unsigned max_n) {
    assert(max_n <= MAX_CPUS);
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        log_perror("fopen");
        exit(EXIT_FAILURE);
    }
    char line[256];
    memset(cpus, 0, sizeof(*cpus)*max_n);
    int in_block = 0;
    unsigned cpu_count = 0;

    unsigned report_back_n = 0;

    while (fgets(line, sizeof(line), fp)) {
        // If blank line, then we have finished a block.
        if (line[0] == '\n') {
            if (cpu_count >= max_n) {
                break;
            }
            report_back_n = 0;
            continue;
        }

        if (sscanf(line, "processor%*[^0-9]%d", &cpus[cpu_count].processor) == 1) {
            // Assign defaults here
            cpus[cpu_count].architecture = 8; // the default for aarch64

            cpu_count++;
            report_back_n++;
            in_block = 1;
        } else if (in_block) {
            #define TRY_GET(key, c_name) \
                uint32_t c_name = 0; \
                res = sscanf(line, key"%*[ \t]: %i", &c_name); \
                if (res != 1) { \
                    res = sscanf(line, key"%*[ \t]: %i", &c_name); \
                } \
                if (res == 1) { \
                    for (unsigned l = 1; l < report_back_n+1; l++) { \
                        cpus[cpu_count-l].c_name = c_name; \
                    } \
                }

            int res;
            TRY_GET("CPU implementer", implementer)
            TRY_GET("CPU architecture", architecture)
            TRY_GET("CPU variant", variant)
            TRY_GET("CPU part", part)
            TRY_GET("CPU revision", revision)
        }
    }
    fclose(fp);

    for (unsigned i=0; i<cpu_count; i++) {
        cpus[i].midr = build_midr(&cpus[i]);
    }

    if (cpu_count == 0) {
        log_error("No CPU information found.");
        exit(EXIT_FAILURE);
    }

    // Verify that the number of CPUs matches the one from
    // /sys/devices/system/cpu/possible
    // This fails if parsing /proc/cpuinfo failed for some reason
    // Probably a different format (older kernel)
    // TODO: for now uncommented because this can break when CPUs are offline
    /* assert(parse_cpu_possible() == cpu_count); */

    char hostname[100];
    if (gethostname((char*)&hostname, sizeof(hostname)) == 0) {
        if (strcmp(hostname, "lab71") == 0) {
            cpu_count--;
        }
    }

#ifdef SINGLE_THREAD
    return 1;
#else
    return cpu_count;
#endif
}

/*
 * Clustering
 */

unsigned group_core_types(core_type* core_types, cpu_info* cpus, unsigned n_cpus) {
    unsigned n = 0;
    for (unsigned i = 0; i < n_cpus; i++) {
        int found = 0;
        // Try to find an existing core type that matches cpus[i]
        for (unsigned j = 0; j < n; j++) {
            if (cpus[i].midr == core_types[j].midr) {
                core_types[j].cpus[core_types[j].count++] = &cpus[i];
                found = 1;
                break;
            }
        }
        // If no match, create a new core type group.
        if (!found) {
            core_types[n].midr = cpus[i].midr;
            core_types[n].count = 1;
            core_types[n].cpus[0] = &cpus[i];
            n++;
        }
    }
    return n;
}

int try_pin_to_cpu(int cpu) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);

    return sched_setaffinity(0, sizeof(mask), &mask) != -1;
}

void pin_to_cpu(int cpu) {
    // Try 10 times to pin the core and sleep in between.
    // The idea is that other spinning cores can hint the kernel to
    // take this core online again because of high load.
    unsigned tries = 10;
    for (unsigned i = 0; i < tries; i++) {
        if (try_pin_to_cpu(cpu)) {
            return;
        } else {
            if (i < tries - 1) {
                sleep(1);
            } else {
                // Last try so error out
                log_error("%d: sched_setaffinity failed; %s", cpu, strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
    }
}

void pin_to_core_verbose(unsigned core) {
    // Gather CPU info for nice logging
    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);
    if (core >= _num_cpus) {
        log_error("Requested core %u out of range (n=%u)", core, _num_cpus);
        exit(EXIT_FAILURE);
    }
    uint32_t midr = cpus[core].midr;
    char* vend = midr_to_vendor(midr);
    char* micro = midr_to_microarch(midr);
    char vecbuf[32];
    build_vec_suffix(vecbuf, sizeof(vecbuf));
    log_info("Pinning to core %u (MIDR 0x%x, %s %s%s)...",
           core, midr, vend ? vend : "unknown", micro ? micro : "unknown", vecbuf);
    pin_to_cpu((int)core);
}

unsigned pin_to_random_core() {
    // NOTE: if segfault occurs here it's probably because we switched the stack and it got too big
    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);
    int core = rand() % _num_cpus;
    char* vend = midr_to_vendor(cpus[core].midr);
    char* micro = midr_to_microarch(cpus[core].midr);
    char vecbuf[32];
    build_vec_suffix(vecbuf, sizeof(vecbuf));
    log_info("Pinning to random core %d (MIDR 0x%x, %s %s%s)...",
           core, cpus[core].midr, vend ? vend : "unknown", micro ? micro : "unknown", vecbuf);
    pin_to_cpu(core);
    return core;
}

unsigned pin_to_midr(uint32_t midr) {
    // NOTE: if segfault occurs here it's probably because we switched the stack and it got too big
    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);
    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, _num_cpus);

    // Try to find a core with the midr
    for (unsigned i=0; i<num_core_types; i++) {
        core_type* ct = &core_types[i];
        if (ct->midr == midr) {
            // Pick a random core
            int core = ct->cpus[rand() % ct->count]->processor;
            char* vend = midr_to_vendor(midr);
            char* micro = midr_to_microarch(midr);
            char vecbuf2[32];
            build_vec_suffix(vecbuf2, sizeof(vecbuf2));
            log_info("Pinning to MIDR 0x%x (%s %s%s) (core %d)...",
                   midr, vend ? vend : "unknown", micro ? micro : "unknown", vecbuf2, core);
            pin_to_cpu(core);
            return core;
        }
    }

    // Try to find a core where only the revision differs
    for (unsigned i=0; i<num_core_types; i++) {
        core_type* ct = &core_types[i];
        if ((ct->midr & (~((1<<MIDR_PARTNUM_SHIFT)-1))) == (midr & (~((1<<MIDR_PARTNUM_SHIFT)-1)))) {
            // Pick a random core
            int core = ct->cpus[rand() % ct->count]->processor;
            char* vend_t = midr_to_vendor(midr);
            char* micro_t = midr_to_microarch(midr);
            char* vend_f = midr_to_vendor(ct->midr);
            char* micro_f = midr_to_microarch(ct->midr);
            char vecbuf3[32];
            build_vec_suffix(vecbuf3, sizeof(vecbuf3));
            log_info("Pinning to MIDR with different revision 0x%x (%s %s%s) vs 0x%x (%s %s) (core %d)...",
                   ct->midr, vend_f ? vend_f : "unknown", micro_f ? micro_f : "unknown", vecbuf3,
                   midr, vend_t ? vend_t : "unknown", micro_t ? micro_t : "unknown",
                   core);
            pin_to_cpu(core);
            return core;
        }
    }

    log_error("No core with MIDR 0x%x (or revision-compatible) found on this machine.", midr);
    exit(EXIT_FAILURE);
}

unsigned pin_core_type(core_type* ct) {
    cpu_info *cpu;
    for (unsigned i = 0; i < 30; i++) {
        cpu = ct->cpus[rand() % ct->count];
        char* vend = midr_to_vendor(ct->midr);
        char* micro = midr_to_microarch(ct->midr);
        char vecbuf5[32];
        build_vec_suffix(vecbuf5, sizeof(vecbuf5));
    log_info("Pinning to MIDR 0x%x (%s %s%s) (core %d)...",
           ct->midr, vend ? vend : "unknown", micro ? micro : "unknown", vecbuf5, cpu->processor);
        if (try_pin_to_cpu(cpu->processor)) {
            return cpu->processor;
        }
        sleep(i);
    }
    log_error("Couldn't pin to a core for MIDR %x (%s %s)",
            ct->midr,
            midr_to_vendor(ct->midr) ?: "unknown",
            midr_to_microarch(ct->midr) ?: "unknown");
    exit(EXIT_FAILURE);
}

int get_num_cpus() {
    cpu_info cpus[MAX_CPUS];
    return parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);
}

static int str_ieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned ca = (unsigned char)*a++;
        unsigned cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == '\0' && *b == '\0';
}

unsigned pin_to_microarch_name(const char* name) {
    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);

    if (!name || !*name) {
    log_info("Microarch name empty; pinning to random core.");
        return pin_to_random_core();
    }

    // Group by core types; then resolve ct->midr name against requested
    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, _num_cpus);

    for (unsigned i = 0; i < num_core_types; i++) {
        core_type* ct = &core_types[i];
        char* m = midr_to_microarch(ct->midr);
        if (m && str_ieq(m, name)) {
            cpu_info* cpu = ct->cpus[rand() % ct->count];
            char* vend = midr_to_vendor(ct->midr);
            char vecbuf7[32];
            build_vec_suffix(vecbuf7, sizeof(vecbuf7));
            log_info("Pinning to %s %s (MIDR 0x%x%s) (core %u)...",
                   vend ? vend : "unknown", m, ct->midr, vecbuf7, cpu->processor);
            pin_to_cpu(cpu->processor);
            return cpu->processor;
        }
    }

    int core = rand() % _num_cpus;
    char* vend_f = midr_to_vendor(cpus[core].midr);
    char* micro_f = midr_to_microarch(cpus[core].midr);
    char vecbuf8[32];
    build_vec_suffix(vecbuf8, sizeof(vecbuf8));
    log_info("No core with microarch '%s' found. Pinning to random core %d (MIDR 0x%x, %s %s%s)...",
           name, core, cpus[core].midr, vend_f ? vend_f : "unknown", micro_f ? micro_f : "unknown", vecbuf8);
    pin_to_cpu(core);
    return core;
}

int resolve_uarch_to_core_strict(const char* name) {
    if (!name || !*name) return -1;
    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);

    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, _num_cpus);

    for (unsigned i = 0; i < num_core_types; i++) {
        core_type* ct = &core_types[i];
        char* m = midr_to_microarch(ct->midr);
        if (m && str_ieq(m, name)) {
            int core = ct->cpus[0]->processor; // deterministic: first
            char* vend = midr_to_vendor(ct->midr);
            char vecbuf[32];
            build_vec_suffix(vecbuf, sizeof(vecbuf));
            log_info("Selected uarch '%s' -> core %d (MIDR 0x%x, %s %s%s).",
                   name, core, ct->midr, vend ? vend : "unknown", m ? m : "unknown", vecbuf);
            return core;
        }
    }
    return -1;
}

// Try a list of MIDRs in order
// Returns pinned core id on success; UINT_MAX if no MIDR matched.
unsigned pin_to_midr_list(const uint32_t* midrs, unsigned n_midrs) {
    if (!midrs || n_midrs == 0) return UINT_MAX;

    cpu_info cpus[MAX_CPUS];
    unsigned _num_cpus = parse_cpuinfo((cpu_info*)&cpus, MAX_CPUS);

    // Log the incoming MIDR preference list
    {
        char buf[1024];
        size_t off = 0;
        buf[0] = '\0';
        for (unsigned i = 0; i < n_midrs; i++) {
            int wrote = snprintf(buf + off, sizeof(buf) - off, i == 0 ? "0x%x" : ", 0x%x", midrs[i]);
            if (wrote < 0) break;
            if ((size_t)wrote >= sizeof(buf) - off) { off = sizeof(buf) - 1; buf[off] = '\0'; break; }
            off += (size_t)wrote;
        }
        log_info("Trying to pin via MIDR list (%u): [%s]", n_midrs, buf);
    }

    // 1) Group core types and try exact matches in order
    core_type core_types[MAX_CPUS];
    unsigned num_core_types = group_core_types((core_type*)&core_types, (cpu_info*)&cpus, _num_cpus);

    for (unsigned i = 0; i < n_midrs; i++) {
        uint32_t midr = midrs[i];
        for (unsigned j = 0; j < num_core_types; j++) {
            core_type* ct = &core_types[j];
            if (ct->midr == midr) {
                cpu_info* cpu = ct->cpus[rand() % ct->count];
                char* vend = midr_to_vendor(midr);
                char* micro = midr_to_microarch(midr);
                char vecbuf[32]; vecbuf[0] = '\0';
                #ifdef VECTOR
                build_vec_suffix(vecbuf, sizeof(vecbuf));
                #endif
                log_info("Selected list[%u] -> MIDR 0x%x; pinning to core %u (%s %s%s).",
                       i, midr, cpu->processor, vend ? vend : "unknown", micro ? micro : "unknown", vecbuf);
                pin_to_cpu(cpu->processor);
                return cpu->processor;
            }
        }
    }

    // 2) Try matches that differ only in revision
    for (unsigned i = 0; i < n_midrs; i++) {
        uint32_t midr = midrs[i];
        for (unsigned j = 0; j < num_core_types; j++) {
            core_type* ct = &core_types[j];
            if ((ct->midr & (~((1<<MIDR_PARTNUM_SHIFT)-1))) == (midr & (~((1<<MIDR_PARTNUM_SHIFT)-1)))) {
                cpu_info* cpu = ct->cpus[rand() % ct->count];
                char* vend_f = midr_to_vendor(ct->midr);
                char* micro_f = midr_to_microarch(ct->midr);
                char vecbuf[32]; vecbuf[0] = '\0';
                #ifdef VECTOR
                build_vec_suffix(vecbuf, sizeof(vecbuf));
                #endif
                log_info("Selected list[%u] -> MIDR 0x%x (revision differs: have 0x%x); pinning to core %u (%s %s%s).",
                       i, midr, ct->midr, cpu->processor, vend_f ? vend_f : "unknown", micro_f ? micro_f : "unknown", vecbuf);
                pin_to_cpu(cpu->processor);
                return cpu->processor;
            }
        }
    }

    // No match
    log_info("No MIDR from provided list matched any core.");
    return UINT_MAX;
}
