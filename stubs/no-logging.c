/*
 * Stub module implementing the logging API for tools that don't do
 * session logging.
 */

#include "putty.h"

void logtraffic(LogContext *ctx, unsigned char c, int logmode) {}
void logflush(LogContext *ctx) {}
void logevent(LogContext *ctx, const char *event) {}
void log_free(LogContext *ctx) {}
void log_reconfig(LogContext *ctx, Conf *conf) {}
void log_packet(LogContext *ctx, int direction, int type,
                const char *texttype, const void *data, size_t len,
                int n_blanks, const struct logblank_t *blanks,
                const unsigned long *seq,
                unsigned downstream_id, const char *additional_log_text) {}

LogContext *log_init(LogPolicy *lp, Conf *conf)
{ return NULL; }
