/*
 * main-gtk-simple.c: the common main-program code between the
 * straight-up Unix PuTTY and pterm, which they do not share with the
 * multi-session main-gtk-application.c.
 */

#define _GNU_SOURCE

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(3,0,0)
#include <gdk/gdkkeysyms.h>
#endif

#if GTK_CHECK_VERSION(2,0,0)
#include <gtk/gtkimmodule.h>
#endif

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "terminal.h"
#include "gtkcompat.h"
#include "unifont.h"
#include "gtkmisc.h"

#ifndef NOT_X_WINDOWS
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "x11misc.h"
#endif

static char *progname, **gtkargvstart;
static int ngtkargs;

static const char *app_name = "pterm";

char *x_get_default(const char *key)
{
#ifndef NOT_X_WINDOWS
    Display *disp;
    if ((disp = get_x11_display()) == NULL)
        return NULL;
    return XGetDefault(disp, app_name, key);
#else
    return NULL;
#endif
}

void fork_and_exec_self(int fd_to_close, ...)
{
    /*
     * Re-execing ourself is not an exact science under Unix. I do
     * the best I can by using /proc/self/exe if available and by
     * assuming argv[0] can be found on $PATH if not.
     *
     * Note that we also have to reconstruct the elements of the
     * original argv which gtk swallowed, since the user wants the
     * new session to appear on the same X display as the old one.
     */
    char **args;
    va_list ap;
    int i, n;
    int pid;

    /*
     * Collect the arguments with which to re-exec ourself.
     */
    va_start(ap, fd_to_close);
    n = 2;                             /* progname and terminating NULL */
    n += ngtkargs;
    while (va_arg(ap, char *) != NULL)
        n++;
    va_end(ap);

    args = snewn(n, char *);
    args[0] = progname;
    args[n-1] = NULL;
    for (i = 0; i < ngtkargs; i++)
        args[i+1] = gtkargvstart[i];

    i++;
    va_start(ap, fd_to_close);
    while ((args[i++] = va_arg(ap, char *)) != NULL);
    va_end(ap);

    assert(i == n);

    /*
     * Do the double fork.
     */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        sfree(args);
        return;
    }

    if (pid == 0) {
        int pid2 = fork();
        if (pid2 < 0) {
            perror("fork");
            _exit(1);
        } else if (pid2 > 0) {
            /*
             * First child has successfully forked second child. My
             * Work Here Is Done. Note the use of _exit rather than
             * exit: the latter appears to cause destroy messages
             * to be sent to the X server. I suspect gtk uses
             * atexit.
             */
            _exit(0);
        }

        /*
         * If we reach here, we are the second child, so we now
         * actually perform the exec.
         */
        if (fd_to_close >= 0)
            close(fd_to_close);

        execv("/proc/self/exe", args);
        execvp(progname, args);
        perror("exec");
        _exit(127);

    } else {
        int status;
        sfree(args);
        waitpid(pid, &status, 0);
    }

}

void launch_duplicate_session(Conf *conf)
{
    /*
     * For this feature we must marshal conf and (possibly) pty_argv
     * into a byte stream, create a pipe, and send this byte stream
     * to the child through the pipe.
     */
    int i, ret;
    strbuf *serialised;
    char option[80];
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    serialised = strbuf_new();

    conf_serialise(BinarySink_UPCAST(serialised), conf);
    if (use_pty_argv && pty_argv)
        for (i = 0; pty_argv[i]; i++)
            put_asciz(serialised, pty_argv[i]);

    sprintf(option, "---[%d,%zu]", pipefd[0], serialised->len);
    noncloexec(pipefd[0]);
    fork_and_exec_self(pipefd[1], option, NULL);
    close(pipefd[0]);

    i = ret = 0;
    while (i < serialised->len &&
           (ret = write(pipefd[1], serialised->s + i,
                        serialised->len - i)) > 0)
        i += ret;
    if (ret < 0)
        perror("write to pipe");
    close(pipefd[1]);
    strbuf_free(serialised);
}

void launch_new_session(void)
{
    fork_and_exec_self(-1, NULL);
}

void launch_saved_session(const char *str)
{
    fork_and_exec_self(-1, "-load", str, NULL);
}

int read_dupsession_data(Conf *conf, char *arg)
{
    int fd, i, ret, size;
    char *data;
    BinarySource src[1];

    if (sscanf(arg, "---[%d,%d]", &fd, &size) != 2) {
        fprintf(stderr, "%s: malformed magic argument `%s'\n", appname, arg);
        exit(1);
    }

    data = snewn(size, char);
    i = ret = 0;
    while (i < size && (ret = read(fd, data + i, size - i)) > 0)
        i += ret;
    if (ret < 0) {
        perror("read from pipe");
        exit(1);
    } else if (i < size) {
        fprintf(stderr, "%s: unexpected EOF in Duplicate Session data\n",
                appname);
        exit(1);
    }

    BinarySource_BARE_INIT(src, data, size);
    if (!conf_deserialise(conf, src)) {
        fprintf(stderr, "%s: malformed Duplicate Session data\n", appname);
        exit(1);
    }
    if (use_pty_argv) {
        int pty_argc = 0;
        size_t argv_startpos = src->pos;

        while (get_asciz(src), !get_err(src))
            pty_argc++;

        src->err = BSE_NO_ERROR;

        if (pty_argc > 0) {
            src->pos = argv_startpos;

            pty_argv = snewn(pty_argc + 1, char *);
            pty_argv[pty_argc] = NULL;
            for (i = 0; i < pty_argc; i++)
                pty_argv[i] = dupstr(get_asciz(src));
        }
    }

    if (get_err(src) || get_avail(src) > 0) {
        fprintf(stderr, "%s: malformed Duplicate Session data\n", appname);
        exit(1);
    }

    sfree(data);
    return 0;
}

static void help(FILE *fp) {
    if (fprintf(fp,
"pterm option summary:\n"
"\n"
"  --display DISPLAY         Specify X display to use (note '--')\n"
"  -name PREFIX              Prefix when looking up resources (default: pterm)\n"
"  -fn FONT                  Normal text font\n"
"  -fb FONT                  Bold text font\n"
"  -geometry GEOMETRY        Position and size of window (size in characters)\n"
"  -sl LINES                 Number of lines of scrollback\n"
"  -fg COLOUR, -bg COLOUR    Foreground/background colour\n"
"  -bfg COLOUR, -bbg COLOUR  Foreground/background bold colour\n"
"  -cfg COLOUR, -bfg COLOUR  Foreground/background cursor colour\n"
"  -T TITLE                  Window title\n"
"  -ut, +ut                  Do(default) or do not update utmp\n"
"  -ls, +ls                  Do(default) or do not make shell a login shell\n"
"  -sb, +sb                  Do(default) or do not display a scrollbar\n"
"  -log PATH, -sessionlog PATH  Log all output to a file\n"
"  -nethack                  Map numeric keypad to hjklyubn direction keys\n"
"  -xrm RESOURCE-STRING      Set an X resource\n"
"  -e COMMAND [ARGS...]      Execute command (consumes all remaining args)\n"
         ) < 0 || fflush(fp) < 0) {
        perror("output error");
        exit(1);
    }
}

static void version(FILE *fp) {
    char *buildinfo_text = buildinfo("\n");
    if (fprintf(fp, "%s: %s\n%s\n", appname, ver, buildinfo_text) < 0 ||
        fflush(fp) < 0) {
        perror("output error");
        exit(1);
    }
    sfree(buildinfo_text);
}

static const char *geometry_string;

void cmdline_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", appname);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void window_setup_error(const char *errmsg)
{
    fprintf(stderr, "%s: %s\n", appname, errmsg);
    exit(1);
}

bool do_cmdline(int argc, char **argv, bool do_everything, Conf *conf)
{
    bool err = false;

    /*
     * Macros to make argument handling easier.
     *
     * Note that because they need to call `continue', they cannot be
     * contained in the usual do {...} while (0) wrapper to make them
     * syntactically single statements. I use the alternative if (1)
     * {...} else ((void)0).
     */
#define EXPECTS_ARG if (1) {                                            \
        if (!nextarg) {                                                 \
            err = true;                                                 \
            fprintf(stderr, "%s: %s expects an argument\n", appname, p); \
            continue;                                                   \
        } else {                                                        \
            arglistpos++;                                               \
        }                                                               \
    } else ((void)0)
#define SECOND_PASS_ONLY if (1) {               \
        if (!do_everything)                     \
            continue;                           \
    } else ((void)0)

    CmdlineArgList *arglist = cmdline_arg_list_from_argv(argc, argv);
    size_t arglistpos = 0;
    while (arglist->args[arglistpos]) {
        CmdlineArg *arg = arglist->args[arglistpos++];
        CmdlineArg *nextarg = arglist->args[arglistpos];
        const char *p = cmdline_arg_to_str(arg);
        const char *val = cmdline_arg_to_str(nextarg);
        int ret;

        /*
         * Shameless cheating. Debian requires all X terminal
         * emulators to support `-T title'; but
         * cmdline_process_param will eat -T (it means no-pty) and
         * complain that pterm doesn't support it. So, in pterm
         * only, we convert -T into -title.
         */
        if ((cmdline_tooltype & TOOLTYPE_NONNETWORK) &&
            !strcmp(p, "-T"))
            p = "-title";

        ret = cmdline_process_param(
            arg, nextarg, do_everything ? 1 : -1, conf);

        if (ret == -2) {
            cmdline_error("option \"%s\" requires an argument", p);
        } else if (ret == 2) {
            arglistpos++;
            continue;
        } else if (ret == 1) {
            continue;
        }

        if (!strcmp(p, "-fn") || !strcmp(p, "-font")) {
            FontSpec *fs;
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            fs = fontspec_new(val);
            conf_set_fontspec(conf, CONF_font, fs);
            fontspec_free(fs);

        } else if (!strcmp(p, "-fb")) {
            FontSpec *fs;
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            fs = fontspec_new(val);
            conf_set_fontspec(conf, CONF_boldfont, fs);
            fontspec_free(fs);

        } else if (!strcmp(p, "-fw")) {
            FontSpec *fs;
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            fs = fontspec_new(val);
            conf_set_fontspec(conf, CONF_widefont, fs);
            fontspec_free(fs);

        } else if (!strcmp(p, "-fwb")) {
            FontSpec *fs;
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            fs = fontspec_new(val);
            conf_set_fontspec(conf, CONF_wideboldfont, fs);
            fontspec_free(fs);

        } else if (!strcmp(p, "-cs")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            conf_set_str(conf, CONF_line_codepage, val);

        } else if (!strcmp(p, "-geometry")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            geometry_string = val;
        } else if (!strcmp(p, "-sl")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            conf_set_int(conf, CONF_savelines, atoi(val));

        } else if (!strcmp(p, "-fg") || !strcmp(p, "-bg") ||
                   !strcmp(p, "-bfg") || !strcmp(p, "-bbg") ||
                   !strcmp(p, "-cfg") || !strcmp(p, "-cbg")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;

            {
#if GTK_CHECK_VERSION(3,0,0)
                GdkRGBA rgba;
                bool success = gdk_rgba_parse(&rgba, val);
#else
                GdkColor col;
                bool success = gdk_color_parse(val, &col);
#endif

                if (!success) {
                    err = true;
                    fprintf(stderr, "%s: unable to parse colour \"%s\"\n",
                            appname, val);
                } else {
#if GTK_CHECK_VERSION(3,0,0)
                    int r = rgba.red * 255;
                    int g = rgba.green * 255;
                    int b = rgba.blue * 255;
#else
                    int r = col.red / 256;
                    int g = col.green / 256;
                    int b = col.blue / 256;
#endif

                    int index;
                    index = (!strcmp(p, "-fg") ? 0 :
                             !strcmp(p, "-bg") ? 2 :
                             !strcmp(p, "-bfg") ? 1 :
                             !strcmp(p, "-bbg") ? 3 :
                             !strcmp(p, "-cfg") ? 4 :
                             !strcmp(p, "-cbg") ? 5 : -1);
                    assert(index != -1);

                    conf_set_int_int(conf, CONF_colours, index*3+0, r);
                    conf_set_int_int(conf, CONF_colours, index*3+1, g);
                    conf_set_int_int(conf, CONF_colours, index*3+2, b);
                }
            }

        } else if (use_pty_argv && !strcmp(p, "-e")) {
            /* This option swallows all further arguments. */
            if (!do_everything)
                break;

            if (nextarg) {
                pty_argv = cmdline_arg_remainder(nextarg);
                break;                 /* finished command-line processing */
            } else
                err = true, fprintf(stderr, "%s: -e expects an argument\n",
                                    appname);

        } else if (!strcmp(p, "-title")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            conf_set_str(conf, CONF_wintitle, val);

        } else if (!strcmp(p, "-log")) {
            EXPECTS_ARG;
            SECOND_PASS_ONLY;
            Filename *fn = cmdline_arg_to_filename(nextarg);
            conf_set_filename(conf, CONF_logfilename, fn);
            conf_set_int(conf, CONF_logtype, LGTYP_DEBUG);
            filename_free(fn);

        } else if (!strcmp(p, "-ut-") || !strcmp(p, "+ut")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_stamp_utmp, false);

        } else if (!strcmp(p, "-ut")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_stamp_utmp, true);

        } else if (!strcmp(p, "-ls-") || !strcmp(p, "+ls")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_login_shell, false);

        } else if (!strcmp(p, "-ls")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_login_shell, true);

        } else if (!strcmp(p, "-nethack")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_nethack_keypad, true);

        } else if (!strcmp(p, "-sb-") || !strcmp(p, "+sb")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_scrollbar, false);

        } else if (!strcmp(p, "-sb")) {
            SECOND_PASS_ONLY;
            conf_set_bool(conf, CONF_scrollbar, true);

        } else if (!strcmp(p, "-name")) {
            EXPECTS_ARG;
            app_name = val;

        } else if (!strcmp(p, "-xrm")) {
            EXPECTS_ARG;
            provide_xrm_string(val, appname);

        } else if (!strcmp(p, "-help") || !strcmp(p, "--help")) {
            help(stdout);
            exit(0);

        } else if (!strcmp(p, "-version") || !strcmp(p, "--version")) {
            version(stdout);
            exit(0);

        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(0);

        } else if (has_ca_config_box &&
                   (!strcmp(p, "-host-ca") || !strcmp(p, "--host-ca") ||
                    !strcmp(p, "-host_ca") || !strcmp(p, "--host_ca"))) {
            show_ca_config_box_synchronously();
            exit(0);

        } else if (p[0] != '-') {
            /* Non-option arguments not handled by cmdline.c are errors. */
            if (do_everything) {
                err = true;
                fprintf(stderr, "%s: unexpected non-option argument '%s'\n",
                        appname, p);
            }

        } else {
            err = true;
            fprintf(stderr, "%s: unrecognized option '%s'\n", appname, p);
        }
    }

    return err;
}

GtkWidget *make_gtk_toplevel_window(GtkFrontend *frontend)
{
    return gtk_window_new(GTK_WINDOW_TOPLEVEL);
}

const bool buildinfo_gtk_relevant = true;

struct post_initial_config_box_ctx {
    Conf *conf;
    const char *geometry_string;
};

static void post_initial_config_box(void *vctx, int result)
{
    struct post_initial_config_box_ctx ctx =
        *(struct post_initial_config_box_ctx *)vctx;
    sfree(vctx);

    if (result > 0) {
        new_session_window(ctx.conf, ctx.geometry_string);
    } else {
        /* In this main(), which only runs one session in total, a
         * negative result from the initial config box means we simply
         * terminate. */
        conf_free(ctx.conf);
        gtk_main_quit();
    }
}

void session_window_closed(void)
{
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    Conf *conf;
    bool need_config_box;

    setlocale(LC_CTYPE, "");

    /* Call the function in ux{putty,pterm}.c to do app-type
     * specific setup */
    setup(true);         /* true means we are a one-session process */

    progname = argv[0];

    /*
     * Copy the original argv before letting gtk_init fiddle with
     * it. It will be required later.
     */
    {
        int i, oldargc;
        gtkargvstart = snewn(argc-1, char *);
        for (i = 1; i < argc; i++)
            gtkargvstart[i-1] = dupstr(argv[i]);
        oldargc = argc;
        gtk_init(&argc, &argv);
        ngtkargs = oldargc - argc;
    }

    conf = conf_new();

    gtkcomm_setup();

    /*
     * Block SIGPIPE: if we attempt Duplicate Session or similar and
     * it falls over in some way, we certainly don't want SIGPIPE
     * terminating the main pterm/PuTTY. However, we'll have to
     * unblock it again when pterm forks.
     */
    block_signal(SIGPIPE, true);

    if (argc > 1 && !strncmp(argv[1], "---", 3)) {
        read_dupsession_data(conf, argv[1]);
        /* Splatter this argument so it doesn't clutter a ps listing */
        smemclr(argv[1], strlen(argv[1]));

        assert(!dup_check_launchable || conf_launchable(conf));
        need_config_box = false;
    } else {
        if (do_cmdline(argc, argv, false, conf))
            exit(1);                   /* pre-defaults pass to get -class */
        do_defaults(NULL, conf);
        if (do_cmdline(argc, argv, true, conf))
            exit(1);                   /* post-defaults, do everything */

        cmdline_run_saved(conf);

        if (cmdline_tooltype & TOOLTYPE_HOST_ARG)
            need_config_box = !cmdline_host_ok(conf);
        else
            need_config_box = false;
    }

    if (need_config_box) {
        /*
         * Put up the initial config box, which will pass the provided
         * parameters (with conf updated) to new_session_window() when
         * (if) the user selects Open. Or it might close without
         * creating a session window, if the user selects Cancel. Or
         * it might just create the session window immediately if this
         * is a pterm-style app which doesn't have an initial config
         * box at all.
         */
        struct post_initial_config_box_ctx *ctx =
            snew(struct post_initial_config_box_ctx);
        ctx->conf = conf;
        ctx->geometry_string = geometry_string;
        initial_config_box(conf, post_initial_config_box, ctx);
    } else {
        /*
         * No initial config needed; just create the session window
         * now.
         */
        new_session_window(conf, geometry_string);
    }

    gtk_main();

    return 0;
}
