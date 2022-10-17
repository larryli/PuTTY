/*
 * dialog.c - dialogs for PuTTY(tel), including the configuration dialog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#include "putty.h"
#include "ssh.h"
#include "putty-rc.h"
#include "win-gui-seat.h"
#include "storage.h"
#include "dialog.h"
#include "licence.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#ifdef MSVC4
#define TVINSERTSTRUCT  TV_INSERTSTRUCT
#define TVITEM          TV_ITEM
#define ICON_BIG        1
#endif

/*
 * These are the various bits of data required to handle the
 * portable-dialog stuff in the config box. Having them at file
 * scope in here isn't too bad a place to put them; if we were ever
 * to need more than one config box per process we could always
 * shift them to a per-config-box structure stored in GWL_USERDATA.
 */
static struct controlbox *ctrlbox;
/*
 * ctrls_base holds the OK and Cancel buttons: the controls which
 * are present in all dialog panels. ctrls_panel holds the ones
 * which change from panel to panel.
 */
static struct winctrls ctrls_base, ctrls_panel;
static struct dlgparam dp;

#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

static char *events_initial[LOGEVENT_INITIAL_MAX];
static char *events_circular[LOGEVENT_CIRCULAR_MAX];
static int ninitial = 0, ncircular = 0, circular_first = 0;

#define PRINTER_DISABLED_STRING "无 (禁止打印)"

void force_normal(HWND hwnd)
{
    static bool recurse = false;

    WINDOWPLACEMENT wp;

    if (recurse)
        return;
    recurse = true;

    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
        wp.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
    }
    recurse = false;
}

static char *getevent(int i)
{
    if (i < ninitial)
        return events_initial[i];
    if ((i -= ninitial) < ncircular)
        return events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX];
    return NULL;
}

static HWND logbox;
HWND event_log_window(void) { return logbox; }

static INT_PTR CALLBACK LogProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    int i;

    switch (msg) {
      case WM_INITDIALOG: {
        char *str = dupprintf("%s 事件日志记录", appname);
        SetWindowText(hwnd, str);
        sfree(str);

        static int tabs[4] = { 78, 108 };
        SendDlgItemMessage(hwnd, IDN_LIST, LB_SETTABSTOPS, 2,
                           (LPARAM) tabs);

        for (i = 0; i < ninitial; i++)
            SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
                               0, (LPARAM) events_initial[i]);
        for (i = 0; i < ncircular; i++)
            SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
                               0, (LPARAM) events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX]);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            logbox = NULL;
            SetActiveWindow(GetParent(hwnd));
            DestroyWindow(hwnd);
            return 0;
          case IDN_COPY:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
                int selcount;
                int *selitems;
                selcount = SendDlgItemMessage(hwnd, IDN_LIST,
                                              LB_GETSELCOUNT, 0, 0);
                if (selcount == 0) {   /* don't even try to copy zero items */
                    MessageBeep(0);
                    break;
                }

                selitems = snewn(selcount, int);
                if (selitems) {
                    int count = SendDlgItemMessage(hwnd, IDN_LIST,
                                                   LB_GETSELITEMS,
                                                   selcount,
                                                   (LPARAM) selitems);
                    int i;
                    int size;
                    char *clipdata;
                    static unsigned char sel_nl[] = SEL_NL;

                    if (count == 0) {  /* can't copy zero stuff */
                        MessageBeep(0);
                        break;
                    }

                    size = 0;
                    for (i = 0; i < count; i++)
                        size +=
                            strlen(getevent(selitems[i])) + sizeof(sel_nl);

                    clipdata = snewn(size, char);
                    if (clipdata) {
                        char *p = clipdata;
                        for (i = 0; i < count; i++) {
                            char *q = getevent(selitems[i]);
                            int qlen = strlen(q);
                            memcpy(p, q, qlen);
                            p += qlen;
                            memcpy(p, sel_nl, sizeof(sel_nl));
                            p += sizeof(sel_nl);
                        }
                        write_aclip(CLIP_SYSTEM, clipdata, size, true);
                        sfree(clipdata);
                    }
                    sfree(selitems);

                    for (i = 0; i < (ninitial + ncircular); i++)
                        SendDlgItemMessage(hwnd, IDN_LIST, LB_SETSEL,
                                           false, i);
                }
            }
            return 0;
        }
        return 0;
      case WM_CLOSE:
        logbox = NULL;
        SetActiveWindow(GetParent(hwnd));
        DestroyWindow(hwnd);
        return 0;
    }
    return 0;
}

static INT_PTR CALLBACK LicenceProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        char *str = dupprintf("%s 许可证", appname);
        SetWindowText(hwnd, str);
        sfree(str);
        SetDlgItemText(hwnd, IDA_TEXT, LICENCE_TEXT("\r\n\r\n"));
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            EndDialog(hwnd, 1);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 1);
        return 0;
    }
    return 0;
}

static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    char *str;

    switch (msg) {
      case WM_INITDIALOG: {
        str = dupprintf("关于 %s", appname);
        SetWindowText(hwnd, str);
        sfree(str);
        char *buildinfo_text = buildinfo("\r\n");
        char *text = dupprintf
            ("%s\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
             appname, ver, buildinfo_text,
             "(C) " SHORT_COPYRIGHT_DETAILS ". 保留所有权利。");
        sfree(buildinfo_text);
        SetDlgItemText(hwnd, IDA_TEXT, text);
        MakeDlgItemBorderless(hwnd, IDA_TEXT);
        sfree(text);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            EndDialog(hwnd, true);
            return 0;
          case IDA_LICENCE:
            EnableWindow(hwnd, 0);
            DialogBox(hinst, MAKEINTRESOURCE(IDD_LICENCEBOX),
                      hwnd, LicenceProc);
            EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
            return 0;

          case IDA_WEB:
            /* Load web browser */
            ShellExecute(hwnd, "open",
                         "https://www.chiark.greenend.org.uk/~sgtatham/putty/",
                         0, 0, SW_SHOWDEFAULT);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, true);
        return 0;
    }
    return 0;
}

static int SaneDialogBox(HINSTANCE hinst,
                         LPCTSTR tmpl,
                         HWND hwndparent,
                         DLGPROC lpDialogFunc)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;
    int flags;
    int ret;
    int gm;

    wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA + 2*sizeof(LONG_PTR);
    wc.hInstance = hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND +1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PuTTYConfigBox";
    RegisterClass(&wc);

    hwnd = CreateDialog(hinst, tmpl, hwndparent, lpDialogFunc);

    SetWindowLongPtr(hwnd, BOXFLAGS, 0); /* flags */
    SetWindowLongPtr(hwnd, BOXRESULT, 0); /* result from SaneEndDialog */

    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
        flags=GetWindowLongPtr(hwnd, BOXFLAGS);
        if (!(flags & DF_END) && !IsDialogMessage(hwnd, &msg))
            DispatchMessage(&msg);
        if (flags & DF_END)
            break;
    }

    if (gm == 0)
        PostQuitMessage(msg.wParam); /* We got a WM_QUIT, pass it on */

    ret=GetWindowLongPtr(hwnd, BOXRESULT);
    DestroyWindow(hwnd);
    return ret;
}

static void SaneEndDialog(HWND hwnd, int ret)
{
    SetWindowLongPtr(hwnd, BOXRESULT, ret);
    SetWindowLongPtr(hwnd, BOXFLAGS, DF_END);
}

/*
 * Null dialog procedure.
 */
static INT_PTR CALLBACK NullDlgProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    return 0;
}

enum {
    IDCX_ABOUT = IDC_ABOUT,
    IDCX_TVSTATIC,
    IDCX_TREEVIEW,
    IDCX_STDBASE,
    IDCX_PANELBASE = IDCX_STDBASE + 32
};

struct treeview_faff {
    HWND treeview;
    HTREEITEM lastat[4];
};

static HTREEITEM treeview_insert(struct treeview_faff *faff,
                                 int level, char *text, char *path)
{
    TVINSERTSTRUCT ins;
    int i;
    HTREEITEM newitem;
    ins.hParent = (level > 0 ? faff->lastat[level - 1] : TVI_ROOT);
    ins.hInsertAfter = faff->lastat[level];
#if _WIN32_IE >= 0x0400 && defined NONAMELESSUNION
#define INSITEM DUMMYUNIONNAME.item
#else
#define INSITEM item
#endif
    ins.INSITEM.mask = TVIF_TEXT | TVIF_PARAM;
    ins.INSITEM.pszText = text;
    ins.INSITEM.cchTextMax = strlen(text)+1;
    ins.INSITEM.lParam = (LPARAM)path;
    newitem = TreeView_InsertItem(faff->treeview, &ins);
    if (level > 0)
        TreeView_Expand(faff->treeview, faff->lastat[level - 1],
                        (level > 1 ? TVE_COLLAPSE : TVE_EXPAND));
    faff->lastat[level] = newitem;
    for (i = level + 1; i < 4; i++)
        faff->lastat[i] = NULL;
    return newitem;
}

/*
 * Create the panelfuls of controls in the configuration box.
 */
static void create_controls(HWND hwnd, char *path)
{
    struct ctlpos cp;
    int index;
    int base_id;
    struct winctrls *wc;

    if (!path[0]) {
        /*
         * Here we must create the basic standard controls.
         */
        ctlposinit(&cp, hwnd, 3, 3, 235);
        wc = &ctrls_base;
        base_id = IDCX_STDBASE;
    } else {
        /*
         * Otherwise, we're creating the controls for a particular
         * panel.
         */
        ctlposinit(&cp, hwnd, 100, 3, 13);
        wc = &ctrls_panel;
        base_id = IDCX_PANELBASE;
    }

    for (index=-1; (index = ctrl_find_path(ctrlbox, path, index)) >= 0 ;) {
        struct controlset *s = ctrlbox->ctrlsets[index];
        winctrl_layout(&dp, wc, &cp, s, &base_id);
    }
}

const char *dialog_box_demo_screenshot_filename = NULL;

/*
 * This function is the configuration box.
 * (Being a dialog procedure, in general it returns 0 if the default
 * dialog processing should be performed, and 1 if it should not.)
 */
static INT_PTR CALLBACK GenericMainDlgProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    const int DEMO_SCREENSHOT_TIMER_ID = 1230;
    HWND hw, treeview;
    struct treeview_faff tvfaff;
    int ret;

    switch (msg) {
      case WM_INITDIALOG:
        dp.hwnd = hwnd;
        create_controls(hwnd, "");     /* Open and Cancel buttons etc */
        SetWindowText(hwnd, dp.wintitle);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        if (has_help())
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                             GetWindowLongPtr(hwnd, GWL_EXSTYLE) |
                             WS_EX_CONTEXTHELP);
        else {
            HWND item = GetDlgItem(hwnd, IDC_HELPBTN);
            if (item)
                DestroyWindow(item);
        }
        SendMessage(hwnd, WM_SETICON, (WPARAM) ICON_BIG,
                    (LPARAM) LoadIcon(hinst, MAKEINTRESOURCE(IDI_CFGICON)));
        /*
         * Centre the window.
         */
        {                              /* centre the window */
            RECT rs, rd;

            hw = GetDesktopWindow();
            if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
                MoveWindow(hwnd,
                           (rs.right + rs.left + rd.left - rd.right) / 2,
                           (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
                           rd.right - rd.left, rd.bottom - rd.top, true);
        }

        /*
         * Create the tree view.
         */
        {
            RECT r;
            WPARAM font;
            HWND tvstatic;

            r.left = 3;
            r.right = r.left + 95;
            r.top = 3;
            r.bottom = r.top + 10;
            MapDialogRect(hwnd, &r);
            tvstatic = CreateWindowEx(0, "STATIC", "分类(&G)：",
                                      WS_CHILD | WS_VISIBLE,
                                      r.left, r.top,
                                      r.right - r.left, r.bottom - r.top,
                                      hwnd, (HMENU) IDCX_TVSTATIC, hinst,
                                      NULL);
            font = SendMessage(hwnd, WM_GETFONT, 0, 0);
            SendMessage(tvstatic, WM_SETFONT, font, MAKELPARAM(true, 0));

            r.left = 3;
            r.right = r.left + 95;
            r.top = 13;
            r.bottom = r.top + 219;
            MapDialogRect(hwnd, &r);
            treeview = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, "",
                                      WS_CHILD | WS_VISIBLE |
                                      WS_TABSTOP | TVS_HASLINES |
                                      TVS_DISABLEDRAGDROP | TVS_HASBUTTONS
                                      | TVS_LINESATROOT |
                                      TVS_SHOWSELALWAYS, r.left, r.top,
                                      r.right - r.left, r.bottom - r.top,
                                      hwnd, (HMENU) IDCX_TREEVIEW, hinst,
                                      NULL);
            font = SendMessage(hwnd, WM_GETFONT, 0, 0);
            SendMessage(treeview, WM_SETFONT, font, MAKELPARAM(true, 0));
            tvfaff.treeview = treeview;
            memset(tvfaff.lastat, 0, sizeof(tvfaff.lastat));
        }

        /*
         * Set up the tree view contents.
         */
        {
            HTREEITEM hfirst = NULL;
            int i;
            char *path = NULL;
            char *firstpath = NULL;

            for (i = 0; i < ctrlbox->nctrlsets; i++) {
                struct controlset *s = ctrlbox->ctrlsets[i];
                HTREEITEM item;
                int j;
                char *c;

                if (!s->pathname[0])
                    continue;
                j = path ? ctrl_path_compare(s->pathname, path) : 0;
                if (j == INT_MAX)
                    continue;          /* same path, nothing to add to tree */

                /*
                 * We expect never to find an implicit path
                 * component. For example, we expect never to see
                 * A/B/C followed by A/D/E, because that would
                 * _implicitly_ create A/D. All our path prefixes
                 * are expected to contain actual controls and be
                 * selectable in the treeview; so we would expect
                 * to see A/D _explicitly_ before encountering
                 * A/D/E.
                 */
                assert(j == ctrl_path_elements(s->pathname) - 1);

                c = strrchr(s->pathname, '/');
                if (!c)
                        c = s->pathname;
                else
                        c++;

                item = treeview_insert(&tvfaff, j, c, s->pathname);
                if (!hfirst) {
                    hfirst = item;
                    firstpath = s->pathname;
                }

                path = s->pathname;
            }

            /*
             * Put the treeview selection on to the first panel in the
             * ctrlbox.
             */
            TreeView_SelectItem(treeview, hfirst);

            /*
             * And create the actual control set for that panel, to
             * match the initial treeview selection.
             */
            assert(firstpath);   /* config.c must have given us _something_ */
            create_controls(hwnd, firstpath);
            dlg_refresh(NULL, &dp);    /* and set up control values */
        }

        /*
         * Set focus into the first available control.
         */
        {
            int i;
            struct winctrl *c;

            for (i = 0; (c = winctrl_findbyindex(&ctrls_panel, i)) != NULL;
                 i++) {
                if (c->ctrl) {
                    dlg_set_focus(c->ctrl, &dp);
                    break;
                }
            }
        }

        /*
         * Now we've finished creating our initial set of controls,
         * it's safe to actually show the window without risking setup
         * flicker.
         */
        ShowWindow(hwnd, SW_SHOWNORMAL);

        /*
         * Set the flag that activates a couple of the other message
         * handlers below, which were disabled until now to avoid
         * spurious firing during the above setup procedure.
         */
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);

        if (dialog_box_demo_screenshot_filename)
            SetTimer(hwnd, DEMO_SCREENSHOT_TIMER_ID, TICKSPERSEC, NULL);
        return 0;
      case WM_TIMER:
        if (dialog_box_demo_screenshot_filename &&
            (UINT_PTR)wParam == DEMO_SCREENSHOT_TIMER_ID) {
            KillTimer(hwnd, DEMO_SCREENSHOT_TIMER_ID);
            const char *err = save_screenshot(
                hwnd, dialog_box_demo_screenshot_filename);
            if (err)
                MessageBox(hwnd, err, "Demo screenshot failure",
                           MB_OK | MB_ICONERROR);
            SaneEndDialog(hwnd, 0);
        }
        return 0;
      case WM_LBUTTONUP:
        /*
         * Button release should trigger WM_OK if there was a
         * previous double click on the session list.
         */
        ReleaseCapture();
        if (dp.ended)
            SaneEndDialog(hwnd, dp.endresult ? 1 : 0);
        break;
      case WM_NOTIFY:
        if (LOWORD(wParam) == IDCX_TREEVIEW &&
            ((LPNMHDR) lParam)->code == TVN_SELCHANGED) {
            /*
             * Selection-change events on the treeview cause us to do
             * a flurry of control deletion and creation - but only
             * after WM_INITDIALOG has finished. The initial
             * selection-change event(s) during treeview setup are
             * ignored.
             */
            HTREEITEM i;
            TVITEM item;
            char buffer[64];

            if (GetWindowLongPtr(hwnd, GWLP_USERDATA) != 1)
                return 0;

            i = TreeView_GetSelection(((LPNMHDR) lParam)->hwndFrom);

            SendMessage (hwnd, WM_SETREDRAW, false, 0);

            item.hItem = i;
            item.pszText = buffer;
            item.cchTextMax = sizeof(buffer);
            item.mask = TVIF_TEXT | TVIF_PARAM;
            TreeView_GetItem(((LPNMHDR) lParam)->hwndFrom, &item);
            {
                /* Destroy all controls in the currently visible panel. */
                int k;
                HWND item;
                struct winctrl *c;

                while ((c = winctrl_findbyindex(&ctrls_panel, 0)) != NULL) {
                    for (k = 0; k < c->num_ids; k++) {
                        item = GetDlgItem(hwnd, c->base_id + k);
                        if (item)
                            DestroyWindow(item);
                    }
                    winctrl_rem_shortcuts(&dp, c);
                    winctrl_remove(&ctrls_panel, c);
                    sfree(c->data);
                    sfree(c);
                }
            }
            create_controls(hwnd, (char *)item.lParam);

            dlg_refresh(NULL, &dp);    /* set up control values */

            SendMessage (hwnd, WM_SETREDRAW, true, 0);
            InvalidateRect (hwnd, NULL, true);

            SetFocus(((LPNMHDR) lParam)->hwndFrom);     /* ensure focus stays */
            return 0;
        }
        break;
      case WM_COMMAND:
      case WM_DRAWITEM:
      default:                         /* also handle drag list msg here */
        /*
         * Only process WM_COMMAND once the dialog is fully formed.
         */
        if (GetWindowLongPtr(hwnd, GWLP_USERDATA) == 1) {
            ret = winctrl_handle_command(&dp, msg, wParam, lParam);
            if (dp.ended && GetCapture() != hwnd)
                SaneEndDialog(hwnd, dp.endresult ? 1 : 0);
        } else
            ret = 0;
        return ret;
      case WM_HELP:
        if (!winctrl_context_help(&dp, hwnd,
                                 ((LPHELPINFO)lParam)->iCtrlId))
            MessageBeep(0);
        break;
      case WM_CLOSE:
        quit_help(hwnd);
        SaneEndDialog(hwnd, 0);
        return 0;

        /* Grrr Explorer will maximize Dialogs! */
      case WM_SIZE:
        if (wParam == SIZE_MAXIMIZED)
            force_normal(hwnd);
        return 0;

    }
    return 0;
}

void modal_about_box(HWND hwnd)
{
    EnableWindow(hwnd, 0);
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
    EnableWindow(hwnd, 1);
    SetActiveWindow(hwnd);
}

void show_help(HWND hwnd)
{
    launch_help(hwnd, NULL);
}

void defuse_showwindow(void)
{
    /*
     * Work around the fact that the app's first call to ShowWindow
     * will ignore the default in favour of the shell-provided
     * setting.
     */
    {
        HWND hwnd;
        hwnd = CreateDialog(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
                            NULL, NullDlgProc);
        ShowWindow(hwnd, SW_HIDE);
        SetActiveWindow(hwnd);
        DestroyWindow(hwnd);
    }
}

bool do_config(Conf *conf)
{
    bool ret;

    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, false, 0, 0);
    win_setup_config_box(ctrlbox, &dp.hwnd, has_help(), false, 0);
    dp_init(&dp);
    winctrl_init(&ctrls_base);
    winctrl_init(&ctrls_panel);
    dp_add_tree(&dp, &ctrls_base);
    dp_add_tree(&dp, &ctrls_panel);
    dp.wintitle = dupprintf("%s 配置", appname);
    dp.errtitle = dupprintf("%s 错误", appname);
    dp.data = conf;
    dlg_auto_set_fixed_pitch_flag(&dp);
    dp.shortcuts['g'] = true;          /* the treeview: `Cate&gory' */

    ret =
        SaneDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), NULL,
                  GenericMainDlgProc);

    ctrl_free_box(ctrlbox);
    winctrl_cleanup(&ctrls_panel);
    winctrl_cleanup(&ctrls_base);
    dp_cleanup(&dp);

    return ret;
}

bool do_reconfig(HWND hwnd, Conf *conf, int protcfginfo)
{
    Conf *backup_conf;
    bool ret;
    int protocol;

    backup_conf = conf_copy(conf);

    ctrlbox = ctrl_new_box();
    protocol = conf_get_int(conf, CONF_protocol);
    setup_config_box(ctrlbox, true, protocol, protcfginfo);
    win_setup_config_box(ctrlbox, &dp.hwnd, has_help(), true, protocol);
    dp_init(&dp);
    winctrl_init(&ctrls_base);
    winctrl_init(&ctrls_panel);
    dp_add_tree(&dp, &ctrls_base);
    dp_add_tree(&dp, &ctrls_panel);
    dp.wintitle = dupprintf("%s 重新配置", appname);
    dp.errtitle = dupprintf("%s 错误", appname);
    dp.data = conf;
    dlg_auto_set_fixed_pitch_flag(&dp);
    dp.shortcuts['g'] = true;          /* the treeview: `Cate&gory' */

    ret = SaneDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), NULL,
                  GenericMainDlgProc);

    ctrl_free_box(ctrlbox);
    winctrl_cleanup(&ctrls_base);
    winctrl_cleanup(&ctrls_panel);
    dp_cleanup(&dp);

    if (!ret)
        conf_copy_into(conf, backup_conf);

    conf_free(backup_conf);

    return ret;
}

static void win_gui_eventlog(LogPolicy *lp, const char *string)
{
    char timebuf[40];
    char **location;
    struct tm tm;

    tm=ltime();
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);

    if (ninitial < LOGEVENT_INITIAL_MAX)
        location = &events_initial[ninitial];
    else
        location = &events_circular[(circular_first + ncircular) % LOGEVENT_CIRCULAR_MAX];

    if (*location)
        sfree(*location);
    *location = dupcat(timebuf, string);
    if (logbox) {
        int count;
        SendDlgItemMessage(logbox, IDN_LIST, LB_ADDSTRING,
                           0, (LPARAM) *location);
        count = SendDlgItemMessage(logbox, IDN_LIST, LB_GETCOUNT, 0, 0);
        SendDlgItemMessage(logbox, IDN_LIST, LB_SETTOPINDEX, count - 1, 0);
    }
    if (ninitial < LOGEVENT_INITIAL_MAX) {
        ninitial++;
    } else if (ncircular < LOGEVENT_CIRCULAR_MAX) {
        ncircular++;
    } else if (ncircular == LOGEVENT_CIRCULAR_MAX) {
        circular_first = (circular_first + 1) % LOGEVENT_CIRCULAR_MAX;
        sfree(events_circular[circular_first]);
        events_circular[circular_first] = dupstr("..");
    }
}

static void win_gui_logging_error(LogPolicy *lp, const char *event)
{
    WinGuiSeat *wgs = container_of(lp, WinGuiSeat, logpolicy);

    /* Send 'can't open log file' errors to the terminal window.
     * (Marked as stderr, although terminal.c won't care.) */
    seat_stderr_pl(&wgs->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&wgs->seat, PTRLEN_LITERAL("\r\n"));
}

void showeventlog(HWND hwnd)
{
    if (!logbox) {
        logbox = CreateDialog(hinst, MAKEINTRESOURCE(IDD_LOGBOX),
                              hwnd, LogProc);
        ShowWindow(logbox, SW_SHOWNORMAL);
    }
    SetActiveWindow(logbox);
}

void showabout(HWND hwnd)
{
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
}

struct hostkey_dialog_ctx {
    const char *const *keywords;
    const char *const *values;
    const char *host;
    int port;
    FingerprintType fptype_default;
    char **fingerprints;
    const char *keydisp;
    LPCTSTR iconid;
    const char *helpctx;
};

static INT_PTR CALLBACK HostKeyMoreInfoProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        const struct hostkey_dialog_ctx *ctx =
            (const struct hostkey_dialog_ctx *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (INT_PTR)ctx);

        if (ctx->fingerprints[SSH_FPTYPE_SHA256])
            SetDlgItemText(hwnd, IDC_HKI_SHA256,
                           ctx->fingerprints[SSH_FPTYPE_SHA256]);
        if (ctx->fingerprints[SSH_FPTYPE_MD5])
            SetDlgItemText(hwnd, IDC_HKI_MD5,
                           ctx->fingerprints[SSH_FPTYPE_MD5]);

        SetDlgItemText(hwnd, IDA_TEXT, ctx->keydisp);

        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
            EndDialog(hwnd, 0);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 0);
        return 0;
    }
    return 0;
}

static INT_PTR CALLBACK HostKeyDialogProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        strbuf *sb = strbuf_new();
        const struct hostkey_dialog_ctx *ctx =
            (const struct hostkey_dialog_ctx *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (INT_PTR)ctx);
        for (int id = 100;; id++) {
            char buf[256];

            if (!GetDlgItemText(hwnd, id, buf, (int)lenof(buf)))
                break;

            strbuf_clear(sb);
            for (const char *p = buf; *p ;) {
                if (*p == '{') {
                    for (size_t i = 0; ctx->keywords[i]; i++) {
                        if (strstartswith(p, ctx->keywords[i])) {
                            p += strlen(ctx->keywords[i]);
                            put_dataz(sb, ctx->values[i]);
                            goto matched;
                        }
                    }
                } else {
                    put_byte(sb, *p++);
                }
              matched:;
            }

            SetDlgItemText(hwnd, id, sb->s);
        }
        strbuf_free(sb);

        char *hostport = dupprintf("%s (port %d)", ctx->host, ctx->port);
        SetDlgItemText(hwnd, IDC_HK_HOST, hostport);
        sfree(hostport);
        MakeDlgItemBorderless(hwnd, IDC_HK_HOST);

        SetDlgItemText(hwnd, IDC_HK_FINGERPRINT,
                       ctx->fingerprints[ctx->fptype_default]);
        MakeDlgItemBorderless(hwnd, IDC_HK_FINGERPRINT);

        HANDLE icon = LoadImage(
            NULL, ctx->iconid, IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
            LR_SHARED);
        SendDlgItemMessage(hwnd, IDC_HK_ICON, STM_SETICON, (WPARAM)icon, 0);

        if (!has_help()) {
            HWND item = GetDlgItem(hwnd, IDHELP);
            if (item)
                DestroyWindow(item);
        }

        return 1;
      }
      case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND control = (HWND)lParam;

        if (GetWindowLongPtr(control, GWLP_ID) == IDC_HK_TITLE) {
            SetBkMode(hdc, TRANSPARENT);
            HFONT prev_font = (HFONT)SelectObject(
                hdc, (HFONT)GetStockObject(SYSTEM_FONT));
            LOGFONT lf;
            if (GetObject(prev_font, sizeof(lf), &lf)) { 
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = lf.lfHeight * 3 / 2;
                HFONT bold_font = CreateFontIndirect(&lf);
                if (bold_font)
                    SelectObject(hdc, bold_font);
            }
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        return 0;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_HK_ACCEPT:
          case IDC_HK_ONCE:
          case IDCANCEL:
            EndDialog(hwnd, LOWORD(wParam));
            return 0;
          case IDHELP: {
            const struct hostkey_dialog_ctx *ctx =
                (const struct hostkey_dialog_ctx *)
                GetWindowLongPtr(hwnd, GWLP_USERDATA);
            launch_help(hwnd, ctx->helpctx);
            return 0;
          }
          case IDC_HK_MOREINFO: {
            const struct hostkey_dialog_ctx *ctx =
                (const struct hostkey_dialog_ctx *)
                GetWindowLongPtr(hwnd, GWLP_USERDATA);
            DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_HK_MOREINFO),
                           hwnd, HostKeyMoreInfoProc, (LPARAM)ctx);
          }
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return 0;
    }
    return 0;
}

SeatPromptResult win_seat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, const char *keydisp, char **fingerprints, bool mismatch,
    void (*callback)(void *ctx, SeatPromptResult result), void *vctx)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);

    static const char *const keywords[] =
        { "{KEYTYPE}", "{APPNAME}", NULL };

    const char *values[2];
    values[0] = keytype;
    values[1] = appname;

    struct hostkey_dialog_ctx ctx[1];
    ctx->keywords = keywords;
    ctx->values = values;
    ctx->fingerprints = fingerprints;
    ctx->fptype_default = ssh2_pick_default_fingerprint(fingerprints);
    ctx->keydisp = keydisp;
    ctx->iconid = (mismatch ? IDI_WARNING : IDI_QUESTION);
    ctx->helpctx = (mismatch ? WINHELP_CTX_errors_hostkey_changed :
                    WINHELP_CTX_errors_hostkey_absent);
    ctx->host = host;
    ctx->port = port;
    int dlgid = (mismatch ? IDD_HK_WRONG : IDD_HK_ABSENT);
    int mbret = DialogBoxParam(
        hinst, MAKEINTRESOURCE(dlgid), wgs->term_hwnd,
        HostKeyDialogProc, (LPARAM)ctx);
    assert(mbret==IDC_HK_ACCEPT || mbret==IDC_HK_ONCE || mbret==IDCANCEL);
    if (mbret == IDC_HK_ACCEPT) {
        store_host_key(host, port, keytype, keystr);
        return SPR_OK;
    } else if (mbret == IDC_HK_ONCE) {
        return SPR_OK;
    }

    return SPR_USER_ABORT;
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
SeatPromptResult win_seat_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    static const char mbtitle[] = "%s 安全警告";
    static const char msg[] =
        "服务器支持的第一个 %s\n"
        "是 %s，其低于配置的警告阀值。\n"
        "要继续连接么？\n";
    char *message, *title;
    int mbret;

    message = dupprintf(msg, algtype, algname);
    title = dupprintf(mbtitle, appname);
    mbret = MessageBox(NULL, message, title,
                       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    sfree(message);
    sfree(title);
    if (mbret == IDYES)
        return SPR_OK;
    else
        return SPR_USER_ABORT;
}

SeatPromptResult win_seat_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    static const char mbtitle[] = "%s 安全警告";
    static const char msg[] =
        "此服务器要存储的第一个主机密钥类型\n"
        "为 %s，其低于配置的警告阀值。\n"
        "此服务器同时也提供有下列高于阀值的\n"
        "主机密钥类型（不会存储）：\n"
        "%s\n"
        "要继续连接么？\n";
    char *message, *title;
    int mbret;

    message = dupprintf(msg, algname, betteralgs);
    title = dupprintf(mbtitle, appname);
    mbret = MessageBox(NULL, message, title,
                       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    sfree(message);
    sfree(title);
    if (mbret == IDYES)
        return SPR_OK;
    else
        return SPR_USER_ABORT;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
static int win_gui_askappend(LogPolicy *lp, Filename *filename,
                             void (*callback)(void *ctx, int result),
                             void *ctx)
{
    static const char msgtemplate[] =
        "会话日志文件 \"%.*s\" 已经存在。\n"
        "可以使用新会话日志覆盖旧文件，\n"
        "或者在旧日志文件结尾增加新日志，\n"
        "或在此会话中禁用日志记录。\n"
        "点击“是”覆盖为新文件，“否”附加到旧文件，\n"
        "或者点击“取消”禁用日志记录。";
    char *message;
    char *mbtitle;
    int mbret;

    message = dupprintf(msgtemplate, FILENAME_MAX, filename->path);
    mbtitle = dupprintf("%s 日志记录到文件", appname);

    mbret = MessageBox(NULL, message, mbtitle,
                       MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON3);

    socket_reselect_all();

    sfree(message);
    sfree(mbtitle);

    if (mbret == IDYES)
        return 2;
    else if (mbret == IDNO)
        return 1;
    else
        return 0;
}

const LogPolicyVtable win_gui_logpolicy_vt = {
    .eventlog = win_gui_eventlog,
    .askappend = win_gui_askappend,
    .logging_error = win_gui_logging_error,
    .verbose = null_lp_verbose_yes,
};

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
    static const char mbtitle[] = "%s 密钥文件警告";
    static const char message[] =
        "现在载入的是一个旧版本文件格式的 SSH2\n"
        "私钥。这意味着该私钥文件没有足够的安全\n"
        "性。未来版本的 %s 可能会停止支持\n"
        "此私钥格式，建议将其转换为新的格式。\n"
        "\n"
        "请使用 PuTTYgen 载入该密钥进行转换然\n"
        "后保存。";

    char *msg, *title;
    msg = dupprintf(message, appname);
    title = dupprintf(mbtitle, appname);

    MessageBox(NULL, msg, title, MB_OK);

    socket_reselect_all();

    sfree(msg);
    sfree(title);
}
