/*
 * Unix PuTTY main program.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "putty.h"
#include "storage.h"

/*
 * TODO:
 * 
 *  - Remainder of the context menu:
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
 */

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

static int got_host = 0;

int process_nonoption_arg(char *arg, Config *cfg)
{
    char *p, *q = arg;

    if (got_host) {
        /*
         * If we already have a host name, treat this argument as a
         * port number. NB we have to treat this as a saved -P
         * argument, so that it will be deferred until it's a good
         * moment to run it.
         */
        int ret = cmdline_process_param("-P", arg, 1, cfg);
        assert(ret == 2);
    } else if (!strncmp(q, "telnet:", 7)) {
        /*
         * If the hostname starts with "telnet:",
         * set the protocol to Telnet and process
         * the string as a Telnet URL.
         */
        char c;

        q += 7;
        if (q[0] == '/' && q[1] == '/')
            q += 2;
        cfg->protocol = PROT_TELNET;
        p = q;
        while (*p && *p != ':' && *p != '/')
            p++;
        c = *p;
        if (*p)
            *p++ = '\0';
        if (c == ':')
            cfg->port = atoi(p);
        else
            cfg->port = -1;
        strncpy(cfg->host, q, sizeof(cfg->host) - 1);
        cfg->host[sizeof(cfg->host) - 1] = '\0';
        got_host = 1;
    } else {
        /*
         * Otherwise, treat this argument as a host name.
         */
        p = arg;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = '\0';
        strncpy(cfg->host, q, sizeof(cfg->host) - 1);
        cfg->host[sizeof(cfg->host) - 1] = '\0';
        got_host = 1;
    }
    return 1;
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
