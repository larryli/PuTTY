/*
 * gtkmain.c: the common main-program code between the straight-up
 * Unix PuTTY and pterm, which they do not share with the
 * multi-session gtkapp.c.
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
#include "gtkfont.h"
#include "gtkmisc.h"

#ifndef NOT_X_WINDOWS
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#endif

static char *progname, **gtkargvstart;
static int ngtkargs;

extern char **pty_argv;	       /* declared in pty.c */
extern int use_pty_argv;

static const char *app_name = "pterm";

char *x_get_default(const char *key)
{
#ifndef NOT_X_WINDOWS
    return XGetDefault(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                       app_name, key);
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
    n = 2;			       /* progname and terminating NULL */
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
    int i, ret, sersize, size;
    char *data;
    char option[80];
    int pipefd[2];

    if (pipe(pipefd) < 0) {
	perror("pipe");
	return;
    }

    size = sersize = conf_serialised_size(conf);
    if (use_pty_argv && pty_argv) {
	for (i = 0; pty_argv[i]; i++)
	    size += strlen(pty_argv[i]) + 1;
    }

    data = snewn(size, char);
    conf_serialise(conf, data);
    if (use_pty_argv && pty_argv) {
	int p = sersize;
	for (i = 0; pty_argv[i]; i++) {
	    strcpy(data + p, pty_argv[i]);
	    p += strlen(pty_argv[i]) + 1;
	}
	assert(p == size);
    }

    sprintf(option, "---[%d,%d]", pipefd[0], size);
    noncloexec(pipefd[0]);
    fork_and_exec_self(pipefd[1], option, NULL);
    close(pipefd[0]);

    i = ret = 0;
    while (i < size && (ret = write(pipefd[1], data + i, size - i)) > 0)
	i += ret;
    if (ret < 0)
	perror("write to pipe");
    close(pipefd[1]);
    sfree(data);
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
    int fd, i, ret, size, size_used;
    char *data;

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

    size_used = conf_deserialise(conf, data, size);
    if (use_pty_argv && size > size_used) {
	int n = 0;
	i = size_used;
	while (i < size) {
	    while (i < size && data[i]) i++;
	    if (i >= size) {
		fprintf(stderr, "%s: malformed Duplicate Session data\n",
			appname);
		exit(1);
	    }
	    i++;
	    n++;
	}
	pty_argv = snewn(n+1, char *);
	pty_argv[n] = NULL;
	n = 0;
	i = size_used;
	while (i < size) {
	    char *p = data + i;
	    while (i < size && data[i]) i++;
	    assert(i < size);
	    i++;
	    pty_argv[n++] = dupstr(p);
	}
    }

    sfree(data);

    return 0;
}

static void help(FILE *fp) {
    if(fprintf(fp,
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
    if(fprintf(fp, "%s: %s\n%s\n", appname, ver, buildinfo_text) < 0 ||
       fflush(fp) < 0) {
	perror("output error");
	exit(1);
    }
    sfree(buildinfo_text);
}

static struct gui_data *the_inst;

static const char *geometry_string;

int do_cmdline(int argc, char **argv, int do_everything, int *allow_launch,
               Conf *conf)
{
    int err = 0;
    char *val;

    /*
     * Macros to make argument handling easier. Note that because
     * they need to call `continue', they cannot be contained in
     * the usual do {...} while (0) wrapper to make them
     * syntactically single statements; hence it is not legal to
     * use one of these macros as an unbraced statement between
     * `if' and `else'.
     */
#define EXPECTS_ARG { \
    if (--argc <= 0) { \
	err = 1; \
	fprintf(stderr, "%s: %s expects an argument\n", appname, p); \
        continue; \
    } else \
	val = *++argv; \
}
#define SECOND_PASS_ONLY { if (!do_everything) continue; }

    while (--argc > 0) {
	const char *p = *++argv;
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

        ret = cmdline_process_param(p, (argc > 1 ? argv[1] : NULL),
                                    do_everything ? 1 : -1, conf);

	if (ret == -2) {
	    cmdline_error("option \"%s\" requires an argument", p);
	} else if (ret == 2) {
	    --argc, ++argv;            /* skip next argument */
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
                int success = gdk_rgba_parse(&rgba, val);
#else
                GdkColor col;
                int success = gdk_color_parse(val, &col);
#endif

                if (!success) {
                    err = 1;
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

	    if (--argc > 0) {
		int i;
		pty_argv = snewn(argc+1, char *);
		++argv;
		for (i = 0; i < argc; i++)
		    pty_argv[i] = argv[i];
		pty_argv[argc] = NULL;
		break;		       /* finished command-line processing */
	    } else
		err = 1, fprintf(stderr, "%s: -e expects an argument\n",
                                 appname);

	} else if (!strcmp(p, "-title")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    conf_set_str(conf, CONF_wintitle, val);

	} else if (!strcmp(p, "-log")) {
	    Filename *fn;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fn = filename_from_str(val);
	    conf_set_filename(conf, CONF_logfilename, fn);
	    conf_set_int(conf, CONF_logtype, LGTYP_DEBUG);
            filename_free(fn);

	} else if (!strcmp(p, "-ut-") || !strcmp(p, "+ut")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_stamp_utmp, 0);

	} else if (!strcmp(p, "-ut")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_stamp_utmp, 1);

	} else if (!strcmp(p, "-ls-") || !strcmp(p, "+ls")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_login_shell, 0);

	} else if (!strcmp(p, "-ls")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_login_shell, 1);

	} else if (!strcmp(p, "-nethack")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_nethack_keypad, 1);

	} else if (!strcmp(p, "-sb-") || !strcmp(p, "+sb")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_scrollbar, 0);

	} else if (!strcmp(p, "-sb")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_scrollbar, 1);

	} else if (!strcmp(p, "-name")) {
	    EXPECTS_ARG;
	    app_name = val;

	} else if (!strcmp(p, "-xrm")) {
	    EXPECTS_ARG;
	    provide_xrm_string(val);

	} else if(!strcmp(p, "-help") || !strcmp(p, "--help")) {
	    help(stdout);
	    exit(0);

	} else if(!strcmp(p, "-version") || !strcmp(p, "--version")) {
	    version(stdout);
	    exit(0);

        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(1);

	} else if(p[0] != '-' && (!do_everything ||
                                  process_nonoption_arg(p, conf,
							allow_launch))) {
            /* do nothing */

	} else {
	    err = 1;
	    fprintf(stderr, "%s: unrecognized option '%s'\n", appname, p);
	}
    }

    return err;
}

GtkWidget *make_gtk_toplevel_window(void *frontend)
{
    return gtk_window_new(GTK_WINDOW_TOPLEVEL);
}

extern int cfgbox(Conf *conf);

const int buildinfo_gtk_relevant = TRUE;

int main(int argc, char **argv)
{
    Conf *conf;
    int need_config_box;

    setlocale(LC_CTYPE, "");

    {
        /* Call the function in ux{putty,pterm}.c to do app-type
         * specific setup */
        extern void setup(int);
        setup(TRUE);     /* TRUE means we are a one-session process */
    }

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
    block_signal(SIGPIPE, 1);

    if (argc > 1 && !strncmp(argv[1], "---", 3)) {
        extern const int dup_check_launchable;

	read_dupsession_data(conf, argv[1]);
	/* Splatter this argument so it doesn't clutter a ps listing */
	smemclr(argv[1], strlen(argv[1]));

        assert(!dup_check_launchable || conf_launchable(conf));
        need_config_box = FALSE;
    } else {
	/* By default, we bring up the config dialog, rather than launching
	 * a session. This gets set to TRUE if something happens to change
	 * that (e.g., a hostname is specified on the command-line). */
	int allow_launch = FALSE;
	if (do_cmdline(argc, argv, 0, &allow_launch, conf))
	    exit(1);		       /* pre-defaults pass to get -class */
	do_defaults(NULL, conf);
	if (do_cmdline(argc, argv, 1, &allow_launch, conf))
	    exit(1);		       /* post-defaults, do everything */

	cmdline_run_saved(conf);

	if (loaded_session)
	    allow_launch = TRUE;

        need_config_box = (!allow_launch || !conf_launchable(conf));
    }

    /*
     * Put up the config box.
     */
    if (need_config_box && !cfgbox(conf))
        exit(0);		       /* config box hit Cancel */

    /*
     * Create the main session window. We don't really need to keep
     * the return value - the fact that it'll be linked from a zillion
     * GTK and glib bits and bobs known to the main loop will be
     * sufficient to make everything actually happen - but we stash it
     * in a global variable anyway, so that it'll be easy to find in a
     * debugger.
     */
    the_inst = new_session_window(conf, geometry_string);

    gtk_main();

    return 0;
}
