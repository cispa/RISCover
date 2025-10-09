#define _GNU_SOURCE
// strstr
#include <string.h>
#include <sys/mman.h>
#undef _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "maps.h"
#include "util.h"
#include "log.h"

int check_page_mapped(void* target) {
    int ret = msync(target, page_size, 0);
    return ret != -1 || errno != ENOMEM;
}

void check_and_map_with_flags(void* target, size_t size, int prot, int flags) {
    // safety check: is page already mapped?
    // TODO: check for range here
    if (check_page_mapped(target)) {
        print_proc_self_maps();
        log_error("Page 0x%lx already mapped", (uint64_t)target);
        exit(1);
    }
    void* res = mmap(target, size, prot, flags, -1, 0);
    if (res != target) {
        print_proc_self_maps();
        log_perror("mmap in check_and_map_with_flags failed");
        log_error("Tried to map at 0x%lx. Resulting address was 0x%lx.", (uint64_t)target, (uint64_t)res);
        exit(1);
    }
}

// TODO: dedup somehow?
void* check_and_map_shmem(void* target, size_t size, int prot, int fd, int flags) {
    // safety check: is page already mapped?
    // TODO: check for range here
    if (check_page_mapped(target)) {
        print_proc_self_maps();
        log_error("Page 0x%lx already mapped", (uint64_t)target);
        exit(1);
    }
    // NOTE: this has to be MAP_SHARED, otherwise modifications are not present in the
    // duplicated mapping
    void* res = mmap(target, size, prot, MAP_SHARED|flags, fd, 0);
    if (res == MAP_FAILED) {
        log_perror("mmap failed");
        exit(EXIT_FAILURE);
    }
    return res;
}

void check_and_map_shmem_fixed(void* target, size_t size, int prot, int fd) {
    void* res = check_and_map_shmem(target, size, prot, fd, MAP_FIXED|MAP_FIXED_NOREPLACE);
    if (res != target) {
        print_proc_self_maps();
        log_perror("mmap in check_and_map_with_flags failed");
        log_error("Tried to map at 0x%lx. Resulting address was 0x%lx.", (uint64_t)target, (uint64_t)res);
        exit(1);
    }
}

void check_and_map_with_additional_flags(void* target, size_t size, int prot, int add_flags) {
    check_and_map_with_flags(target, size, prot, MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED|MAP_FIXED_NOREPLACE|add_flags);
}

void check_and_map(void* target, size_t size, int prot) {
    check_and_map_with_additional_flags(target, size, prot, 0);
}


FILE* open_proc_self_maps() {
    FILE *maps_file = fopen("/proc/self/maps", "r");
    if (!maps_file) {
        log_perror("Error opening /proc/self/maps");
        exit(EXIT_FAILURE);
    }
    return maps_file;
}

int get_addr(char* id, uint64_t* start, uint64_t* end) {
    FILE *maps_file = open_proc_self_maps();

    int exit = 1;
    char line[256];
    while (fgets(line, sizeof(line), maps_file)) {
        if (strstr(line, id)) {
            sscanf(line, "%lx-%lx", start, end);
            exit = 0;
            goto exit;
        }
    }

exit:
    fclose(maps_file);
    return exit;
}

void unmap_section(char* id) {
    while (1) {
        uint64_t start, end;
        if (get_addr(id, &start, &end)) {
            // Section not found
            return;
        }

        if (munmap((void*)start, end - start)) {
            log_error("Couldn't unmap section %s (%lx-%lx)", id, start, end);
            print_proc_self_maps();
            exit(EXIT_FAILURE);
        }
    }
}

void print_proc_self_maps() {
    FILE *maps_file = open_proc_self_maps();

    char line[256];
    while (fgets(line, sizeof(line), maps_file)) {
        printf("%s", line);
    }

    fclose(maps_file);
}

__attribute__((noreturn)) void no_return_wrapper(void (*__attribute__((noreturn)) no_return_func)(void)) {
    unmap_section("[stack]");
    no_return_func();
    __builtin_unreachable();
}

// NOTE: You probably want to call clean_memory_mappings before calling this to free up space for the new stack
// Needed since shifting of 0xffff... often results in pointers that fall into the common stack region 0x3ffff...
__attribute__((noreturn)) void switch_stack(uint64_t new_address, void (*__attribute__((noreturn)) no_return_func)(void)) {
    // Map new stack
    assert(new_address % stack_size == 0);
    check_and_map_with_additional_flags((void*)new_address-stack_size, stack_size, PROT_READ | PROT_WRITE, MAP_STACK);

    // Set new sp
    #if defined(__riscv)
    asm volatile("mv sp, %0" : : "r"(new_address));
    #elif defined(__aarch64__)
    asm volatile("mov sp, %0" : : "r"(new_address));
    #endif

    no_return_wrapper(no_return_func);
}

#define ASHMEM_SET_NAME1  _IOW(0x77, 1, char[256])
#define ASHMEM_SET_NAME2  _IOW(0x77, 1, char[128])
#define ASHMEM_SET_SIZE  _IOW(0x77, 3, size_t)

int portable_shmem_create(const char *name, size_t size) {
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd >= 0) {
        if (ioctl(fd, ASHMEM_SET_NAME1, name) < 0 && ioctl(fd, ASHMEM_SET_NAME2, name) < 0) {
            log_perror("ioctl(ASHMEM_SET_NAME)");
            exit(EXIT_FAILURE);
        } else if (ioctl(fd, ASHMEM_SET_SIZE, size) < 0) {
            log_perror("ioctl(ASHMEM_SET_SIZE)");
            exit(EXIT_FAILURE);
        }
    } else {
        fd = memfd_create(name, 0);
        if (fd == -1) {
            log_perror("memfd_create");
            exit(EXIT_FAILURE);
        }
        if (ftruncate(fd, size) == -1) {
            log_perror("ftruncate");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    return fd;
}
