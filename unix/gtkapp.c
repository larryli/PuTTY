/*
 * gtkapp.c: a top-level front end to GUI PuTTY and pterm, using
 * GtkApplication. Suitable for OS X. Currently unfinished.
 *
 * (You could run it on ordinary Linux GTK too, in principle, but I
 * don't think it would be particularly useful to do so, even once
 * it's fully working.)
 */

/*

To build on OS X, you will need a build environment with GTK 3 and
gtk-mac-bundler, and also Halibut on the path (to build the man pages,
without which the standard Makefile will complain). Then, from a clean
checkout, do this:

./mkfiles.pl -U --with-quartz
make -C icons icns
make -C doc
make

and you should get unix/PuTTY.app and unix/PTerm.app as output.

*/

/*

TODO list for a sensible GTK3 PuTTY/pterm on OS X:

Menu items' keyboard shortcuts (Command-Q for Quit, Command-V for
Paste) do not currently work. It's intentional that if you turn on
'Command key acts as Meta' in the configuration then those shortcuts
should be superseded by the Meta-key functionality (e.g. Cmd-Q should
send ESC Q to the session), for the benefit of people whose non-Mac
keyboard reflexes expect the Meta key to be in that position; but if
you don't turn that option on, then these shortcuts should work as an
ordinary Mac user expects, and currently they don't.

Mouse wheel events and trackpad scrolling gestures don't work quite
right in the terminal drawing area.

There doesn't seem to be a resize handle on terminal windows. I don't
think this is a fundamental limitation of OS X GTK (their demo app has
one), so perhaps I need to do something to make sure it appears?

A slight oddity with menus that pop up directly under the mouse
pointer: mousing over the menu items doesn't highlight them initially,
but if I mouse off the menu and back on (without un-popping-it-up)
then suddenly that does work. I don't know if this is something I can
fix, though; it might very well be a quirk of the underlying GTK.

I want to arrange *some* way to paste efficiently using my Apple
wireless keyboard and trackpad. The trackpad doesn't provide a middle
button; I can't use the historic Shift-Ins shortcut because the
keyboard has no Ins key; I configure the Command key to be Meta, so
Command-V is off the table too. I can always use the menu, but I'd
prefer there to be _some_ easily reachable mouse or keyboard gesture.

Revamping the clipboard handling in general is going to be needed, as
well. Not everybody will want the current auto-copy-on-select
behaviour inherited from ordinary Unix PuTTY. Should arrange to have a
mode in which you have to take an explicit Copy action, and then
arrange that the Edit menu includes one of those.

The application menu bar is very minimal at the moment. Should include
all the usual stuff from the Ctrl-right-click menu - saved sessions,
mid-session special commands, Duplicate Session, Change Settings,
Event Log, clear scrollback, reset terminal, about box, anything else
I can think of.

Does OS X have a standard system of online help that I could tie into?

Need to work out what if anything we can do with Pageant on OS X.
Perhaps it's too much bother and we should just talk to the
system-provided SSH agent? Or perhaps not.

Nice-to-have: a custom right-click menu from the application's dock
tile, listing the saved sessions for quick launch. As far as I know
there's nothing built in to GtkApplication that can produce this, but
it's possible we might be able to drop a piece of native Cocoa code in
under ifdef, substituting an application delegate of our own which
forwards all methods we're not interested in to the GTK-provided one?

At the point where this becomes polished enough to publish pre-built,
I suppose I'll have to look into OS X code signing.
https://wiki.gnome.org/Projects/GTK%2B/OSX/Bundling has some links.

 */

#include <assert.h>
#include <stdlib.h>

#include <unistd.h>

#include <gtk/gtk.h>

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "gtkmisc.h"

char *x_get_default(const char *key) { return NULL; }

const int buildinfo_gtk_relevant = TRUE;

#if !GTK_CHECK_VERSION(3,0,0)
/* This front end only works in GTK 3. If that's not what we've got,
 * it's easier to just turn this program into a trivial stub by ifdef
 * in the source than it is to remove it in the makefile edifice. */
int main(int argc, char **argv)
{
    fprintf(stderr, "GtkApplication frontend doesn't work pre-GTK3\n");
    return 1;
}
GtkWidget *make_gtk_toplevel_window(void *frontend) { return NULL; }
void launch_duplicate_session(Conf *conf) {}
void launch_new_session(void) {}
void launch_saved_session(const char *str) {}
void session_window_closed(void) {}
#else /* GTK_CHECK_VERSION(3,0,0) */

static void startup(GApplication *app, gpointer user_data)
{
    GMenu *menubar, *menu, *section;

    menubar = g_menu_new();

    menu = g_menu_new();
    g_menu_append_submenu(menubar, "File", G_MENU_MODEL(menu));

    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append(section, "New Window", "app.newwin");

    menu = g_menu_new();
    g_menu_append_submenu(menubar, "Edit", G_MENU_MODEL(menu));

    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append(section, "Paste", "win.paste");

    gtk_application_set_menubar(GTK_APPLICATION(app),
                                G_MENU_MODEL(menubar));
}

static void paste_cb(GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
    request_paste(user_data);
}

static const GActionEntry win_actions[] = {
    { "paste", paste_cb },
};

static GtkApplication *app;
GtkWidget *make_gtk_toplevel_window(void *frontend)
{
    GtkWidget *win = gtk_application_window_new(app);
    g_action_map_add_action_entries(G_ACTION_MAP(win),
                                    win_actions,
                                    G_N_ELEMENTS(win_actions),
                                    frontend);
    return win;
}

void launch_duplicate_session(Conf *conf)
{
    extern const int dup_check_launchable;
    assert(!dup_check_launchable || conf_launchable(conf));
    new_session_window(conf_copy(conf), NULL);
}

void session_window_closed(void)
{
    g_application_release(G_APPLICATION(app));
}

static void post_initial_config_box(void *vctx, int result)
{
    Conf *conf = (Conf *)vctx;

    if (result > 0) {
        new_session_window(conf, NULL);
    } else if (result == 0) {
        conf_free(conf);
        g_application_release(G_APPLICATION(app));
    }
}

void launch_saved_session(const char *str)
{
    Conf *conf = conf_new();
    do_defaults(str, conf);

    g_application_hold(G_APPLICATION(app));

    if (!conf_launchable(conf)) {
        initial_config_box(conf, post_initial_config_box, conf);
    } else {
        new_session_window(conf, NULL);
    }
}

void launch_new_session(void)
{
    /* Same as launch_saved_session except that we pass NULL to
     * do_defaults. */
    launch_saved_session(NULL);
}

void new_app_win(GtkApplication *app)
{
    launch_new_session();
}

static void window_setup_error_callback(void *vctx, int result)
{
    g_application_release(G_APPLICATION(app));
}

void window_setup_error(const char *errmsg)
{
    create_message_box(NULL, "Error creating session window", errmsg,
                       string_width("Some sort of fiddly error message that "
                                    "might be technical"),
                       TRUE, &buttons_ok, window_setup_error_callback, NULL);
}

static void activate(GApplication *app,
                     gpointer      user_data)
{
    new_app_win(GTK_APPLICATION(app));
}

static void newwin_cb(GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
    new_app_win(GTK_APPLICATION(user_data));
}

static void quit_cb(GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
    g_application_quit(G_APPLICATION(user_data));
}

static const GActionEntry app_actions[] = {
    { "newwin", newwin_cb },
    { "quit", quit_cb },
};

int main(int argc, char **argv)
{
    int status;

    {
        /* Call the function in ux{putty,pterm}.c to do app-type
         * specific setup */
        extern void setup(int);
        setup(FALSE);     /* FALSE means we are not a one-session process */
    }

    if (argc > 1) {
        extern char *pty_osx_envrestore_prefix;
        pty_osx_envrestore_prefix = argv[--argc];
    }

    {
        const char *home = getenv("HOME");
        if (home) {
            if (chdir(home)) {}
        }
    }

    gtkcomm_setup();

    app = gtk_application_new("org.tartarus.projects.putty.macputty",
                              G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_action_map_add_action_entries(G_ACTION_MAP(app),
                                    app_actions,
                                    G_N_ELEMENTS(app_actions),
                                    app);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

#endif /* GTK_CHECK_VERSION(3,0,0) */
