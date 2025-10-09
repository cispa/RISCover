#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "util.h"  // for COLOR_*
#include "log.h"

static int env_color_seen = -1; // -1 unknown, 0 not set, 1 set
static int env_color_value = 0; // only valid if env_color_seen==1

int log_should_color(FILE* fp) {
    if (env_color_seen == -1) {
        const char* env = getenv("COLOR");
        if (env && *env) {
            env_color_seen = 1;
            if (!strcmp(env, "0") || !strcasecmp(env, "off") || !strcasecmp(env, "false") || !strcasecmp(env, "no")) {
                env_color_value = 0;
            } else {
                env_color_value = 1;
            }
        } else {
            env_color_seen = 0;
        }
    }
    if (env_color_seen == 1) {
        return env_color_value;
    }
    int fd = fileno(fp);
    if (fd < 0) return 0;
    return isatty(fd);
}

static void vprint_prefixed(FILE* fp, const char* prefix, const char* color, const char* fmt, va_list ap) {
    if (log_should_color(fp)) {
        fprintf(fp, "%s%s%s ", color, prefix, COLOR_RESET);
    } else {
        fprintf(fp, "%s ", prefix);
    }
    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
}

static void vprint_noprefix(FILE* fp, const char* fmt, va_list ap) {
    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
}

void log_info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_prefixed(stdout, "INFO:", COLOR_CYAN, fmt, ap);
    va_end(ap);
}

void log_warning(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_prefixed(stdout, "WARNING:", COLOR_YELLOW, fmt, ap);
    va_end(ap);
}

void log_error(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_prefixed(stdout, "ERROR:", COLOR_RED, fmt, ap);
    va_end(ap);
}

void log_info_noprefix(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_noprefix(stdout, fmt, ap);
    va_end(ap);
}

void log_warning_noprefix(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_noprefix(stdout, fmt, ap);
    va_end(ap);
}

void log_error_noprefix(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprint_noprefix(stdout, fmt, ap);
    va_end(ap);
}

void log_perror(const char* context) {
    int saved = errno;
    if (log_should_color(stdout)) {
        fprintf(stdout, "%sERROR:%s %s: %s\n", COLOR_RED, COLOR_RESET,
                context ? context : "error", strerror(saved));
    } else {
        fprintf(stdout, "ERROR: %s: %s\n", context ? context : "error", strerror(saved));
    }
}
