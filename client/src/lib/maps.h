#ifndef MAPS_H_
#define MAPS_H_

#include <stddef.h>
#include <stdint.h>

int check_page_mapped(void* target);
void check_and_map(void* target, size_t size, int prot);
void check_and_map_with_additional_flags(void* target, size_t size, int prot, int add_flags);

void* check_and_map_shmem(void* target, size_t size, int prot, int fd, int flags);
void check_and_map_shmem_fixed(void* target, size_t size, int prot, int fd);

void unmap_section(char* id);
void print_proc_self_maps();
__attribute__((noreturn)) void switch_stack(uint64_t new_address, void (*__attribute__((noreturn)) no_return_func)(void));

int portable_shmem_create(const char *name, size_t size);

#endif // MAPS_H_
