#pragma once

// Set the musl malloc heap base very early.
//
// Requires:
// - Program compiled with -DHEAP_BASE=<addr>
// - Linked against patched musl exposing `current_heap_addr` (mallocng)
//
// Usage:
// - Include this header and call musl_set_heap_base(); as the very first
//   statement in main (or equivalent entry), before any allocations.

#include <stddef.h>

#include "util.h"
#include "log.h"

static inline void musl_set_heap_base(void) {
    extern size_t current_heap_addr __attribute__((weak));
    if (&current_heap_addr) {
        current_heap_addr = (size_t)HEAP_BASE;
    } else {
        log_warning("Called musl_set_heap_base but not linked against musl supporting setting heap base.");
    }
}
