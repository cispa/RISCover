#ifndef LOG_H_
#define LOG_H_

#include <stdarg.h>
#include <stdio.h>

// Simple logging helpers writing to stderr (or stdout if configured).
// Prefix variants add a colored PREFIX: and a space before the message.
// Noprefix variants write the message only (useful for continued lines).

void log_info(const char* fmt, ...);
void log_warning(const char* fmt, ...);
void log_error(const char* fmt, ...);

void log_info_noprefix(const char* fmt, ...);
void log_warning_noprefix(const char* fmt, ...);
void log_error_noprefix(const char* fmt, ...);

// Convenience helper printing: ERROR: <context>: <strerror(errno)>
void log_perror(const char* context);

// Color detection helper usable by non-logging code paths that may print
// colored output. Honors the same environment override as the logger.
// Returns non-zero if color should be used for the given stream.
int log_should_color(FILE* fp);

#endif // LOG_H_
