/*
 * wincons.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"

bool console_batch_mode = false;

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();

    exit(code);
}

/*
 * Various error message and/or fatal exit functions.
 */
void console_print_error_msg(const char *prefix, const char *msg)
{
    fputs(prefix, stderr);
    fputs(": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

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

void timer_change_notify(unsigned long next)
{
}

int console_verify_ssh_host_key(
    Seat *seat, const char *host, int port,
    const char *keytype, char *keystr, char *fingerprint,
    void (*callback)(void *ctx, int result), void *ctx)
{
    int ret;
    HANDLE hin;
    DWORD savemode, i;

    static const char absentmsg_batch[] =
	"在系统注册表缓存中没有找到该服务器密钥。\n"
	"不能保证该服务器是能够正确访问的计算机。\n"
	""
	"该服务器的 %s 密钥指纹为:\n"
	"%s\n"
	"放弃连接。\n";
    static const char absentmsg[] =
	"在系统注册表缓存中没有找到该服务器密钥。\n"
	"不能保证该服务器是能够正确访问的计算机。\n"
	""
	"该服务器的 %s 密钥指纹为:\n"
	"%s\n"
	"如果信任该主机，请输入 \"y\" 增加密钥到"
	" PuTTY 缓存中并继续连接。\n"
	"如果仅仅只希望进行本次连接，而不"
	"将密钥储存，请输入 \"n\"。\n"
	"如果不信任该主机，请按回车键放弃"
	"连接。\n"
	"是否储存该密钥？(y/n) ";

    static const char wrongmsg_batch[] =
	"**警告** - 潜在安全隐患！\n"
	"在系统注册表缓存中不能匹配该服务器密钥。\n"
	"这说明可能该服务器管理员更新了主机密钥，\n"
	"或者更可能是连接到了一台伪装成该服务器的\n"
	"虚假计算机系统。\n"
	""
	"新的 %s 密钥指纹为:\n"
	"%s\n"
	"放弃连接。\n";
    static const char wrongmsg[] =
	"**警告** - 潜在安全隐患！\n"
	"在系统注册表缓存中不能匹配该服务器密钥。\n"
	"这说明可能该服务器管理员更新了主机密钥，\n"
	"或者更可能是连接到了一台伪装成该服务器的\n"
	"虚假计算机系统。\n"
	""
	"新的 %s 密钥指纹为:\n"
	"%s\n"
	"如果确信该密钥被更新同意接受新的密钥，\n"
	"请输入 \"y\" 更新 PuTTY 缓存并继续连接。\n"
	"如果仅仅只希望继续本次连接，而不更新\n"
	"系统缓存，请输入 \"n\"。\n"
	"如果希望完全放弃本次连接，请按回车键\n"
	"取消操作。按下回车键是**唯一**可以保证"
	"的安全选择。\n"
	"更新缓存密钥？(y/n, 回车键取消连接) ";

    static const char abandoned[] = "放弃连接。\n";

    char line[32];

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)		       /* success - key matched OK */
	return 1;

    if (ret == 2) {		       /* key was different */
	if (console_batch_mode) {
	    fprintf(stderr, wrongmsg_batch, keytype, fingerprint);
            return 0;
	}
	fprintf(stderr, wrongmsg, keytype, fingerprint);
	fflush(stderr);
    }
    if (ret == 1) {		       /* key was absent */
	if (console_batch_mode) {
	    fprintf(stderr, absentmsg_batch, keytype, fingerprint);
            return 0;
	}
	fprintf(stderr, absentmsg, keytype, fingerprint);
	fflush(stderr);
    }

    line[0] = '\0';         /* fail safe if ReadFile returns no data */

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n') {
	if (line[0] == 'y' || line[0] == 'Y')
	    store_host_key(host, port, keytype, keystr);
        return 1;
    } else {
	fprintf(stderr, abandoned);
        return 0;
    }
}

int console_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msg[] =
	"服务器支持的第一个 %s 是\n"
	"%s，其低于配置的警告阀值。\n"
	"继续连接？(y/n) ";
    static const char msg_batch[] =
	"服务器支持的第一个 %s 是\n"
	"%s，其低于配置的警告阀值。\n"
	"放弃连接。\n";
    static const char abandoned[] = "放弃连接。\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msg_batch, algtype, algname);
	return 0;
    }

    fprintf(stderr, msg, algtype, algname);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
	return 1;
    } else {
	fprintf(stderr, abandoned);
	return 0;
    }
}

int console_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msg[] =
	"我们储存的此服务器第一个主机密钥类型\n"
	"为 %s，其低于配置的警告阀值。\n"
	"此服务器同时也提供有我们没有储存的高\n"
        "于阀值的下列主机密钥类型：\n"
        "%s\n"
	"继续连接？(y/n) ";
    static const char msg_batch[] =
	"我们储存的此服务器第一个主机密钥类型\n"
	"为 %s，其低于配置的警告阀值。\n"
	"此服务器同时也提供有我们没有储存的高\n"
        "于阀值的下列主机密钥类型：\n"
        "%s\n"
	"放弃连接。\n";
    static const char abandoned[] = "放弃连接。\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msg_batch, algname, betteralgs);
	return 0;
    }

    fprintf(stderr, msg, algname, betteralgs);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
	return 1;
    } else {
	fprintf(stderr, abandoned);
	return 0;
    }
}

bool is_interactive(void)
{
    return is_console_handle(GetStdHandle(STD_INPUT_HANDLE));
}

bool console_antispoof_prompt = true;
bool console_set_trust_status(Seat *seat, bool trusted)
{
    if (console_batch_mode || !is_interactive() || !console_antispoof_prompt) {
        /*
         * In batch mode, we don't need to worry about the server
         * mimicking our interactive authentication, because the user
         * already knows not to expect any.
         *
         * If standard input isn't connected to a terminal, likewise,
         * because even if the server did send a spoof authentication
         * prompt, the user couldn't respond to it via the terminal
         * anyway.
         *
         * We also vacuously return success if the user has purposely
         * disabled the antispoof prompt.
         */
        return true;
    }

    return false;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
static int console_askappend(LogPolicy *lp, Filename *filename,
                             void (*callback)(void *ctx, int result),
                             void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msgtemplate[] =
	"会话日志文件 \"%.*s\" 已经存在。\n"
	"你可以使用新会话日志覆盖旧文件，\n"
	"或者在旧日志文件结尾增加新日志，\n"
	"或在此会话中禁止日志记录。\n"
	"输入 \"y\" 覆盖为新文件，\"n\" 附加到旧文件，\n"
	"或者直接回车禁止日志记录。\n"
	"要覆盖为新文件么？(y/n，回车取消日志记录) ";

    static const char msgtemplate_batch[] =
	"会话日志文件 \"%.*s\" 已经存在。\n"
	"日志功能未被启用。\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msgtemplate_batch, FILENAME_MAX, filename->path);
	fflush(stderr);
	return 0;
    }
    fprintf(stderr, msgtemplate, FILENAME_MAX, filename->path);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y')
	return 2;
    else if (line[0] == 'n' || line[0] == 'N')
	return 1;
    else
	return 0;
}

/*
 * Warn about the obsolescent key file format.
 * 
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char message[] =
	"现在载入的是一个旧版本文件格式的 SSH2\n"
	" 私钥格式。这意味着该私钥文件不是\n"
	"足够的安全。未来版本的 PuTTY 可能会\n"
	"停止对该私钥格式的支持。\n"
	"建议将其转换为新的\n"
	"格式。\n"
	"\n"
	"一旦密钥被载入到 PuTTYgen，你可以简单的\n"
	"使用保存文件来进行转换。\n";

    fputs(message, stderr);
}

/*
 * Display the fingerprints of the PGP Master Keys to the user.
 */
void pgp_fingerprints(void)
{
    fputs("These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
	  "be used to establish a trust path from this executable to another\n"
	  "one. See the manual for more information.\n"
	  "(Note: these fingerprints have nothing to do with SSH!)\n"
	  "\n"
	  "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
          " (" PGP_MASTER_KEY_DETAILS "):\n"
	  "  " PGP_MASTER_KEY_FP "\n\n"
	  "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
          ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
	  "  " PGP_PREV_MASTER_KEY_FP "\n", stdout);
}

static void console_logging_error(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    fprintf(stderr, "%s\n", string);
    fflush(stderr);
}

static void console_eventlog(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    if (flags & FLAG_VERBOSE)
        console_logging_error(lp, string);
}

StripCtrlChars *console_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new(bs_out, false, 0);
}

static void console_write(HANDLE hout, ptrlen data)
{
    DWORD dummy;
    WriteFile(hout, data.ptr, data.len, &dummy, NULL);
}

int console_get_userpass_input(prompts_t *p)
{
    HANDLE hin = INVALID_HANDLE_VALUE, hout = INVALID_HANDLE_VALUE;
    size_t curr_prompt;

    /*
     * Zero all the results, in case we abort half-way through.
     */
    {
	int i;
	for (i = 0; i < (int)p->n_prompts; i++)
            prompt_set_result(p->prompts[i], "");
    }

    /*
     * The prompts_t might contain a message to be displayed but no
     * actual prompt. More usually, though, it will contain
     * questions that the user needs to answer, in which case we
     * need to ensure that we're able to get the answers.
     */
    if (p->n_prompts) {
	if (console_batch_mode)
	    return 0;
	hin = GetStdHandle(STD_INPUT_HANDLE);
	if (hin == INVALID_HANDLE_VALUE) {
	    fprintf(stderr, "无法获取标准输入句柄\n");
	    cleanup_exit(1);
	}
    }

    /*
     * And if we have anything to print, we need standard output.
     */
    if ((p->name_reqd && p->name) || p->instruction || p->n_prompts) {
	hout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hout == INVALID_HANDLE_VALUE) {
	    fprintf(stderr, "无法获取标准输出句柄\n");
	    cleanup_exit(1);
	}
    }

    /*
     * Preamble.
     */
    /* We only print the `name' caption if we have to... */
    if (p->name_reqd && p->name) {
	ptrlen plname = ptrlen_from_asciz(p->name);
	console_write(hout, plname);
        if (!ptrlen_endswith(plname, PTRLEN_LITERAL("\n"), NULL))
	    console_write(hout, PTRLEN_LITERAL("\n"));
    }
    /* ...but we always print any `instruction'. */
    if (p->instruction) {
	ptrlen plinst = ptrlen_from_asciz(p->instruction);
	console_write(hout, plinst);
        if (!ptrlen_endswith(plinst, PTRLEN_LITERAL("\n"), NULL))
	    console_write(hout, PTRLEN_LITERAL("\n"));
    }

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {

	DWORD savemode, newmode;
        size_t len;
	prompt_t *pr = p->prompts[curr_prompt];

	GetConsoleMode(hin, &savemode);
	newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
	if (!pr->echo)
	    newmode &= ~ENABLE_ECHO_INPUT;
	else
	    newmode |= ENABLE_ECHO_INPUT;
	SetConsoleMode(hin, newmode);

	console_write(hout, ptrlen_from_asciz(pr->prompt));

        len = 0;
        while (1) {
            DWORD ret = 0;

            prompt_ensure_result_size(pr, len * 5 / 4 + 512);

            if (!ReadFile(hin, pr->result + len, pr->resultsize - len - 1,
                          &ret, NULL) || ret == 0) {
                len = (size_t)-1;
                break;
            }
            len += ret;
            if (pr->result[len - 1] == '\n') {
                len--;
                if (pr->result[len - 1] == '\r')
                    len--;
                break;
            }
        }

	SetConsoleMode(hin, savemode);

	if (!pr->echo)
            console_write(hout, PTRLEN_LITERAL("\r\n"));

        if (len == (size_t)-1) {
            return 0;                  /* failure due to read error */
        }

	pr->result[len] = '\0';
    }

    return 1; /* success */
}

static const LogPolicyVtable default_logpolicy_vt = {
    console_eventlog,
    console_askappend,
    console_logging_error,
};
LogPolicy default_logpolicy[1] = {{ &default_logpolicy_vt }};
