/*
 * Common pieces between the platform console frontend modules.
 */

#include <stdbool.h>
#include <stdarg.h>

#include "putty.h"
#include "misc.h"
#include "console.h"

const char weakcrypto_msg_common_fmt[] =
    "服务器支持的第一个 %s 为\n"
    "%s，其低于配置的警告阀值。\n";

const char weakhk_msg_common_fmt[] =
    "此服务器要存储的第一个主机密钥类型\n"
    "为 %s，其低于配置的警告阀值。\n"
    "此服务器同时也提供有下列高于阀值的\n"
    "主机密钥类型（不会存储）：\n"
    "%s\n";

const char console_continue_prompt[] = "是否继续连接? (y/n) ";
const char console_abandoned_msg[] = "连接已放弃。\n";

const SeatDialogPromptDescriptions *console_prompt_descriptions(Seat *seat)
{
    static const SeatDialogPromptDescriptions descs = {
        .hk_accept_action = "输入 \"y\"",
        .hk_connect_once_action = "输入 \"n\"",
        .hk_cancel_action = "按回车",
        .hk_cancel_action_Participle = "按回车",
    };
    return &descs;
}

bool console_batch_mode = false;

/*
 * Error message and/or fatal exit functions, all based on
 * console_print_error_msg which the platform front end provides.
 */
void console_print_error_msg_fmt_v(
    const char *prefix, const char *fmt, va_list ap)
{
    char *msg = dupvprintf(fmt, ap);
    console_print_error_msg(prefix, msg);
    sfree(msg);
}

void console_print_error_msg_fmt(const char *prefix, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v(prefix, fmt, ap);
    va_end(ap);
}

void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("FATAL ERROR", fmt, ap);
    va_end(ap);
    cleanup_exit(1);
}

void nonfatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("ERROR", fmt, ap);
    va_end(ap);
}

void console_connection_fatal(Seat *seat, const char *msg)
{
    console_print_error_msg("FATAL ERROR", msg);
    cleanup_exit(1);
}

/*
 * Console front ends redo their select() or equivalent every time, so
 * they don't need separate timer handling.
 */
void timer_change_notify(unsigned long next)
{
}
