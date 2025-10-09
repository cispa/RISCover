#include "../regs.h"
#include <stdio.h>
#include <string.h>
#include "../util.h"
#include "../hexdiff.h"
#include "../regdiff_common.h"
#include "../log.h"

const char* get_abi_name(unsigned i) {
    static const char* abi_names[] = {
        REGS_GP, "sp", "pstate",
    #if defined(FLOATS) || defined(VECTOR)
        "fpsr",
    #endif
    #ifdef FLOATS
        REGS_FP
    #endif
    #ifdef VECTOR
        REGS_VECTOR
    #endif
    };

    if (i < sizeof(abi_names)/sizeof(*abi_names)) {
        return abi_names[i];
    } else {
        exit(3);
    }
}

#if defined(FLOATS) || defined(VECTOR)
const char* get_abi_name_float(unsigned i) {
    static const char* abi_names[] = {
        REGS_GP, "sp", "pstate",
        "fpsr",
        REGS_FP
    };

    if (i < sizeof(abi_names)/sizeof(*abi_names)) {
        return abi_names[i];
    } else {
        exit(3);
    }
}
#endif

void print_reg_diff(const struct regs* regs_before, const struct regs* regs_after) {
    print_reg_diff_opts(stdout, "", regs_before, regs_after, log_should_color(stdout));
}

void print_reg_diff_opts(FILE* file, const char* prefix, const struct regs* regs_before, const struct regs* regs_after, int color_on) {
    if (!file) file = stdout;
    if (!prefix) prefix = "";

    int opened = 0;
    // gp
    LOOP_OVER_GP_DIFF(regs_before, regs_after,
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "gp"); opened = 1; }
        char lbl[16]; snprintf(lbl, sizeof(lbl), "%s:", get_abi_name(abi_i));
        regdiff_emit_scalar(file, prefix, color_on, lbl, (unsigned long)*before, (unsigned long)*after);
    )
    if (regs_before->pstate != regs_after->pstate) {
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "gp"); opened = 1; }
        regdiff_emit_scalar(file, prefix, color_on, "pstate:", (unsigned long)regs_before->pstate, (unsigned long)regs_after->pstate);
    }

#if defined(FLOATS) || defined(VECTOR)
    opened = 0;
    const char* fp_section_name =
    #if defined(VECTOR)
        "fp (subset of vector)";
    #else
        "fp";
    #endif
    if (regs_before->fpsr != regs_after->fpsr) {
        if (!opened) { regdiff_begin_section(file, prefix, color_on, fp_section_name); opened = 1; }
        char lbl0[16]; snprintf(lbl0, sizeof(lbl0), "%s:", get_abi_name(getregindex(fpsr)));
        regdiff_emit_scalar(file, prefix, color_on, lbl0, (unsigned long)regs_before->fpsr, (unsigned long)regs_after->fpsr);
    }
    LOOP_OVER_FP_DIFF(regs_before, regs_after,
        if (!opened) { regdiff_begin_section(file, prefix, color_on, fp_section_name); opened = 1; }
        char flbl[16]; snprintf(flbl, sizeof(flbl), "%s:", get_abi_name_float(abi_i));
        regdiff_emit_fp_scalar(file, prefix, color_on, flbl,
                               (unsigned long)before->u, (unsigned long)after->u,
                               0, 0.0, 0.0, 0.0f, 0.0f);
    )
#endif

#ifdef VECTOR
    opened = 0;
    LOOP_OVER_VECTOR_REGS_DIFF(regs_before, regs_after,
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "vec"); opened = 1; }
        char vlbl[16]; snprintf(vlbl, sizeof(vlbl), "%s:", get_abi_name(abi_i));
        regdiff_emit_vec(file, prefix, color_on, vlbl, (const uint8_t*)before, (const uint8_t*)after, (int)sizeof(*before));
    )
#endif
}
