/*
 * PuTTY key generation front end.
 */

#include <windows.h>
#include <commctrl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define PUTTY_DO_GLOBALS

#include "putty.h"
#include "ssh.h"
#include "winstuff.h"

#define WM_DONEKEY (WM_XUSER + 1)

#define DEFAULT_KEYSIZE 1024

/* ----------------------------------------------------------------------
 * Progress report code. This is really horrible :-)
 */
#define PHASE1TOTAL 0x10000
#define PHASE2TOTAL 0x10000
#define PHASE3TOTAL 0x04000
#define PHASE1START 0
#define PHASE2START (PHASE1TOTAL)
#define PHASE3START (PHASE1TOTAL + PHASE2TOTAL)
#define TOTALTOTAL  (PHASE1TOTAL + PHASE2TOTAL + PHASE3TOTAL)
#define PROGRESSBIGRANGE 65535
#define DIVISOR     ((TOTALTOTAL + PROGRESSBIGRANGE - 1) / PROGRESSBIGRANGE)
#define PROGRESSRANGE (TOTALTOTAL / DIVISOR)
struct progress {
    unsigned phase1param, phase1current, phase1n;
    unsigned phase2param, phase2current, phase2n;
    unsigned phase3mult;
    HWND progbar;
};

static void progress_update(void *param, int phase, int iprogress) {
    struct progress *p = (struct progress *)param;
    unsigned progress = iprogress;
    int position;

    switch (phase) {
      case -1:
        p->phase1param = 0x10000 + progress;
        p->phase1current = 0x10000; p->phase1n = 0;
        return;
      case -2:
        p->phase2param = 0x10000 + progress;
        p->phase2current = 0x10000; p->phase2n = 0;
        return;
      case -3:
        p->phase3mult = PHASE3TOTAL / progress;
        return;
      case 1:
        while (p->phase1n < progress) {
            p->phase1n++;
            p->phase1current *= p->phase1param;
            p->phase1current /= 0x10000;
        }
        position = PHASE1START + 0x10000 - p->phase1current;
        break;
      case 2:
        while (p->phase2n < progress) {
            p->phase2n++;
            p->phase2current *= p->phase2param;
            p->phase2current /= 0x10000;
        }
        position = PHASE2START + 0x10000 - p->phase2current;
        break;
      case 3:
        position = PHASE3START + progress * p->phase3mult;
        break;
    }

    SendMessage(p->progbar, PBM_SETPOS, position / DIVISOR, 0);
}

extern char ver[];

#define PASSPHRASE_MAXLEN 512

struct PassphraseProcStruct {
    char *passphrase;
    char *comment;
};

/*
 * Dialog-box function for the passphrase box.
 */
static int CALLBACK PassphraseProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam) {
    static char *passphrase;
    struct PassphraseProcStruct *p;

    switch (msg) {
      case WM_INITDIALOG:
        SetForegroundWindow(hwnd);
        SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        p = (struct PassphraseProcStruct *)lParam;
        passphrase = p->passphrase;
        if (p->comment)
            SetDlgItemText(hwnd, 101, p->comment);
        *passphrase = 0;
        return 0;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    if (*passphrase)
		EndDialog (hwnd, 1);
	    else
		MessageBeep (0);
	    return 0;
	  case IDCANCEL:
	    EndDialog (hwnd, 0);
	    return 0;
          case 102:                    /* edit box */
	    if (HIWORD(wParam) == EN_CHANGE) {
                GetDlgItemText (hwnd, 102, passphrase, PASSPHRASE_MAXLEN-1);
                passphrase[PASSPHRASE_MAXLEN-1] = '\0';
            }
            return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog (hwnd, 0);
	return 0;
    }
    return 0;
}

/*
 * Prompt for a key file. Assumes the filename buffer is of size
 * FILENAME_MAX.
 */
static int prompt_keyfile(HWND hwnd, char *dlgtitle,
                          char *filename, int save) {
    OPENFILENAME of;
    memset(&of, 0, sizeof(of));
#ifdef OPENFILENAME_SIZE_VERSION_400
    of.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
    of.lStructSize = sizeof(of);
#endif
    of.hwndOwner = hwnd;
    of.lpstrFilter = "All Files\0*\0\0\0";
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filename; *filename = '\0';
    of.nMaxFile = FILENAME_MAX;
    of.lpstrFileTitle = NULL;
    of.lpstrInitialDir = NULL;
    of.lpstrTitle = dlgtitle;
    of.Flags = 0;
    if (save)
        return GetSaveFileName(&of);
    else
        return GetOpenFileName(&of);
}

/*
 * This function is needed to link with the DES code. We need not
 * have it do anything at all.
 */
void logevent(char *msg) {
}

/*
 * Dialog-box function for the Licence box.
 */
static int CALLBACK LicenceProc (HWND hwnd, UINT msg,
				 WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
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

/*
 * Dialog-box function for the About box.
 */
static int CALLBACK AboutProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
        SetDlgItemText (hwnd, 100, ver);
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
            EndDialog(hwnd, 1);
	    return 0;
	  case 101:
	    EnableWindow(hwnd, 0);
	    DialogBox (hinst, MAKEINTRESOURCE(214), NULL, LicenceProc);
	    EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

/*
 * Thread to generate a key.
 */
struct rsa_key_thread_params {
    HWND progressbar;                  /* notify this with progress */
    HWND dialog;                       /* notify this on completion */
    int keysize;		       /* bits in key */
    struct RSAKey *key;
    struct RSAAux *aux;
};
static DWORD WINAPI generate_rsa_key_thread(void *param) {
    struct rsa_key_thread_params *params =
        (struct rsa_key_thread_params *)param;
    struct progress prog;
    prog.progbar = params->progressbar;

    rsa_generate(params->key, params->aux,
		 params->keysize, progress_update, &prog);

    PostMessage(params->dialog, WM_DONEKEY, 0, 0);

    free(params);
    return 0;
}

struct MainDlgState {
    int collecting_entropy;
    int generation_thread_exists;
    int key_exists;
    int entropy_got, entropy_required, entropy_size;
    int keysize;
    unsigned *entropy;
    struct RSAKey key;
    struct RSAAux aux;
};

static void hidemany(HWND hwnd, const int *ids, int hideit) {
    while (*ids) {
        ShowWindow(GetDlgItem(hwnd, *ids++), (hideit ? SW_HIDE : SW_SHOW));
    }
}

static void setupbigedit(HWND hwnd, int id, struct RSAKey *key) {
    char *buffer;
    char *dec1, *dec2;

    dec1 = bignum_decimal(key->exponent);
    dec2 = bignum_decimal(key->modulus);
    buffer = malloc(strlen(dec1)+strlen(dec2)+
                    strlen(key->comment)+30);
    sprintf(buffer, "%d %s %s %s",
            ssh1_bignum_bitcount(key->modulus),
            dec1, dec2, key->comment);
    SetDlgItemText(hwnd, id, buffer);
    free(dec1);
    free(dec2);
    free(buffer);
}

/*
 * Dialog-box function for the main PuTTYgen dialog box.
 */
static int CALLBACK MainDlgProc (HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam) {
    enum {
        controlidstart = 100,
        IDC_TITLE,
        IDC_BOX_KEY, IDC_BOXT_KEY,
        IDC_NOKEY,
        IDC_GENERATING,
        IDC_PROGRESS,
        IDC_PKSTATIC, IDC_KEYDISPLAY,
        IDC_FPSTATIC, IDC_FINGERPRINT,
        IDC_COMMENTSTATIC, IDC_COMMENTEDIT,
        IDC_PASSPHRASE1STATIC, IDC_PASSPHRASE1EDIT,
        IDC_PASSPHRASE2STATIC, IDC_PASSPHRASE2EDIT,
        IDC_BOX_ACTIONS, IDC_BOXT_ACTIONS,
        IDC_GENSTATIC, IDC_GENERATE,
        IDC_LOADSTATIC, IDC_LOAD,
        IDC_SAVESTATIC, IDC_SAVE,
        IDC_BOX_PARAMS, IDC_BOXT_PARAMS,
        IDC_BITSSTATIC, IDC_BITS,
        IDC_ABOUT,
    };
    static const int nokey_ids[] = { IDC_NOKEY, 0 };
    static const int generating_ids[] = { IDC_GENERATING, IDC_PROGRESS, 0 };
    static const int gotkey_ids[] = {
        IDC_PKSTATIC, IDC_KEYDISPLAY,
        IDC_FPSTATIC, IDC_FINGERPRINT,
        IDC_COMMENTSTATIC, IDC_COMMENTEDIT,
        IDC_PASSPHRASE1STATIC, IDC_PASSPHRASE1EDIT,
        IDC_PASSPHRASE2STATIC, IDC_PASSPHRASE2EDIT, 0 };
    static const char generating_msg[] =
        "Please wait while a key is generated...";
    static const char entropy_msg[] =
        "Please generate some randomness by moving the mouse over the blank area.";
    struct MainDlgState *state;

    switch (msg) {
      case WM_INITDIALOG:
        state = malloc(sizeof(*state));
        state->generation_thread_exists = FALSE;
        state->collecting_entropy = FALSE;
        state->entropy = NULL;
        state->key_exists = FALSE;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)state);
        {
            struct ctlpos cp, cp2;

	    /* Accelerators used: acglops */

            ctlposinit(&cp, hwnd, 10, 10, 10);
            bartitle(&cp, "Public and private key generation for PuTTY",
                    IDC_TITLE);
            beginbox(&cp, "Key",
                     IDC_BOX_KEY, IDC_BOXT_KEY);
            cp2 = cp;
            statictext(&cp2, "No key.", IDC_NOKEY);
            cp2 = cp;
            statictext(&cp2, "",
                       IDC_GENERATING);
            progressbar(&cp2, IDC_PROGRESS);
            bigeditctrl(&cp,
                        "&Public key for pasting into authorized_keys file:",
                        IDC_PKSTATIC, IDC_KEYDISPLAY, 7);
            SendDlgItemMessage(hwnd, IDC_KEYDISPLAY, EM_SETREADONLY, 1, 0);
            staticedit(&cp, "Key fingerprint:", IDC_FPSTATIC,
                       IDC_FINGERPRINT, 70);
            SendDlgItemMessage(hwnd, IDC_FINGERPRINT, EM_SETREADONLY, 1, 0);
            staticedit(&cp, "Key &comment:", IDC_COMMENTSTATIC,
                       IDC_COMMENTEDIT, 70);
            staticpassedit(&cp, "Key p&assphrase:", IDC_PASSPHRASE1STATIC,
                           IDC_PASSPHRASE1EDIT, 70);
            staticpassedit(&cp, "C&onfirm passphrase:", IDC_PASSPHRASE2STATIC,
                           IDC_PASSPHRASE2EDIT, 70);
            endbox(&cp);
            beginbox(&cp, "Actions",
                     IDC_BOX_ACTIONS, IDC_BOXT_ACTIONS);
            staticbtn(&cp, "Generate a public/private key pair",
                      IDC_GENSTATIC, "&Generate", IDC_GENERATE);
            staticbtn(&cp, "Load an existing private key file",
                      IDC_LOADSTATIC, "&Load", IDC_LOAD);
            staticbtn(&cp, "Save the generated key to a new file",
                      IDC_SAVESTATIC, "&Save", IDC_SAVE);
            endbox(&cp);
            beginbox(&cp, "Parameters",
                     IDC_BOX_PARAMS, IDC_BOXT_PARAMS);
            staticedit(&cp, "Number of &bits in a generated key:",
		       IDC_BITSSTATIC, IDC_BITS, 20);
            endbox(&cp);
        }
	SetDlgItemInt(hwnd, IDC_BITS, DEFAULT_KEYSIZE, FALSE);

        /*
         * Initially, hide the progress bar and the key display,
         * and show the no-key display. Also disable the Save
         * button, because with no key we obviously can't save
         * anything.
         */
        hidemany(hwnd, nokey_ids, FALSE);
        hidemany(hwnd, generating_ids, TRUE);
        hidemany(hwnd, gotkey_ids, TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 0);

	return 1;
      case WM_MOUSEMOVE:
        state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
        if (state->collecting_entropy &&
            state->entropy &&
            state->entropy_got < state->entropy_required) {
            state->entropy[state->entropy_got++] = lParam;
            state->entropy[state->entropy_got++] = GetMessageTime();
            SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS,
                               state->entropy_got, 0);
            if (state->entropy_got >= state->entropy_required) {
                struct rsa_key_thread_params *params;
                DWORD threadid;

                /*
                 * Seed the entropy pool
                 */
                random_add_heavynoise(state->entropy, state->entropy_size);
                memset(state->entropy, 0, state->entropy_size);
                free(state->entropy);
                state->collecting_entropy = FALSE;

                SetDlgItemText(hwnd, IDC_GENERATING, generating_msg);
                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0,
                                   MAKELPARAM(0, PROGRESSRANGE));
                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);

                params = malloc(sizeof(*params));
                params->progressbar = GetDlgItem(hwnd, IDC_PROGRESS);
                params->dialog = hwnd;
		params->keysize = state->keysize;
                params->key = &state->key;
                params->aux = &state->aux;

                if (!CreateThread(NULL, 0, generate_rsa_key_thread,
                                  params, 0, &threadid)) {
                    MessageBox(hwnd, "Out of thread resources",
                               "Key generation error",
                               MB_OK | MB_ICONERROR);
                    free(params);
                } else {
                    state->generation_thread_exists = TRUE;
                }
            }
        }
        break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC_COMMENTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
                state = (struct MainDlgState *)
                    GetWindowLong(hwnd, GWL_USERDATA);
                if (state->key_exists) {
                    HWND editctl = GetDlgItem(hwnd, IDC_COMMENTEDIT);
                    int len = GetWindowTextLength(editctl);
                    if (state->key.comment)
                        free(state->key.comment);
                    state->key.comment = malloc(len+1);
                    GetWindowText(editctl, state->key.comment, len+1);
                }                
            }
	    break;
	  case IDC_ABOUT:
	    EnableWindow(hwnd, 0);
	    DialogBox (hinst, MAKEINTRESOURCE(213), NULL, AboutProc);
	    EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
	    return 0;
          case IDC_GENERATE:
            state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
            if (!state->generation_thread_exists) {
                BOOL ok;
                state->keysize = GetDlgItemInt(hwnd, IDC_BITS,
                                               &ok, FALSE);
                if (!ok) state->keysize = DEFAULT_KEYSIZE;
                if (state->keysize < 256) {
                    int ret = MessageBox(hwnd,
                                         "PuTTYgen will not generate a key"
                                         " smaller than 256 bits.\n"
                                         "Key length reset to 256. Continue?",
                                         "PuTTYgen Warning",
                                         MB_ICONWARNING | MB_OKCANCEL);
                    if (ret != IDOK)
                        break;
                    state->keysize = 256;
                    SetDlgItemInt(hwnd, IDC_BITS, 256, FALSE);
                }
                hidemany(hwnd, nokey_ids, TRUE);
                hidemany(hwnd, generating_ids, FALSE);
                hidemany(hwnd, gotkey_ids, TRUE);
                EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 0);
                EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 0);
                EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 0);
                state->key_exists = FALSE;
                SetDlgItemText(hwnd, IDC_GENERATING, entropy_msg);
                state->collecting_entropy = TRUE;

                /*
                 * My brief statistical tests on mouse movements
                 * suggest that there are about 2.5 bits of
                 * randomness in the x position, 2.5 in the y
                 * position, and 1.7 in the message time, making
                 * 5.7 bits of unpredictability per mouse movement.
                 * However, other people have told me it's far less
                 * than that, so I'm going to be stupidly cautious
                 * and knock that down to a nice round 2. With this
                 * method, we require two words per mouse movement,
                 * so with 2 bits per mouse movement we expect 2
                 * bits every 2 words.
                 */
                state->entropy_required = (state->keysize/2) * 2;
                state->entropy_got = 0;
                state->entropy_size = (state->entropy_required *
                                       sizeof(*state->entropy));
                state->entropy = malloc(state->entropy_size);

                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0,
                                   MAKELPARAM(0, state->entropy_required));
                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);
            }
            break;
          case IDC_SAVE:
            state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
            if (state->key_exists) {
                char filename[FILENAME_MAX];
                char passphrase[PASSPHRASE_MAXLEN];
                char passphrase2[PASSPHRASE_MAXLEN];
                GetDlgItemText(hwnd, IDC_PASSPHRASE1EDIT,
                               passphrase, sizeof(passphrase));
                GetDlgItemText(hwnd, IDC_PASSPHRASE2EDIT,
                               passphrase2, sizeof(passphrase2));
		if (strcmp(passphrase, passphrase2)) {
                    MessageBox(hwnd,
			       "The two passphrases given do not match.",
			       "PuTTYgen Error",
			       MB_OK | MB_ICONERROR);
		    break;
		}
                if (!*passphrase) {
                    int ret;
                    ret = MessageBox(hwnd,
                                     "Are you sure you want to save this key\n"
                                     "without a passphrase to protect it?",
                                     "PuTTYgen Warning",
                                     MB_YESNO | MB_ICONWARNING);
                    if (ret != IDYES)
                        break;
                }
                if (prompt_keyfile(hwnd, "Save private key as:",
                                   filename, 1)) {
		    int ret;
		    FILE *fp = fopen(filename, "r");
		    if (fp) {
			char buffer[FILENAME_MAX+80];
			fclose(fp);
			sprintf(buffer, "Overwrite existing file\n%.*s?",
				FILENAME_MAX, filename);
			ret = MessageBox(hwnd, buffer, "PuTTYgen Warning",
					 MB_YESNO | MB_ICONWARNING);
			if (ret != IDYES)
			    break;
		    }
                    ret = saversakey(filename, &state->key, &state->aux,
				     *passphrase ? passphrase : NULL);
		    if (ret <= 0) {
			MessageBox(hwnd, "Unable to save key file",
				   "PuTTYgen Error",
				   MB_OK | MB_ICONERROR);
		    }
                }
            }
            break;
          case IDC_LOAD:
            state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
            if (!state->generation_thread_exists) {
                char filename[FILENAME_MAX];
                if (prompt_keyfile(hwnd, "Load private key:",
                                   filename, 0)) {
                    char passphrase[PASSPHRASE_MAXLEN];
                    int needs_pass;
                    int ret;
                    char *comment;
                    struct PassphraseProcStruct pps;
                    struct RSAKey newkey;
                    struct RSAAux newaux;

                    needs_pass = rsakey_encrypted(filename, &comment);
                    pps.passphrase = passphrase;
                    pps.comment = comment;
                    do {
                        if (needs_pass) {
                            int dlgret;
                            dlgret = DialogBoxParam(hinst,
                                                    MAKEINTRESOURCE(210),
                                                    NULL, PassphraseProc,
                                                    (LPARAM)&pps);
                            if (!dlgret) {
                                ret = -2;
                                break;
                            }
                        } else
                            *passphrase = '\0';
                        ret = loadrsakey(filename, &newkey, &newaux,
                                         passphrase);
                    } while (ret == -1);
                    if (comment) free(comment);
                    if (ret == 0) {
                        MessageBox(NULL, "Couldn't load private key.",
                                   "PuTTYgen Error", MB_OK | MB_ICONERROR);
                    } else if (ret == 1) {
                        state->key = newkey;
                        state->aux = newaux;

                        EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 1);
                        EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 1);
                        EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 1);
                        /*
                         * Now update the key controls with all the
                         * key data.
                         */
                        {
                            char buf[128];
                            SetDlgItemText(hwnd, IDC_PASSPHRASE1EDIT,
                                           passphrase);
                            SetDlgItemText(hwnd, IDC_PASSPHRASE2EDIT,
                                           passphrase);
                            SetDlgItemText(hwnd, IDC_COMMENTEDIT,
                                           state->key.comment);
                            /*
                             * Set the key fingerprint.
                             */
                            {
                                char *savecomment = state->key.comment;
                                state->key.comment = NULL;
                                rsa_fingerprint(buf, sizeof(buf), &state->key);
                                state->key.comment = savecomment;
                            }
                            SetDlgItemText(hwnd, IDC_FINGERPRINT, buf);
                            /*
                             * Construct a decimal representation
                             * of the key, for pasting into
                             * .ssh/authorized_keys on a Unix box.
                             */
                            setupbigedit(hwnd, IDC_KEYDISPLAY, &state->key);
                        }
                        /*
                         * Finally, hide the progress bar and show
                         * the key data.
                         */
                        hidemany(hwnd, nokey_ids, TRUE);
                        hidemany(hwnd, generating_ids, TRUE);
                        hidemany(hwnd, gotkey_ids, FALSE);
                        state->key_exists = TRUE;
                    }
                }
            }
            break;
	}
	return 0;
      case WM_DONEKEY:
        state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
        state->generation_thread_exists = FALSE;
        state->key_exists = TRUE;
        SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, PROGRESSRANGE, 0);
        EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 1);
        /*
         * Invent a comment for the key. We'll do this by including
         * the date in it. This will be so horrifyingly ugly that
         * the user will immediately want to change it, which is
         * what we want :-)
         */
        state->key.comment = malloc(30);
        {
            time_t t;
            struct tm *tm;
            time(&t);
            tm = localtime(&t);
            strftime(state->key.comment, 30, "rsa-key-%Y%m%d", tm);
        }
            
        /*
         * Now update the key controls with all the key data.
         */
        {
            char buf[128];
            /*
             * Blank passphrase, initially. This isn't dangerous,
             * because we will warn (Are You Sure?) before allowing
             * the user to save an unprotected private key.
             */
            SetDlgItemText(hwnd, IDC_PASSPHRASE1EDIT, "");
            SetDlgItemText(hwnd, IDC_PASSPHRASE2EDIT, "");
            /*
             * Set the comment.
             */
            SetDlgItemText(hwnd, IDC_COMMENTEDIT, state->key.comment);
            /*
             * Set the key fingerprint.
             */
            {
                char *savecomment = state->key.comment;
                state->key.comment = NULL;
                rsa_fingerprint(buf, sizeof(buf), &state->key);
                state->key.comment = savecomment;
            }
            SetDlgItemText(hwnd, IDC_FINGERPRINT, buf);
            /*
             * Construct a decimal representation of the key, for
             * pasting into .ssh/authorized_keys on a Unix box.
             */
            setupbigedit(hwnd, IDC_KEYDISPLAY, &state->key);
        }
        /*
         * Finally, hide the progress bar and show the key data.
         */
        hidemany(hwnd, nokey_ids, TRUE);
        hidemany(hwnd, generating_ids, TRUE);
        hidemany(hwnd, gotkey_ids, FALSE);
        break;
      case WM_CLOSE:
        state = (struct MainDlgState *)GetWindowLong(hwnd, GWL_USERDATA);
        free(state);
        EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    InitCommonControls();
    hinst = inst;
    random_init();
    return DialogBox(hinst, MAKEINTRESOURCE(201), NULL, MainDlgProc) != IDOK;
}
