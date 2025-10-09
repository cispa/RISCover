// Small helpers to print per-nibble hex diffs in yellow
#pragma once

#include <stdio.h>
#include <string.h>
#include "util.h"
#include "log.h"

static inline void fprint_hex_pair_diff_fixed_ul(FILE* f, unsigned long a, unsigned long b, int width_nibbles, int color_on)
{
    FILE* out = f ? f : stdout;
    // Print A and B as fixed-width lowercase hex, optionally coloring differing nibbles
    for (int i = width_nibbles - 1; i >= 0; --i) {
        unsigned ca = (a >> (i * 4)) & 0xF;
        unsigned cb = (b >> (i * 4)) & 0xF;
        char ch = "0123456789abcdef"[ca];
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, ch); else fprintf(out, "%c", ch);
    }
    fprintf(out, " -> ");
    for (int i = width_nibbles - 1; i >= 0; --i) {
        unsigned ca = (a >> (i * 4)) & 0xF;
        unsigned cb = (b >> (i * 4)) & 0xF;
        char ch = "0123456789abcdef"[cb];
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, ch); else fprintf(out, "%c", ch);
    }
}

static inline void print_hex_pair_diff_fixed_ul(unsigned long a, unsigned long b, int width_nibbles)
{
    fprint_hex_pair_diff_fixed_ul(stdout, a, b, width_nibbles, log_should_color(stdout));
}

static inline void fprint_hex_pair_diff_0x_ul(FILE* f, unsigned long a, unsigned long b, int color_on)
{
    FILE* out = f ? f : stdout;
    // Print A and B like 0x%lx -> 0x%lx with optional per-digit coloring
    char sa[2*sizeof(unsigned long)+1];
    char sb[2*sizeof(unsigned long)+1];
    snprintf(sa, sizeof(sa), "%lx", a);
    snprintf(sb, sizeof(sb), "%lx", b);
    int la = (int)strlen(sa);
    int lb = (int)strlen(sb);
    int L = la > lb ? la : lb;

    fputs("0x", out);
    for (int i = 0; i < la; ++i) {
        int j = i - (L - la);
        char ca = sa[i];
        char cb = (j >= 0 && j < lb) ? sb[j] : '0';
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, ca); else fputc(ca, out);
    }
    fputs(" -> 0x", out);
    for (int i = 0; i < lb; ++i) {
        int j = i - (L - lb);
        char cb = sb[i];
        char ca = (j >= 0 && j < la) ? sa[j] : '0';
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, cb); else fputc(cb, out);
    }
}

static inline void print_hex_pair_diff_0x_ul(unsigned long a, unsigned long b)
{
    fprint_hex_pair_diff_0x_ul(stdout, a, b, log_should_color(stdout));
}

// Print two hex numbers with 0x prefix and fixed width (nibbles),
// highlighting per-nibble differences in yellow.
static inline void fprint_hex_pair_diff_0x_fixed_ul(FILE* f, unsigned long a, unsigned long b, int width_nibbles, int color_on)
{
    FILE* out = f ? f : stdout;
    fputs("0x", out);
    for (int i = width_nibbles - 1; i >= 0; --i) {
        unsigned ca = (a >> (i * 4)) & 0xF;
        unsigned cb = (b >> (i * 4)) & 0xF;
        char ch = "0123456789abcdef"[ca];
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, ch); else fprintf(out, "%c", ch);
    }
    fputs(" -> 0x", out);
    for (int i = width_nibbles - 1; i >= 0; --i) {
        unsigned ca = (a >> (i * 4)) & 0xF;
        unsigned cb = (b >> (i * 4)) & 0xF;
        char ch = "0123456789abcdef"[cb];
        if (color_on && ca != cb) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, ch); else fprintf(out, "%c", ch);
    }
}

static inline void print_hex_pair_diff_0x_fixed_ul(unsigned long a, unsigned long b, int width_nibbles)
{
    fprint_hex_pair_diff_0x_fixed_ul(stdout, a, b, width_nibbles, log_should_color(stdout));
}

// Print two vector registers as hex with per-nibble coloring of differences.
// Matches util.c's fprint_vec order (highest byte first) and style (0x prefix).
static inline void fprint_vec_pair_diff_hex(FILE* f, const uint8_t* before, const uint8_t* after, int n, int color_on)
{
    FILE* out = f ? f : stdout;
    fputs("0x", out);
    for (int j = n - 1; j >= 0; --j) {
        uint8_t ba = before[j];
        uint8_t bb = after[j];
        unsigned a_hi = (ba >> 4) & 0xF;
        unsigned a_lo = ba & 0xF;
        unsigned b_hi = (bb >> 4) & 0xF;
        unsigned b_lo = bb & 0xF;
        char hi = "0123456789abcdef"[a_hi];
        char lo = "0123456789abcdef"[a_lo];
        if (color_on && a_hi != b_hi) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, hi); else fprintf(out, "%c", hi);
        if (color_on && a_lo != b_lo) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, lo); else fprintf(out, "%c", lo);
    }
    fputs(" -> 0x", out);
    for (int j = n - 1; j >= 0; --j) {
        uint8_t ba = before[j];
        uint8_t bb = after[j];
        unsigned a_hi = (ba >> 4) & 0xF;
        unsigned a_lo = ba & 0xF;
        unsigned b_hi = (bb >> 4) & 0xF;
        unsigned b_lo = bb & 0xF;
        char hi = "0123456789abcdef"[b_hi];
        char lo = "0123456789abcdef"[b_lo];
        if (color_on && a_hi != b_hi) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, hi); else fprintf(out, "%c", hi);
        if (color_on && a_lo != b_lo) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, lo); else fprintf(out, "%c", lo);
    }
}

static inline void print_vec_pair_diff_hex(const uint8_t* before, const uint8_t* after, int n)
{
    fprint_vec_pair_diff_hex(stdout, before, after, n, log_should_color(stdout));
}

// Print two byte arrays (low->high order) with per-nibble coloring of differences.
// Matches util.c's fprint_hex order and style.
static inline void fprint_bytes_pair_diff_hex(FILE* f, const uint8_t* before, const uint8_t* after, int n, int color_on)
{
    FILE* out = f ? f : stdout;
    fputs("0x", out);
    for (int i = 0; i < n; ++i) {
        uint8_t ba = before[i];
        uint8_t bb = after[i];
        unsigned a_hi = (ba >> 4) & 0xF;
        unsigned a_lo = ba & 0xF;
        unsigned b_hi = (bb >> 4) & 0xF;
        unsigned b_lo = bb & 0xF;
        char hi = "0123456789abcdef"[a_hi];
        char lo = "0123456789abcdef"[a_lo];
        if (color_on && a_hi != b_hi) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, hi); else fprintf(out, "%c", hi);
        if (color_on && a_lo != b_lo) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, lo); else fprintf(out, "%c", lo);
    }
    fputs(" -> 0x", out);
    for (int i = 0; i < n; ++i) {
        uint8_t ba = before[i];
        uint8_t bb = after[i];
        unsigned a_hi = (ba >> 4) & 0xF;
        unsigned a_lo = ba & 0xF;
        unsigned b_hi = (bb >> 4) & 0xF;
        unsigned b_lo = bb & 0xF;
        char hi = "0123456789abcdef"[b_hi];
        char lo = "0123456789abcdef"[b_lo];
        if (color_on && a_hi != b_hi) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, hi); else fprintf(out, "%c", hi);
        if (color_on && a_lo != b_lo) fprintf(out, COLOR_YELLOW "%c" COLOR_RESET, lo); else fprintf(out, "%c", lo);
    }
}

static inline void print_bytes_pair_diff_hex(const uint8_t* before, const uint8_t* after, int n)
{
    fprint_bytes_pair_diff_hex(stdout, before, after, n, log_should_color(stdout));
}

// Print two already-formatted strings with per-character coloring of differences
static inline void print_str_pair_diff(const char* sa, const char* sb)
{
    size_t la = strlen(sa);
    size_t lb = strlen(sb);
    for (size_t i = 0; i < la; ++i) {
        char ca = sa[i];
        char cb = (i < lb) ? sb[i] : '\0';
        if (ca != cb) printf(COLOR_YELLOW "%c" COLOR_RESET, ca); else printf("%c", ca);
    }
    printf(" -> ");
    for (size_t i = 0; i < lb; ++i) {
        char cb = sb[i];
        char ca = (i < la) ? sa[i] : '\0';
        if (ca != cb) printf(COLOR_YELLOW "%c" COLOR_RESET, cb); else printf("%c", cb);
    }
}

// Print float/double pair with per-digit coloring using compact representation
static inline void print_num_pair_diff_double(double a, double b)
{
    char sa[64], sb[64];
    // 17 significant digits reliably round-trips for double
    snprintf(sa, sizeof(sa), "%.17g", a);
    snprintf(sb, sizeof(sb), "%.17g", b);
    print_str_pair_diff(sa, sb);
}

static inline void print_num_pair_diff_float(float a, float b)
{
    char sa[32], sb[32];
    // 9 significant digits is typical for float
    snprintf(sa, sizeof(sa), "%.9g", a);
    snprintf(sb, sizeof(sb), "%.9g", b);
    print_str_pair_diff(sa, sb);
}
