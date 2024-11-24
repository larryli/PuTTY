/*
 * console.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"
#include "console.h"

void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();

    exit(code);
}

void console_print_error_msg(const char *prefix, const char *msg)
{
    fputs(prefix, stderr);
    fputs(": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

/*
 * System for getting I/O handles to talk to the console for
 * interactive prompts.
 *
 * In PuTTY 0.81 and before, these prompts used the standard I/O
 * handles. But this means you can't redirect Plink's actual stdin
 * from a sensible data channel without the responses to login prompts
 * unwantedly being read from it too. Also, if you have a real
 * console handle then you can read from it in Unicode mode, which is
 * an option not available for any old file handle.
 *
 * However, many versions of PuTTY have worked the old way, so we need
 * a method of falling back to it for the sake of whoever's workflow
 * it turns out to break. So this structure equivocates between the
 * two systems.
 */
static bool conio_use_standard_handles = false;
bool console_set_stdio_prompts(bool newvalue)
{
    conio_use_standard_handles = newvalue;
    return true;
}

static bool conio_use_utf8 = true;
bool set_legacy_charset_handling(bool newvalue)
{
    conio_use_utf8 = !newvalue;
    return true;
}

typedef struct ConsoleIO {
    HANDLE hin, hout;
    bool need_close_hin, need_close_hout;
    bool hin_is_console, hout_is_console;
    bool utf8;
    BinarySink_IMPLEMENTATION;
} ConsoleIO;

static void console_write(BinarySink *bs, const void *data, size_t len);

static ConsoleIO *conio_setup(bool utf8, DWORD fallback_output)
{
    ConsoleIO *conio = snew(ConsoleIO);

    conio->hin = conio->hout = INVALID_HANDLE_VALUE;
    conio->need_close_hin = conio->need_close_hout = false;

    init_winver();
    if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS ||
        osPlatformId == VER_PLATFORM_WIN32s)
        conio->utf8 = false;           /* no Unicode support at all */
    else
        conio->utf8 = utf8 && conio_use_utf8;

    /*
     * First try opening the console itself, so that prompts will go
     * there regardless of I/O redirection. We don't do this if the
     * user has deliberately requested a fallback to the old
     * behaviour. We also don't do it in batch mode, because in that
     * situation, any need for an interactive prompt will instead
     * noninteractively abort the connection, and in that situation,
     * the 'prompt' becomes more in the nature of an error message, so
     * it should go to standard error like everything else.
     */
    if (!conio_use_standard_handles && !console_batch_mode) {
        /*
         * If we do open the console, it has to be done separately for
         * input and output, with different magic file names.
         *
         * We need both read and write permission for both handles,
         * because read permission is needed to read the console mode
         * (in particular, to test if a file handle _is_ a console),
         * and write permission to change it.
         */
        conio->hin = CreateFile("CONIN$", GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING, 0, NULL);
        if (conio->hin != INVALID_HANDLE_VALUE)
            conio->need_close_hin = true;

        conio->hout = CreateFile("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING, 0, NULL);
        if (conio->hout != INVALID_HANDLE_VALUE)
            conio->need_close_hout = true;
    }

    /*
     * Fall back from that to using the standard handles.
     * (For prompt output, some callers use STD_ERROR_HANDLE rather
     * than STD_OUTPUT_HANDLE, because that has a better chance of
     * separating them from session output.)
     */
    if (conio->hin == INVALID_HANDLE_VALUE)
        conio->hin = GetStdHandle(STD_INPUT_HANDLE);
    if (conio->hout == INVALID_HANDLE_VALUE)
        conio->hout = GetStdHandle(fallback_output);

    DWORD dummy;
    conio->hin_is_console = GetConsoleMode(conio->hin, &dummy);
    conio->hout_is_console = GetConsoleMode(conio->hout, &dummy);

    BinarySink_INIT(conio, console_write);

    return conio;
}

static void conio_free(ConsoleIO *conio)
{
    if (conio->need_close_hin)
        CloseHandle(conio->hin);
    if (conio->need_close_hout)
        CloseHandle(conio->hout);
    sfree(conio);
}

static void console_write(BinarySink *bs, const void *data, size_t len)
{
    ConsoleIO *conio = BinarySink_DOWNCAST(bs, ConsoleIO);

    if (conio->utf8) {
        /*
         * Convert the UTF-8 input into a wide string.
         */
        size_t wlen;
        wchar_t *wide = dup_mb_to_wc_c(CP_UTF8, data, len, &wlen);
        if (conio->hout_is_console) {
            /*
             * To write UTF-8 to a console, use WriteConsoleW on the
             * wide string we've just made.
             */
            size_t pos = 0;
            DWORD nwritten;

            while (pos < wlen && WriteConsoleW(conio->hout, wide+pos, wlen-pos,
                                               &nwritten, NULL))
                pos += nwritten;
        } else {
            /*
             * To write a string encoded in UTF-8 to any other file
             * handle, the best we can do is to convert it into the
             * system code page. This will lose some characters, but
             * what else can you do?
             */
            size_t clen;
            char *sys_cp = dup_wc_to_mb_c(CP_ACP, wide, wlen, "?", &clen);
            size_t pos = 0;
            DWORD nwritten;

            while (pos < clen && WriteFile(conio->hout, sys_cp+pos, clen-pos,
                                           &nwritten, NULL))
                pos += nwritten;

            burnstr(sys_cp);
        }

        burnwcs(wide);
    } else {
        /*
         * If we're in legacy non-UTF-8 mode, just send the bytes
         * we're given to the file handle without trying to be clever.
         */
        const char *cdata = (const char *)data;
        size_t pos = 0;
        DWORD nwritten;

        while (pos < len && WriteFile(conio->hout, cdata+pos, len-pos,
                                      &nwritten, NULL))
            pos += nwritten;
    }
}

static bool console_read_line_to_strbuf(ConsoleIO *conio, bool echo,
                                        strbuf *sb)
{
    DWORD savemode;

    if (conio->hin_is_console) {
        GetConsoleMode(conio->hin, &savemode);
        DWORD newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
        if (!echo)
            newmode &= ~ENABLE_ECHO_INPUT;
        else
            newmode |= ENABLE_ECHO_INPUT;

        SetConsoleMode(conio->hin, newmode);
    }

    bool toret = false;

    while (true) {
        if (ptrlen_endswith(ptrlen_from_strbuf(sb),
                            PTRLEN_LITERAL("\n"), NULL)) {
            toret = true;
            goto out;
        }

        if (conio->utf8) {
            wchar_t wbuf[4097];
            size_t wlen;

            if (conio->hin_is_console) {
                /*
                 * To read UTF-8 from a console, read wide character data
                 * via ReadConsoleW, and convert it to UTF-8.
                 */
                DWORD nread;
                if (!ReadConsoleW(conio->hin, wbuf, lenof(wbuf), &nread, NULL))
                    goto out;
                wlen = nread;
            } else {
                /*
                 * To read UTF-8 from an ordinary file handle, read it
                 * as normal bytes and then convert from CP_ACP to
                 * UTF-8, in the reverse of what we did above for
                 * output.
                 */
                char buf[4096];
                DWORD nread;
                if (!ReadFile(conio->hin, buf, lenof(buf), &nread, NULL))
                    goto out;

                buffer_sink bs[1];
                buffer_sink_init(bs, wbuf, sizeof(wbuf) - sizeof(wchar_t));
                put_mb_to_wc(bs, CP_ACP, buf, nread);
                assert(!bs->overflowed);
                wlen = (wchar_t *)bs->out - wbuf;
                smemclr(buf, sizeof(buf));
            }

            put_wc_to_mb(sb, CP_UTF8, wbuf, wlen, "");
            smemclr(wbuf, sizeof(wbuf));
        } else {
            /*
             * If we're in legacy non-UTF-8 mode, just read bytes
             * directly from the file handle into the output strbuf.
             */
            char buf[4096];
            DWORD nread;
            if (!ReadFile(conio->hin, buf, lenof(buf), &nread, NULL))
                goto out;

            put_data(sb, buf, nread);
            smemclr(buf, sizeof(buf));
        }
    }

  out:
    if (!echo)
        put_datalit(conio, "\r\n");
    if (conio->hin_is_console)
        SetConsoleMode(conio->hin, savemode);
    return toret;
}

static char *console_read_line(ConsoleIO *conio, bool echo)
{
    strbuf *sb = strbuf_new_nm();
    if (!console_read_line_to_strbuf(conio, echo, sb)) {
        strbuf_free(sb);
        return NULL;
    } else {
        return strbuf_to_str(sb);
    }
}

typedef enum {
    RESPONSE_ABANDON,
    RESPONSE_YES,
    RESPONSE_NO,
    RESPONSE_INFO,
    RESPONSE_UNRECOGNISED
} ResponseType;

static ResponseType parse_and_free_response(char *line)
{
    if (!line)
        return RESPONSE_ABANDON;

    ResponseType toret;
    switch (line[0]) {
        /* In case of misplaced reflexes from another program,
         * recognise 'q' as 'abandon connection' as well as the
         * advertised 'just press Return' */
      case 'q':
      case 'Q':
      case '\n':
      case '\r':
      case '\0':
        toret = RESPONSE_ABANDON;
        break;
      case 'y':
      case 'Y':
        toret = RESPONSE_YES;
        break;
      case 'n':
      case 'N':
        toret = RESPONSE_NO;
        break;
      case 'i':
      case 'I':
        toret = RESPONSE_INFO;
        break;
      default:
        toret = RESPONSE_UNRECOGNISED;
        break;
    }

    burnstr(line);
    return toret;
}

/*
 * Helper function to print the message from a SeatDialogText. Returns
 * the final prompt to print on the input line, or NULL if a
 * batch-mode abort is needed. In the latter case it will have printed
 * the abort text already.
 */
static const char *console_print_seatdialogtext(
    ConsoleIO *conio, SeatDialogText *text)
{
    const char *prompt = NULL;

    for (SeatDialogTextItem *item = text->items,
             *end = item+text->nitems; item < end; item++) {
        switch (item->type) {
          case SDT_PARA:
            wordwrap(BinarySink_UPCAST(conio),
                     ptrlen_from_asciz(item->text), 60);
            put_byte(conio, '\n');
            break;
          case SDT_DISPLAY:
            put_fmt(conio, "  %s\n", item->text);
            break;
          case SDT_SCARY_HEADING:
            /* Can't change font size or weight in this context */
            put_fmt(conio, "%s\n", item->text);
            break;
          case SDT_BATCH_ABORT:
            if (console_batch_mode) {
                put_fmt(conio, "%s\n", item->text);
                return NULL;
            }
            break;
          case SDT_PROMPT:
            prompt = item->text;
            break;
          default:
            break;
        }
    }
    assert(prompt); /* something in the SeatDialogText should have set this */
    return prompt;
}

SeatPromptResult console_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    ConsoleIO *conio = conio_setup(false, STD_ERROR_HANDLE);
    SeatPromptResult result;

    const char *prompt = console_print_seatdialogtext(conio, text);
    if (!prompt) {
        result = SPR_SW_ABORT("Cannot confirm a host key in batch mode");
        goto out;
    }

    ResponseType response;

    while (true) {
        put_fmt(conio, "%s (y/n, Return cancels connection, i for more info) ",
                prompt);

        response = parse_and_free_response(console_read_line(conio, true));

        if (response == RESPONSE_INFO) {
            for (SeatDialogTextItem *item = text->items,
                     *end = item+text->nitems; item < end; item++) {
                switch (item->type) {
                  case SDT_MORE_INFO_KEY:
                    put_dataz(conio, item->text);
                    break;
                  case SDT_MORE_INFO_VALUE_SHORT:
                    put_fmt(conio, ": %s\n", item->text);
                    break;
                  case SDT_MORE_INFO_VALUE_BLOB:
                    put_fmt(conio, ":\n%s\n", item->text);
                    break;
                  default:
                    break;
                }
            }
        } else {
            break;
        }
    }

    if (response == RESPONSE_YES || response == RESPONSE_NO) {
        if (response == RESPONSE_YES)
            store_host_key(seat, host, port, keytype, keystr);
        result = SPR_OK;
    } else {
        put_dataz(conio, console_abandoned_msg);
        result = SPR_USER_ABORT;
    }
  out:
    conio_free(conio);
    return result;
}

SeatPromptResult console_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    ConsoleIO *conio = conio_setup(false, STD_ERROR_HANDLE);
    SeatPromptResult result;

    const char *prompt = console_print_seatdialogtext(conio, text);
    if (!prompt) {
        result = SPR_SW_ABORT("Cannot confirm a weak crypto primitive "
                              "in batch mode");
        goto out;
    }

    put_fmt(conio, "%s (y/n) ", prompt);

    ResponseType response = parse_and_free_response(
        console_read_line(conio, true));

    if (response == RESPONSE_YES) {
        result = SPR_OK;
    } else {
        put_dataz(conio, console_abandoned_msg);
        result = SPR_USER_ABORT;
    }
  out:
    conio_free(conio);
    return result;
}

SeatPromptResult console_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    ConsoleIO *conio = conio_setup(false, STD_ERROR_HANDLE);
    SeatPromptResult result;

    const char *prompt = console_print_seatdialogtext(conio, text);
    if (!prompt)
        return SPR_SW_ABORT("Cannot confirm a weak cached host key "
                            "in batch mode");

    put_fmt(conio, "%s (y/n) ", prompt);

    ResponseType response = parse_and_free_response(
        console_read_line(conio, true));

    if (response == RESPONSE_YES) {
        result = SPR_OK;
    } else {
        put_dataz(conio, console_abandoned_msg);
        result = SPR_USER_ABORT;
    }

    conio_free(conio);
    return result;
}

bool is_interactive(void)
{
    return is_console_handle(GetStdHandle(STD_INPUT_HANDLE));
}

bool console_antispoof_prompt = true;

void console_set_trust_status(Seat *seat, bool trusted)
{
    /* Do nothing in response to a change of trust status, because
     * there's nothing we can do in a console environment. However,
     * the query function below will make a fiddly decision about
     * whether to tell the backend to enable fallback handling. */
}

bool console_can_set_trust_status(Seat *seat)
{
    if (console_batch_mode) {
        /*
         * In batch mode, we don't need to worry about the server
         * mimicking our interactive authentication, because the user
         * already knows not to expect any.
         */
        return true;
    }

    return false;
}

bool console_has_mixed_input_stream(Seat *seat)
{
    if (!is_interactive() || !console_antispoof_prompt) {
        /*
         * If standard input isn't connected to a terminal, then even
         * if the server did send a spoof authentication prompt, the
         * user couldn't respond to it via the terminal anyway.
         *
         * We also pretend this is true if the user has purposely
         * disabled the antispoof prompt.
         */
        return false;
    }

    return true;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int console_askappend(LogPolicy *lp, Filename *filename,
                      void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msgtemplate[] =
        "The session log file \"%.*s\" already exists.\n"
        "You can overwrite it with a new session log,\n"
        "append your session log to the end of it,\n"
        "or disable session logging for this session.\n"
        "Enter \"y\" to wipe the file, \"n\" to append to it,\n"
        "or just press Return to disable logging.\n"
        "Wipe the log file? (y/n, Return cancels logging) ";

    static const char msgtemplate_batch[] =
        "The session log file \"%.*s\" already exists.\n"
        "Logging will not be enabled.\n";

    ConsoleIO *conio = conio_setup(true, STD_ERROR_HANDLE);
    int result;

    if (console_batch_mode) {
        put_fmt(conio, msgtemplate_batch, FILENAME_MAX, filename->utf8path);
        result = 0;
        goto out;
    }
    put_fmt(conio, msgtemplate, FILENAME_MAX, filename->utf8path);

    ResponseType response = parse_and_free_response(
        console_read_line(conio, true));

    if (response == RESPONSE_YES)
        result = 2;
    else if (response == RESPONSE_NO)
        result = 1;
    else
        result = 0;
  out:
    conio_free(conio);
    return result;
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
        "You are loading an SSH-2 private key which has an\n"
        "old version of the file format. This means your key\n"
        "file is not fully tamperproof. Future versions of\n"
        "PuTTY may stop supporting this private key format,\n"
        "so we recommend you convert your key to the new\n"
        "format.\n"
        "\n"
        "Once the key is loaded into PuTTYgen, you can perform\n"
        "this conversion simply by saving it again.\n";

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

void console_logging_error(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    fprintf(stderr, "%s\n", string);
    fflush(stderr);
}

void console_eventlog(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    if (lp_verbose(lp))
        console_logging_error(lp, string);
}

StripCtrlChars *console_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new(bs_out, false, 0);
}

SeatPromptResult console_get_userpass_input(prompts_t *p)
{
    ConsoleIO *conio = conio_setup(p->utf8, STD_OUTPUT_HANDLE);
    SeatPromptResult result;
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
        if (console_batch_mode) {
            result = SPR_SW_ABORT("Cannot answer interactive prompts "
                                  "in batch mode");
            goto out;
        }
    }

    /*
     * Preamble.
     */
    /* We only print the `name' caption if we have to... */
    if (p->name_reqd && p->name) {
        ptrlen plname = ptrlen_from_asciz(p->name);
        put_datapl(conio, plname);
        if (!ptrlen_endswith(plname, PTRLEN_LITERAL("\n"), NULL))
            put_datalit(conio, "\n");
    }
    /* ...but we always print any `instruction'. */
    if (p->instruction) {
        ptrlen plinst = ptrlen_from_asciz(p->instruction);
        put_datapl(conio, plinst);
        if (!ptrlen_endswith(plinst, PTRLEN_LITERAL("\n"), NULL))
            put_datalit(conio, "\n");
    }

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {
        prompt_t *pr = p->prompts[curr_prompt];

        put_dataz(conio, pr->prompt);

        if (!console_read_line_to_strbuf(conio, pr->echo, pr->result)) {
            result = make_spr_sw_abort_winerror(
                "Error reading from console", GetLastError());
            goto out;
        } else if (!pr->result->len) {
            /* Regard EOF on the terminal as a deliberate user-abort */
            result = SPR_USER_ABORT;
            goto out;
        } else {
            if (strbuf_chomp(pr->result, '\n')) {
                strbuf_chomp(pr->result, '\r');
            }
        }
    }

    result = SPR_OK;
  out:
    conio_free(conio);
    return result;
}
