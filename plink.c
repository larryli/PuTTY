/*
 * PLink - a command-line (stdin/stdout) variant of PuTTY.
 */

#ifndef AUTO_WINSOCK
#include <winsock2.h>
#endif
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define PUTTY_DO_GLOBALS		       /* actually _define_ globals */
#include "putty.h"
#include "storage.h"

void fatalbox (char *p, ...) {
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ", p);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}
void connection_fatal (char *p, ...) {
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ", p);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}

static char *password = NULL;

void logevent(char *string) { }

void verify_ssh_host_key(char *host, int port, char *keytype,
                         char *keystr, char *fingerprint) {
    int ret;
    HANDLE hin;
    DWORD savemode, i;

    static const char absentmsg[] =
        "The server's host key is not cached in the registry. You\n"
        "have no guarantee that the server is the computer you\n"
        "think it is.\n"
        "The server's key fingerprint is:\n"
        "%s\n"
        "If you trust this host, enter \"y\" to add the key to\n"
        "PuTTY's cache and carry on connecting.\n"
        "If you do not trust this host, enter \"n\" to abandon the\n"
        "connection.\n"
        "Continue connecting? (y/n) ";

    static const char wrongmsg[] =
        "WARNING - POTENTIAL SECURITY BREACH!\n"
        "The server's host key does not match the one PuTTY has\n"
        "cached in the registry. This means that either the\n"
        "server administrator has changed the host key, or you\n"
        "have actually connected to another computer pretending\n"
        "to be the server.\n"
        "The new key fingerprint is:\n"
        "%s\n"
        "If you were expecting this change and trust the new key,\n"
        "enter \"y\" to update PuTTY's cache and continue connecting.\n"
        "If you want to carry on connecting but without updating\n"
        "the cache, enter \"n\".\n"
        "If you want to abandon the connection completely, press\n"
        "Return to cancel. Pressing Return is the ONLY guaranteed\n"
        "safe choice.\n"
        "Update cached key? (y/n, Return cancels connection) ";

    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)                      /* success - key matched OK */
        return;

    if (ret == 2)                      /* key was different */
        fprintf(stderr, wrongmsg, fingerprint);
    if (ret == 1)                      /* key was absent */
        fprintf(stderr, absentmsg, fingerprint);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
                         ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line)-1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (ret == 2) {                    /* key was different */
        if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n') {
            if (line[0] == 'y' || line[0] == 'Y')
                store_host_key(host, port, keytype, keystr);
        } else {
            fprintf(stderr, abandoned);
            exit(0);
        }
    }
    if (ret == 1) {                    /* key was absent */
        if (line[0] == 'y' || line[0] == 'Y')
            store_host_key(host, port, keytype, keystr);
        else {
            fprintf(stderr, abandoned);
            exit(0);
        }
    }
}

HANDLE outhandle;
DWORD orig_console_mode;

void begin_session(void) {
    if (!cfg.ldisc_term)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
    else
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_console_mode);
}

void term_out(void)
{
    int reap;
    DWORD ret;
    reap = 0;
    while (reap < inbuf_head) {
        if (!WriteFile(outhandle, inbuf+reap, inbuf_head-reap, &ret, NULL))
            return;                    /* give up in panic */
        reap += ret;
    }
    inbuf_head = 0;
}

struct input_data {
    DWORD len;
    char buffer[4096];
    HANDLE event;
};

static int get_password(const char *prompt, char *str, int maxlen)
{
    HANDLE hin, hout;
    DWORD savemode, i;

    if (password) {
        static int tried_once = 0;

        if (tried_once) {
            return 0;
        } else {
            strncpy(str, password, maxlen);
            str[maxlen-1] = '\0';
            tried_once = 1;
            return 1;
        }
    }

    hin = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot get standard input/output handles");
        return 0;
    }

    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode & (~ENABLE_ECHO_INPUT)) |
                   ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT);

    WriteFile(hout, prompt, strlen(prompt), &i, NULL);
    ReadFile(hin, str, maxlen-1, &i, NULL);

    SetConsoleMode(hin, savemode);

    if ((int)i > maxlen) i = maxlen-1; else i = i - 2;
    str[i] = '\0';

    WriteFile(hout, "\r\n", 2, &i, NULL);

    return 1;
}

static DWORD WINAPI stdin_read_thread(void *param) {
    struct input_data *idata = (struct input_data *)param;
    HANDLE inhandle;

    inhandle = GetStdHandle(STD_INPUT_HANDLE);

    while (ReadFile(inhandle, idata->buffer, sizeof(idata->buffer),
                    &idata->len, NULL)) {
        SetEvent(idata->event);
    }

    idata->len = 0;
    SetEvent(idata->event);

    return 0;
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Link: command-line connection utility\n");
    printf("%s\n", ver);
    printf("Usage: plink [options] [user@]host [command]\n");
    printf("Options:\n");
    printf("  -v        show verbose messages\n");
    printf("  -ssh      force use of ssh protocol\n");
    printf("  -P port   connect to specified port\n");
    printf("  -pw passw login with specified password\n");
    exit(1);
}

int main(int argc, char **argv) {
    WSADATA wsadata;
    WORD winsock_ver;
    WSAEVENT netevent, stdinevent;
    HANDLE handles[2];
    SOCKET socket;
    DWORD threadid;
    struct input_data idata;
    int sending;
    int portnumber = -1;

    ssh_get_password = get_password;

    flags = FLAG_STDERR;
    /*
     * Process the command line.
     */
    do_defaults(NULL, &cfg);
    default_protocol = cfg.protocol;
    default_port = cfg.port;
    {
        /*
         * Override the default protocol if PLINK_PROTOCOL is set.
         */
        char *p = getenv("PLINK_PROTOCOL");
        int i;
        if (p) {
            for (i = 0; backends[i].backend != NULL; i++) {
                if (!strcmp(backends[i].name, p)) {
                    default_protocol = cfg.protocol = backends[i].protocol;
                    default_port = cfg.port = backends[i].backend->default_port;
                    break;
                }
            }
        }
    }
    while (--argc) {
        char *p = *++argv;
        if (*p == '-') {
            if (!strcmp(p, "-ssh")) {
		default_protocol = cfg.protocol = PROT_SSH;
		default_port = cfg.port = 22;
            } else if (!strcmp(p, "-telnet")) {
		default_protocol = cfg.protocol = PROT_TELNET;
		default_port = cfg.port = 23;
            } else if (!strcmp(p, "-raw")) {
		default_protocol = cfg.protocol = PROT_RAW;
	    } else if (!strcmp(p, "-v")) {
                flags |= FLAG_VERBOSE;
	    } else if (!strcmp(p, "-log")) {
                logfile = "putty.log";
            } else if (!strcmp(p, "-pw") && argc > 1) {
                --argc, password = *++argv;
            } else if (!strcmp(p, "-l") && argc > 1) {
                char *username;
                --argc, username = *++argv;
                strncpy(cfg.username, username, sizeof(cfg.username));
                cfg.username[sizeof(cfg.username)-1] = '\0';
            } else if (!strcmp(p, "-P") && argc > 1) {
                --argc, portnumber = atoi(*++argv);
            }
	} else if (*p) {
            if (!*cfg.host) {
                char *q = p;
                /*
                 * If the hostname starts with "telnet:", set the
                 * protocol to Telnet and process the string as a
                 * Telnet URL.
                 */
                if (!strncmp(q, "telnet:", 7)) {
                    char c;

                    q += 7;
                    if (q[0] == '/' && q[1] == '/')
                        q += 2;
                    cfg.protocol = PROT_TELNET;
                    p = q;
                    while (*p && *p != ':' && *p != '/') p++;
                    c = *p;
                    if (*p)
                        *p++ = '\0';
                    if (c == ':')
                        cfg.port = atoi(p);
                    else
                        cfg.port = -1;
                    strncpy (cfg.host, q, sizeof(cfg.host)-1);
                    cfg.host[sizeof(cfg.host)-1] = '\0';
                } else {
                    char *r;
                    /*
                     * Before we process the [user@]host string, we
                     * first check for the presence of a protocol
                     * prefix (a protocol name followed by ",").
                     */
                    r = strchr(p, ',');
                    if (r) {
                        int i, j;
                        for (i = 0; backends[i].backend != NULL; i++) {
                            j = strlen(backends[i].name);
                            if (j == r-p &&
                                !memcmp(backends[i].name, p, j)) {
                                default_protocol = cfg.protocol = backends[i].protocol;
                                portnumber = backends[i].backend->default_port;
                                p = r+1;
                                break;
                            }
                        }
                    }

                    /*
                     * Three cases. Either (a) there's a nonzero
                     * length string followed by an @, in which
                     * case that's user and the remainder is host.
                     * Or (b) there's only one string, not counting
                     * a potential initial @, and it exists in the
                     * saved-sessions database. Or (c) only one
                     * string and it _doesn't_ exist in the
                     * database.
                     */
                    r = strrchr(p, '@');
                    if (r == p) p++, r = NULL;   /* discount initial @ */
                    if (r == NULL) {
                        /*
                         * One string.
                         */
                        do_defaults (p, &cfg);
                        if (cfg.host[0] == '\0') {
                            /* No settings for this host; use defaults */
                            strncpy(cfg.host, p, sizeof(cfg.host)-1);
                            cfg.host[sizeof(cfg.host)-1] = '\0';
                            cfg.port = 22;
                        }
                    } else {
                        *r++ = '\0';
                        strncpy(cfg.username, p, sizeof(cfg.username)-1);
                        cfg.username[sizeof(cfg.username)-1] = '\0';
                        strncpy(cfg.host, r, sizeof(cfg.host)-1);
                        cfg.host[sizeof(cfg.host)-1] = '\0';
                        cfg.port = 22;
                    }
                }
            } else {
                int len = sizeof(cfg.remote_cmd) - 1;
                char *cp = cfg.remote_cmd;
                int len2;

                strncpy(cp, p, len); cp[len] = '\0';
                len2 = strlen(cp); len -= len2; cp += len2;
                while (--argc) {
                    if (len > 0)
                        len--, *cp++ = ' ';
                    strncpy(cp, *++argv, len); cp[len] = '\0';
                    len2 = strlen(cp); len -= len2; cp += len2;
                }
                cfg.nopty = TRUE;      /* command => no terminal */
                cfg.ldisc_term = TRUE; /* use stdin like a line buffer */
                break;                 /* done with cmdline */
            }
	}
    }

    if (!*cfg.host) {
        usage();
    }

    if (!*cfg.remote_cmd)
        flags |= FLAG_INTERACTIVE;

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    {
        int i;
        back = NULL;
        for (i = 0; backends[i].backend != NULL; i++)
            if (backends[i].protocol == cfg.protocol) {
                back = backends[i].backend;
                break;
            }
        if (back == NULL) {
            fprintf(stderr, "Internal fault: Unsupported protocol found\n");
            return 1;
        }
    }

    /*
     * Select port.
     */
    if (portnumber != -1)
        cfg.port = portnumber;

    /*
     * Initialise WinSock.
     */
    winsock_ver = MAKEWORD(2, 0);
    if (WSAStartup(winsock_ver, &wsadata)) {
	MessageBox(NULL, "Unable to initialise WinSock", "WinSock Error",
		   MB_OK | MB_ICONEXCLAMATION);
	return 1;
    }
    if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 0) {
	MessageBox(NULL, "WinSock version is incompatible with 2.0",
		   "WinSock Error", MB_OK | MB_ICONEXCLAMATION);
	WSACleanup();
	return 1;
    }

    /*
     * Start up the connection.
     */
    {
	char *error;
	char *realhost;

	error = back->init (NULL, cfg.host, cfg.port, &realhost);
	if (error) {
	    fprintf(stderr, "Unable to open connection:\n%s", error);
	    return 1;
	}
    }

    netevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    stdinevent = CreateEvent(NULL, FALSE, FALSE, NULL);

    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &orig_console_mode);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);

    /*
     * Now we must send the back end oodles of stuff.
     */
    socket = back->socket();
    /*
     * Turn off ECHO and LINE input modes. We don't care if this
     * call fails, because we know we aren't necessarily running in
     * a console.
     */
    WSAEventSelect(socket, netevent, FD_READ | FD_CLOSE);
    handles[0] = netevent;
    handles[1] = stdinevent;
    sending = FALSE;
    while (1) {
        int n;

        if (!sending && back->sendok()) {
            /*
             * Create a separate thread to read from stdin. This is
             * a total pain, but I can't find another way to do it:
             *
             *  - an overlapped ReadFile or ReadFileEx just doesn't
             *    happen; we get failure from ReadFileEx, and
             *    ReadFile blocks despite being given an OVERLAPPED
             *    structure. Perhaps we can't do overlapped reads
             *    on consoles. WHY THE HELL NOT?
             * 
             *  - WaitForMultipleObjects(netevent, console) doesn't
             *    work, because it signals the console when
             *    _anything_ happens, including mouse motions and
             *    other things that don't cause data to be readable
             *    - so we're back to ReadFile blocking.
             */
            idata.event = stdinevent;
            if (!CreateThread(NULL, 0, stdin_read_thread,
                              &idata, 0, &threadid)) {
                fprintf(stderr, "Unable to create second thread\n");
                exit(1);
            }
            sending = TRUE;
        }

        n = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (n == 0) {
            WSANETWORKEVENTS things;
            if (!WSAEnumNetworkEvents(socket, netevent, &things)) {
                if (things.lNetworkEvents & FD_READ)
                    back->msg(0, FD_READ);
                if (things.lNetworkEvents & FD_CLOSE) {
                    back->msg(0, FD_CLOSE);
                    break;
                }
            }
            term_out();
        } else if (n == 1) {
            if (idata.len > 0) {
                back->send(idata.buffer, idata.len);
            } else {
                back->special(TS_EOF);
            }
        }
    }
    WSACleanup();
    return 0;
}
