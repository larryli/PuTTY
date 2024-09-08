/*
 * main-gtk-application.c: a top-level front end to GUI PuTTY and
 * pterm, using GtkApplication. Suitable for OS X. Currently
 * unfinished.
 *
 * (You could run it on ordinary Linux GTK too, in principle, but I
 * don't think it would be particularly useful to do so, even once
 * it's fully working.)
 */

/*

Building this for OS X is currently broken, because the new
CMake-based build system doesn't support it yet. Probably what needs
doing is to add it back in to unix/CMakeLists.txt under a condition
like if(CMAKE_SYSTEM_NAME MATCHES "Darwin").

*/

/*

TODO list for a sensible GTK3 PuTTY/pterm on OS X:

Still to do on the application menu bar: items that have to vary with
context or user action (saved sessions and mid-session special
commands), and disabling/enabling the main actions in parallel with
their counterparts in the Ctrl-rightclick context menu.

Mouse wheel events and trackpad scrolling gestures don't work quite
right in the terminal drawing area. This seems to be a combination of
two things, neither of which I completely understand yet. Firstly, on
OS X GTK my trackpad seems to generate GDK scroll events for which
gdk_event_get_scroll_deltas returns integers rather than integer
multiples of 1/30, so we end up scrolling by very large amounts;
secondly, the window doesn't seem to receive a GTK "draw" event until
after the entire scroll gesture is complete, which means we don't get
constant visual feedback on how much we're scrolling by.

There doesn't seem to be a resize handle on terminal windows. Then
again, they do seem to _be_ resizable; the handle just isn't shown.
Perhaps that's a feature (certainly in a scrollbarless configuration
the handle gets in the way of the bottom right character cell in the
terminal itself), but it would be nice to at least understand _why_ it
happens and perhaps include an option to put it back again.

A slight oddity with menus that pop up directly under the mouse
pointer: mousing over the menu items doesn't highlight them initially,
but if I mouse off the menu and back on (without un-popping-it-up)
then suddenly that does work. I don't know if this is something I can
fix, though; it might very well be a quirk of the underlying GTK.

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
#include "gtkcompat.h"

char *x_get_default(const char *key) { return NULL; }

const bool buildinfo_gtk_relevant = true;

#if !GTK_CHECK_VERSION(3,0,0)
#error This front end only works in GTK 3
#endif

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
    g_menu_append(section, "Copy", "win.copy");
    g_menu_append(section, "Paste", "win.paste");
    g_menu_append(section, "Copy All", "win.copyall");

    menu = g_menu_new();
    g_menu_append_submenu(menubar, "Window", G_MENU_MODEL(menu));

    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append(section, "Restart Session", "win.restart");
    g_menu_append(section, "Duplicate Session", "win.duplicate");

    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append(section, "Change Settings", "win.changesettings");

    if (use_event_log) {
        section = g_menu_new();
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
        g_menu_append(section, "Event Log", "win.eventlog");
    }

    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_menu_append(section, "Clear Scrollback", "win.clearscrollback");
    g_menu_append(section, "Reset Terminal", "win.resetterm");

#if GTK_CHECK_VERSION(3,12,0)
#define SET_ACCEL(app, command, accel) do                       \
    {                                                           \
        static const char *const accels[] = { accel, NULL };    \
        gtk_application_set_accels_for_action(                  \
            GTK_APPLICATION(app), command, accels);             \
    } while (0)
#else
    /* The Gtk function used above was new in 3.12; the one below
     * was deprecated from 3.14. */
#define SET_ACCEL(app, command, accel) \
    gtk_application_add_accelerator(GTK_APPLICATION(app), accel, \
                                    command, NULL)
#endif

    SET_ACCEL(app, "app.newwin", "<Primary>n");
    SET_ACCEL(app, "win.copy", "<Primary>c");
    SET_ACCEL(app, "win.paste", "<Primary>v");

#undef SET_ACCEL

    gtk_application_set_menubar(GTK_APPLICATION(app),
                                G_MENU_MODEL(menubar));
}

#define WIN_ACTION_LIST(X)                      \
    X("copy", MA_COPY)                          \
    X("paste", MA_PASTE)                        \
    X("copyall", MA_COPY_ALL)                   \
    X("duplicate", MA_DUPLICATE_SESSION)        \
    X("restart", MA_RESTART_SESSION)            \
    X("changesettings", MA_CHANGE_SETTINGS)     \
    X("clearscrollback", MA_CLEAR_SCROLLBACK)   \
    X("resetterm", MA_RESET_TERMINAL)           \
    X("eventlog", MA_EVENT_LOG)                 \
    /* end of list */

#define WIN_ACTION_CALLBACK(name, id) \
static void win_action_cb_ ## id(GSimpleAction *a, GVariant *p, gpointer d) \
{ app_menu_action(d, id); }
WIN_ACTION_LIST(WIN_ACTION_CALLBACK)
#undef WIN_ACTION_CALLBACK

static const GActionEntry win_actions[] = {
#define WIN_ACTION_ENTRY(name, id) { name, win_action_cb_ ## id },
WIN_ACTION_LIST(WIN_ACTION_ENTRY)
#undef WIN_ACTION_ENTRY
};

static GtkApplication *app;
GtkWidget *make_gtk_toplevel_window(GtkFrontend *frontend)
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
    assert(!dup_check_launchable || conf_launchable(conf));
    g_application_hold(G_APPLICATION(app));
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
                       true, &buttons_ok, window_setup_error_callback, NULL);
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

static void about_cb(GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
    about_box(NULL);
}

static const GActionEntry app_actions[] = {
    { "newwin", newwin_cb },
    { "about", about_cb },
    { "quit", quit_cb },
};

int main(int argc, char **argv)
{
    int status;

    /* Call the function in ux{putty,pterm}.c to do app-type
     * specific setup */
    setup(false);     /* false means we are not a one-session process */

    if (argc > 1) {
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
                              G_APPLICATION_DEFAULT_FLAGS);
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
