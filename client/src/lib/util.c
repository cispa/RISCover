// For strsignal
#include <stdint.h>
#define _GNU_SOURCE
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/utsname.h>
#undef _GNU_SOURCE

#include "util.h"
#include "log.h"
#include "regs.h"
#include "maps.h"

void fprint_hex_plain(FILE* file, uint8_t* ptr, int n) {
    for (int j=0; j<n; j++) {
        fprintf(file, "%02x", ptr[j]);
    }
}

void fprint_hex(FILE* file, uint8_t* ptr, int n) {
    fprintf(file, "0x");
    fprint_hex_plain(file, ptr, n);
}

void print_hex(uint8_t* ptr, int n) {
    fprint_hex(stdout, ptr, n);
}

void fprint_vec(FILE* file, uint8_t* ptr, int n) {
    fprintf(file, "0x");
    for (int j=n-1; j>=0; j--) {
        fprintf(file, "%02x", ptr[j]);
    }
}

void print_vec(uint8_t* ptr, int n) {
    fprint_vec(stdout, ptr, n);
}

void print_hexbuf(uint32_t* ptr, int n) {
    for (int i=0; i<n; i++) {
        printf("%08x ", ptr[i]);
    }
    printf("\n");
}

void print_hexbuf_group(uint8_t* ptr, int n, int size) {
    for (int i=0; i<n; i++) {
        print_hex(&ptr[i*size], size);
        if (i % 3 == 2) {
            printf("\n");
        } else {
            printf(" ");
        }
    }
    printf("\n");
}

char* my_strsignal(int sig) {
    if (sig == 0) {
        return "OK";
    } else {
        return strsignal(sig);
    }
}

void get_current_stack_pointer() {
    uint64_t current_sp;
    #if defined(__riscv)
    asm volatile("mv %0, sp" : "=r"(current_sp));
    #elif defined(__aarch64__)
    asm volatile("mov %0, sp" : "=r"(current_sp));
    #endif
}

void copy_file(char* src_path, char* dst_path) {
    FILE *src = fopen(src_path, "r");
    if (!src) {
        log_perror("fopen src_path");
        exit(EXIT_FAILURE);
    }

    FILE *dst = fopen(dst_path, "w");
    if (!dst) {
        log_perror("fopen dst_path");
        fclose(src);
        exit(EXIT_FAILURE);
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }

    fclose(src);
    fclose(dst);
}

size_t memcmp_common_prefix(const uint8_t* a, const uint8_t* b, size_t n) {
    size_t i = 0;
    // Try larger chunks first to leverage optimized memcmp
    while (n - i >= 512) {
        if (memcmp(a + i, b + i, 512) == 0) {
            i += 512;
        } else {
            break;
        }
    }
    while (i < n && a[i] == b[i]) {
        i++;
    }
    return i;
}

char* read_file(char* path) {
    FILE *fp;
    char* buffer = malloc(4096);
    size_t bytes_read;
    char *result = NULL;
    size_t total_length = 0;
    fp = fopen(path, "r");
    if (fp == NULL) {
        log_error("Could not open %s", path);
        log_perror("fopen");
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
        log_perror("Reading file failed");
        exit(1);
    }
    result[total_length] = '\0';
    fclose(fp);
    // Make sure that no difference comes from that buffer
    memset(buffer, 0, 4096);
    free(buffer);
    return result;
}

void prepare_result_dir(const char *dirname) {
    DIR *dir = opendir(dirname);
    if (dir) {
        /* Directory exists. */
        closedir(dir);

        // Build the old directory name (dirname-old)
        char oldDir[256];
        snprintf(oldDir, sizeof(oldDir), "%s-old", dirname);

        // Check if the old directory already exists
        DIR *oldDirPtr = opendir(oldDir);
        if (oldDirPtr) {
            closedir(oldDirPtr);
            // Delete the old directory recursively
            char rmCmd[256+20];
            snprintf(rmCmd, sizeof(rmCmd), "rm -rf %s", oldDir);
            if(system(rmCmd) == -1) {
                log_perror("system");
                exit(EXIT_FAILURE);
            }
        }

        // Rename the existing directory to oldDir
        if (rename(dirname, oldDir) != 0) {
            log_perror("rename");
            exit(EXIT_FAILURE);
        }
        log_warning("Reusing existing directory. Moved '%s' to '%s'.", dirname, oldDir);
    }

    // Create the new directory
    if (mkdir(dirname, 0777) != 0) {
        log_perror("mkdir");
        exit(EXIT_FAILURE);
    }
}

long long timestamp_us() {
    struct timespec ts;

#if defined(__aarch64__)
    // Direct syscall to bypass VDSO on aarch64
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#endif

    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int get_android_property(char *propname, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return 0;
    dst[0] = '\0';
    if (!propname || !*propname) return 0;

    char cmd[256];
    int n = snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", propname);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return 0;
    }

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        // getprop may not exist on non-Android systems; fail soft
        return 0;
    }

    if (!fgets(dst, (int)dst_len, fp)) {
        dst[0] = '\0';
        pclose(fp);
        return 0;
    }
    pclose(fp);

    // Trim trailing newline and carriage return
    size_t out_len = strcspn(dst, "\r\n");
    dst[out_len] = '\0';
    return (int)out_len;
}

void detect_preferred_hostname(char* out, size_t out_size, const char* override_non_null) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (override_non_null && *override_non_null) {
        snprintf(out, out_size, "%s", override_non_null);
        return;
    }

    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf)) != 0) {
        log_perror("gethostname");
        snprintf(out, out_size, "unknown");
        return;
    }

    if (strcmp(buf, "localhost") != 0) {
        snprintf(out, out_size, "%s", buf);
        return;
    }

    // Android-style fallback when hostname is 'localhost'
    char serialno[256] = {0};
    char model_name[256] = {0};
    char product_name[256] = {0};
    (void)get_android_property("ro.serialno", serialno, sizeof(serialno));
    (void)get_android_property("ro.product.model", model_name, sizeof(model_name));
    (void)get_android_property("ro.product.name", product_name, sizeof(product_name));

    const char *host_env = getenv("HOST");
    if (host_env && strcmp(host_env, "localhost") != 0) {
        snprintf(out, out_size, "%s", host_env);
    } else if (product_name[0] && serialno[0]) {
        snprintf(out, out_size, "%s_%s", product_name, serialno);
    } else if (model_name[0]) {
        snprintf(out, out_size, "%s", model_name);
    } else {
        // Fallback to uname nodename if available
        struct utsname uts;
        if (uname(&uts) == 0 && uts.nodename[0]) {
            snprintf(out, out_size, "%s", uts.nodename);
        } else {
            snprintf(out, out_size, "localhost");
        }
    }
}
