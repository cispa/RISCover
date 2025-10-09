#include "../regs.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../util.h"
#include "../hexdiff.h"
#include "../regdiff_common.h"
#include "../log.h"

const char* get_abi_name(unsigned i) {
    static const char* abi_names[] = {
        REGS_BEFORE_SP, "sp", REGS_AFTER_SP, "s0", REGS_AFTER_S0, "fcsr", REGS_FP, REGS_VECTOR
    };

    if (i < sizeof(abi_names)/sizeof(*abi_names)) {
        return abi_names[i];
    } else {
        exit(3);
    }
}

#ifdef FLOATS
const char* get_abi_name_float(unsigned i) {
    return get_abi_name(i);
}
#endif


void print_reg_diff(const struct regs* regs_before, const struct regs* regs_after) {
    print_reg_diff_opts(stdout, "", regs_before, regs_after, log_should_color(stdout));
}

void print_reg_diff_opts(FILE* file, const char* prefix, const struct regs* regs_before, const struct regs* regs_after, int color_on) {
    if (!file) file = stdout;
    if (!prefix) prefix = "";

    // Simple helpers
    int opened = 0;
    // gp
    LOOP_OVER_GP_DIFF(regs_before, regs_after,
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "gp"); opened = 1; }
        char lbl[16]; snprintf(lbl, sizeof(lbl), "%s:", get_abi_name(abi_i));
        regdiff_emit_scalar(file, prefix, color_on, lbl, (unsigned long)*before, (unsigned long)*after);
    )

#ifdef FLOATS
    opened = 0;
    if (regs_before->fcsr != regs_after->fcsr) {
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "fp"); opened = 1; }
        char flbl0[16]; snprintf(flbl0, sizeof(flbl0), "%s:", get_abi_name(getregindex(fcsr)));
        regdiff_emit_scalar(file, prefix, color_on, flbl0, (unsigned long)regs_before->fcsr, (unsigned long)regs_after->fcsr);
    }
    LOOP_OVER_FP_DIFF(regs_before, regs_after,
        if (!opened) { regdiff_begin_section(file, prefix, color_on, "fp"); opened = 1; }
        char flbl[16]; snprintf(flbl, sizeof(flbl), "%s:", get_abi_name(abi_i));
        regdiff_emit_fp_scalar(file, prefix, color_on, flbl,
                               (unsigned long)before->u, (unsigned long)after->u,
                               1, before->dbl, after->dbl, before->flt, after->flt);
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
