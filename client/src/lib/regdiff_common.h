// Shared minimal helpers to print register diffs in colored stdout or
// plain FILE-with-prefix mode. Keep logic small and reusable across arch.
#pragma once

#include <stdio.h>
#include "util.h"
#include "hexdiff.h"

static inline void regdiff_begin_section(FILE* file, const char* prefix, int color_on, const char* name)
{
    if (color_on) {
        printf("%s:\n", name);
    } else {
        fputs(prefix ? prefix : "", file ? file : stdout);
        fprintf(file ? file : stdout, "%s:\n", name);
    }
}

static inline void regdiff_emit_scalar(FILE* file, const char* prefix, int color_on,
                                       const char* label_with_colon, unsigned long a, unsigned long b)
{
    if (color_on) {
        print_aligned_label(label_with_colon);
        print_hex_pair_diff_0x_fixed_ul(a, b, (int)(sizeof(unsigned long) * 2));
        printf("\n");
    } else {
        FILE* f = file ? file : stdout;
        const char* pref = prefix ? prefix : "";
        fprintf(f, "%s%*s ", pref, (int)LABEL_ALIGN_W, label_with_colon);
        int w = (int)(sizeof(unsigned long) * 2);
        fprintf(f, "0x%0*lx -> 0x%0*lx\n", w, a, w, b);
    }
}

// show_fp_details: when non-zero and color_on, append human-readable dbl/flt
static inline void regdiff_emit_fp_scalar(FILE* file, const char* prefix, int color_on,
                                          const char* label_with_colon,
                                          unsigned long a, unsigned long b,
                                          int show_fp_details,
                                          double da, double db,
                                          float fa, float fb)
{
    if (color_on) {
        print_aligned_label(label_with_colon);
        print_hex_pair_diff_0x_fixed_ul(a, b, (int)(sizeof(unsigned long) * 2));
        if (show_fp_details) {
            printf(", dbl: ");
            print_num_pair_diff_double(da, db);
            printf(", flt: ");
            print_num_pair_diff_float(fa, fb);
        }
        printf("\n");
    } else {
        // plain: hex only (keep output compact and consistent)
        FILE* f = file ? file : stdout;
        const char* pref = prefix ? prefix : "";
        fprintf(f, "%s%*s ", pref, (int)LABEL_ALIGN_W, label_with_colon);
        int w = (int)(sizeof(unsigned long) * 2);
        fprintf(f, "0x%0*lx -> 0x%0*lx\n", w, a, w, b);
    }
}

static inline void regdiff_emit_vec(FILE* file, const char* prefix, int color_on,
                                    const char* label_with_colon,
                                    const uint8_t* before, const uint8_t* after, int n)
{
    if (color_on) {
        print_aligned_label(label_with_colon);
        print_vec_pair_diff_hex(before, after, n);
        printf("\n");
    } else {
        FILE* f = file ? file : stdout;
        const char* pref = prefix ? prefix : "";
        fprintf(f, "%s%*s ", pref, (int)LABEL_ALIGN_W, label_with_colon);
        fprint_vec(f, (uint8_t*)before, n);
        fputs(" -> ", f);
        fprint_vec(f, (uint8_t*)after, n);
        fprintf(f, "\n");
    }
}

