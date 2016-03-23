/*
 * gtkapp.c: a top-level front end to GUI PuTTY and pterm, using
 * GtkApplication. Suitable for OS X. Currently unfinished.
 *
 * (You could run it on ordinary Linux GTK too, in principle, but I
 * don't think it would be particularly useful to do so, even once
 * it's fully working.)
 */

#include <assert.h>
#include <stdlib.h>

#include <unistd.h>

#include <gtk/gtk.h>

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"

char *x_get_default(const char *key) { return NULL; }

#if !GTK_CHECK_VERSION(3,0,0)
/* This front end only works in GTK 3. If that's not what we've got,
 * it's easier to just turn this program into a trivial stub by ifdef
 * in the source than it is to remove it in the makefile edifice. */
int main(int argc, char **argv)
{
    fprintf(stderr, "launcher does nothing on non-OSX platforms\n");
    return 1;
}
GtkWidget *make_gtk_toplevel_window(void *frontend) { return NULL; }
void launch_duplicate_session(Conf *conf) {}
void launch_new_session(void) {}
void launch_saved_session(const char *str) {}
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

extern int cfgbox(Conf *conf);

void launch_duplicate_session(Conf *conf)
{
    assert(conf_launchable(conf));
    new_session_window(conf, NULL);
}

void launch_new_session(void)
{
    Conf *conf = conf_new();
    do_defaults(NULL, conf);
    if (conf_launchable(conf) || cfgbox(conf)) {
        new_session_window(conf, NULL);
    }
}

void launch_saved_session(const char *str)
{
    Conf *conf = conf_new();
    do_defaults(str, conf);
    if (conf_launchable(conf) || cfgbox(conf)) {
        new_session_window(conf, NULL);
    }
}

void new_app_win(GtkApplication *app)
{
    launch_new_session();
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
