/*
 * Unix PuTTY main program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "putty.h"
#include "storage.h"

/*
 * TODO:
 * 
 *  - Fix command-line parsing to be more PuTTYlike and not so
 *    ptermy - in particular non-option arguments should be
 *    hostname and port in the obvious way.
 * 
 *  - libcharset enumeration.
 * 
 *  - fix the printer enum (I think the sensible thing is simply to
 *    have uxcfg.c remove the drop-down list completely, since you
 *    can't sensibly provide an enumerated list of lpr commands!).
 * 
 *  - Ctrl+right-click for a context menu (also in Windows for
 *    consistency, I think). This should contain pretty much
 *    everything in the Windows PuTTY menu, and a subset of that in
 *    pterm:
 * 
 *     - Telnet special commands (not in pterm :-)
 * 
 *     - Event Log (this means we must implement the Event Log; not
 *       in pterm)
 * 
 *     - New Session and Duplicate Session (perhaps in pterm, in fact?!)
 *        + Duplicate Session will be fun, since we must work out
 *          how to pass the config data through.
 *        + In fact this should be easier on Unix, since fork() is
 *          available so we need not even exec (this also saves us
 *          the trouble of scrabbling around trying to find our own
 *          binary). Possible scenario: respond to Duplicate
 *          Session by forking. Parent continues as before; child
 *          unceremoniously frees all extant resources (backend,
 *          terminal, ldisc, frontend etc) and then _longjmps_ (I
 *          kid you not) back to a point in pt_main() which causes
 *          it to go back round to the point of opening a new
 *          terminal window and a new backend.
 *        + A tricky bit here is how to free everything without
 *          also _destroying_ things - calling GTK to free up
 *          existing widgets is liable to send destroy messages to
 *          the X server, which won't go down too well with the
 *          parent process. exec() is a much cleaner solution to
 *          this bit, but requires us to invent some ghastly IPC as
 *          we did in Windows PuTTY.
 *        + Arrgh! Also, this won't work in pterm since we'll
 *          already have dropped privileges by this point, so we
 *          can't get another pty. Sigh. Looks like exec has to be
 *          the way forward then :-/
 * 
 *     - Saved Sessions submenu (not in pterm of course)
 * 
 *     - Change Settings
 *        + we must also implement mid-session reconfig in pterm.c.
 *        + note this also requires config.c and uxcfg.c to be able
 *          to get hold of the application name.
 * 
 *     - Copy All to Clipboard (for what that's worth)
 * 
 *     - Clear Scrollback and Reset Terminal
 * 
 *     - About (and uxcfg.c must also supply the about box)
 */

void cmdline_error(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "plink: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

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
 * Another bunch of temporary stub functions. These ones will want
 * removing by means of implementing them properly: libcharset
 * should invent its own sensible format for codepage names and a
 * means of enumerating them, and printer_enum needs to be dealt
 * with somehow or other too.
 */

char *cp_name(int codepage)
{
    return "";
}
char *cp_enumerate(int index)
{
    return NULL;
}
int decode_codepage(char *cp_name)
{
    return -2;
}

printer_enum *printer_start_enum(int *nprinters_ptr) {
    *nprinters_ptr = 0;
    return NULL;
}
char *printer_get_name(printer_enum *pe, int i) { return NULL;
}
void printer_finish_enum(printer_enum *pe) { }

Backend *select_backend(Config *cfg)
{
    int i;
    Backend *back = NULL;
    for (i = 0; backends[i].backend != NULL; i++)
	if (backends[i].protocol == cfg->protocol) {
	    back = backends[i].backend;
	    break;
	}
    assert(back != NULL);
    return back;
}

int cfgbox(Config *cfg)
{
    extern int do_config_box(const char *title, Config *cfg);
    return do_config_box("PuTTY Configuration", cfg);
}

char *make_default_wintitle(char *hostname)
{
    return dupcat(hostname, " - PuTTY", NULL);
}

int main(int argc, char **argv)
{
    extern int pt_main(int argc, char **argv);
    sk_init();
    flags = FLAG_VERBOSE | FLAG_INTERACTIVE;
    default_protocol = be_default_protocol;
    /* Find the appropriate default port. */
    {
	int i;
	default_port = 0; /* illegal */
	for (i = 0; backends[i].backend != NULL; i++)
	    if (backends[i].protocol == default_protocol) {
		default_port = backends[i].backend->default_port;
		break;
	    }
    }
    return pt_main(argc, argv);
}
