/*
 * PLink - a command-line (stdin/stdout) variant of PuTTY.
 */

#ifndef AUTO_WINSOCK
#include <winsock2.h>
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"
#include "storage.h"
#include "tree234.h"

#define MAX_STDIN_BACKLOG 4096

void fatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}
void connection_fatal(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}

static char *password = NULL;

void logevent(char *string)
{
}

void verify_ssh_host_key(char *host, int port, char *keytype,
			 char *keystr, char *fingerprint)
{
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
	"If you want to carry on connecting just once, without\n"
	"adding the key to the cache, enter \"n\".\n"
	"If you do not trust this host, press Return to abandon the\n"
	"connection.\n"
	"Store key in cache? (y/n) ";

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

    if (ret == 0)		       /* success - key matched OK */
	return;

    if (ret == 2) {		       /* key was different */
	fprintf(stderr, wrongmsg, fingerprint);
	fflush(stderr);
    }
    if (ret == 1) {		       /* key was absent */
	fprintf(stderr, absentmsg, fingerprint);
	fflush(stderr);
    }

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n') {
	if (line[0] == 'y' || line[0] == 'Y')
	    store_host_key(host, port, keytype, keystr);
    } else {
	fprintf(stderr, abandoned);
	exit(0);
    }
}

/*
 * Ask whether the selected cipher is acceptable (since it was
 * below the configured 'warn' threshold).
 * cs: 0 = both ways, 1 = client->server, 2 = server->client
 */
void askcipher(char *ciphername, int cs)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msg[] =
	"The first %scipher supported by the server is\n"
	"%s, which is below the configured warning threshold.\n"
	"Continue with connection? (y/n) ";
    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    fprintf(stderr, msg,
	    (cs == 0) ? "" :
	    (cs == 1) ? "client-to-server " :
			"server-to-client ",
	    ciphername);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
	return;
    } else {
	fprintf(stderr, abandoned);
	exit(0);
    }
}

HANDLE inhandle, outhandle, errhandle;
DWORD orig_console_mode;

WSAEVENT netevent;

int term_ldisc(int mode)
{
    return FALSE;
}
void ldisc_update(int echo, int edit)
{
    /* Update stdin read mode to reflect changes in line discipline. */
    DWORD mode;

    mode = ENABLE_PROCESSED_INPUT;
    if (echo)
	mode = mode | ENABLE_ECHO_INPUT;
    else
	mode = mode & ~ENABLE_ECHO_INPUT;
    if (edit)
	mode = mode | ENABLE_LINE_INPUT;
    else
	mode = mode & ~ENABLE_LINE_INPUT;
    SetConsoleMode(inhandle, mode);
}

static int get_line(const char *prompt, char *str, int maxlen, int is_pw)
{
    HANDLE hin, hout;
    DWORD savemode, newmode, i;

    if (is_pw && password) {
	static int tried_once = 0;

	if (tried_once) {
	    return 0;
	} else {
	    strncpy(str, password, maxlen);
	    str[maxlen - 1] = '\0';
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
    newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
    if (is_pw)
	newmode &= ~ENABLE_ECHO_INPUT;
    else
	newmode |= ENABLE_ECHO_INPUT;
    SetConsoleMode(hin, newmode);

    WriteFile(hout, prompt, strlen(prompt), &i, NULL);
    ReadFile(hin, str, maxlen - 1, &i, NULL);

    SetConsoleMode(hin, savemode);

    if ((int) i > maxlen)
	i = maxlen - 1;
    else
	i = i - 2;
    str[i] = '\0';

    if (is_pw)
	WriteFile(hout, "\r\n", 2, &i, NULL);

    return 1;
}

struct input_data {
    DWORD len;
    char buffer[4096];
    HANDLE event, eventback;
};

static DWORD WINAPI stdin_read_thread(void *param)
{
    struct input_data *idata = (struct input_data *) param;
    HANDLE inhandle;

    inhandle = GetStdHandle(STD_INPUT_HANDLE);

    while (ReadFile(inhandle, idata->buffer, sizeof(idata->buffer),
		    &idata->len, NULL) && idata->len > 0) {
	SetEvent(idata->event);
	WaitForSingleObject(idata->eventback, INFINITE);
    }

    idata->len = 0;
    SetEvent(idata->event);

    return 0;
}

struct output_data {
    DWORD len, lenwritten;
    int writeret;
    char *buffer;
    int is_stderr, done;
    HANDLE event, eventback;
    int busy;
};

static DWORD WINAPI stdout_write_thread(void *param)
{
    struct output_data *odata = (struct output_data *) param;
    HANDLE outhandle, errhandle;

    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);
    errhandle = GetStdHandle(STD_ERROR_HANDLE);

    while (1) {
	WaitForSingleObject(odata->eventback, INFINITE);
	if (odata->done)
	    break;
	odata->writeret =
	    WriteFile(odata->is_stderr ? errhandle : outhandle,
		      odata->buffer, odata->len, &odata->lenwritten, NULL);
	SetEvent(odata->event);
    }

    return 0;
}

bufchain stdout_data, stderr_data;
struct output_data odata, edata;

void try_output(int is_stderr)
{
    struct output_data *data = (is_stderr ? &edata : &odata);
    void *senddata;
    int sendlen;

    if (!data->busy) {
	bufchain_prefix(is_stderr ? &stderr_data : &stdout_data,
			&senddata, &sendlen);
	data->buffer = senddata;
	data->len = sendlen;
	SetEvent(data->eventback);
	data->busy = 1;
    }
}

int from_backend(int is_stderr, char *data, int len)
{
    int pos;
    DWORD ret;
    HANDLE h = (is_stderr ? errhandle : outhandle);
    void *writedata;
    int writelen;
    int osize, esize;

    if (is_stderr) {
	bufchain_add(&stderr_data, data, len);
	try_output(1);
    } else {
	bufchain_add(&stdout_data, data, len);
	try_output(0);
    }

    osize = bufchain_size(&stdout_data);
    esize = bufchain_size(&stderr_data);

    return osize + esize;
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
    printf("  -m file   read remote command(s) from file\n");
    exit(1);
}

char *do_select(SOCKET skt, int startup)
{
    int events;
    if (startup) {
	events = FD_READ | FD_WRITE | FD_OOB | FD_CLOSE | FD_ACCEPT;
    } else {
	events = 0;
    }
    if (WSAEventSelect(skt, netevent, events) == SOCKET_ERROR) {
	switch (WSAGetLastError()) {
	  case WSAENETDOWN:
	    return "Network is down";
	  default:
	    return "WSAAsyncSelect(): unknown error";
	}
    }
    return NULL;
}

int main(int argc, char **argv)
{
    WSADATA wsadata;
    WORD winsock_ver;
    WSAEVENT stdinevent, stdoutevent, stderrevent;
    HANDLE handles[4];
    DWORD in_threadid, out_threadid, err_threadid;
    struct input_data idata;
    int reading;
    int sending;
    int portnumber = -1;
    SOCKET *sklist;
    int skcount, sksize;
    int connopen;

    ssh_get_line = get_line;

    sklist = NULL;
    skcount = sksize = 0;
    /*
     * Initialise port and protocol to sensible defaults. (These
     * will be overridden by more or less anything.)
     */
    default_protocol = PROT_SSH;
    default_port = 22;

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
		    default_port = cfg.port =
			backends[i].backend->default_port;
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
		cfg.username[sizeof(cfg.username) - 1] = '\0';
	    } else if (!strcmp(p, "-m") && argc > 1) {
		char *filename, *command;
		int cmdlen, cmdsize;
		FILE *fp;
		int c, d;

		--argc, filename = *++argv;

		cmdlen = cmdsize = 0;
		command = NULL;
		fp = fopen(filename, "r");
		if (!fp) {
		    fprintf(stderr, "plink: unable to open command "
			    "file \"%s\"\n", filename);
		    return 1;
		}
		do {
		    c = fgetc(fp);
		    d = c;
		    if (c == EOF)
			d = 0;
		    if (cmdlen >= cmdsize) {
			cmdsize = cmdlen + 512;
			command = srealloc(command, cmdsize);
		    }
		    command[cmdlen++] = d;
		} while (c != EOF);
		cfg.remote_cmd_ptr = command;
		cfg.nopty = TRUE;      /* command => no terminal */
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
		    while (*p && *p != ':' && *p != '/')
			p++;
		    c = *p;
		    if (*p)
			*p++ = '\0';
		    if (c == ':')
			cfg.port = atoi(p);
		    else
			cfg.port = -1;
		    strncpy(cfg.host, q, sizeof(cfg.host) - 1);
		    cfg.host[sizeof(cfg.host) - 1] = '\0';
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
			    if (j == r - p &&
				!memcmp(backends[i].name, p, j)) {
				default_protocol = cfg.protocol =
				    backends[i].protocol;
				portnumber =
				    backends[i].backend->default_port;
				p = r + 1;
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
		    if (r == p)
			p++, r = NULL; /* discount initial @ */
		    if (r == NULL) {
			/*
			 * One string.
			 */
			Config cfg2;
			do_defaults(p, &cfg2);
			if (cfg2.host[0] == '\0') {
			    /* No settings for this host; use defaults */
			    strncpy(cfg.host, p, sizeof(cfg.host) - 1);
			    cfg.host[sizeof(cfg.host) - 1] = '\0';
			    cfg.port = default_port;
			} else {
			    cfg = cfg2;
			    cfg.remote_cmd_ptr = cfg.remote_cmd;
			}
		    } else {
			*r++ = '\0';
			strncpy(cfg.username, p, sizeof(cfg.username) - 1);
			cfg.username[sizeof(cfg.username) - 1] = '\0';
			strncpy(cfg.host, r, sizeof(cfg.host) - 1);
			cfg.host[sizeof(cfg.host) - 1] = '\0';
			cfg.port = default_port;
		    }
		}
	    } else {
		int len = sizeof(cfg.remote_cmd) - 1;
		char *cp = cfg.remote_cmd;
		int len2;

		strncpy(cp, p, len);
		cp[len] = '\0';
		len2 = strlen(cp);
		len -= len2;
		cp += len2;
		while (--argc) {
		    if (len > 0)
			len--, *cp++ = ' ';
		    strncpy(cp, *++argv, len);
		    cp[len] = '\0';
		    len2 = strlen(cp);
		    len -= len2;
		    cp += len2;
		}
		cfg.nopty = TRUE;      /* command => no terminal */
		break;		       /* done with cmdline */
	    }
	}
    }

    if (!*cfg.host) {
	usage();
    }

    if (!*cfg.remote_cmd_ptr)
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
	    fprintf(stderr,
		    "Internal fault: Unsupported protocol found\n");
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
    sk_init();

    /*
     * Start up the connection.
     */
    netevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    {
	char *error;
	char *realhost;

	error = back->init(cfg.host, cfg.port, &realhost);
	if (error) {
	    fprintf(stderr, "Unable to open connection:\n%s", error);
	    return 1;
	}
	sfree(realhost);
    }
    connopen = 1;

    stdinevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    stdoutevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    stderrevent = CreateEvent(NULL, FALSE, FALSE, NULL);

    inhandle = GetStdHandle(STD_INPUT_HANDLE);
    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);
    errhandle = GetStdHandle(STD_ERROR_HANDLE);
    GetConsoleMode(inhandle, &orig_console_mode);
    SetConsoleMode(inhandle, ENABLE_PROCESSED_INPUT);

    /*
     * Turn off ECHO and LINE input modes. We don't care if this
     * call fails, because we know we aren't necessarily running in
     * a console.
     */
    handles[0] = netevent;
    handles[1] = stdinevent;
    handles[2] = stdoutevent;
    handles[3] = stderrevent;
    sending = FALSE;

    /*
     * Create spare threads to write to stdout and stderr, so we
     * can arrange asynchronous writes.
     */
    odata.event = stdoutevent;
    odata.eventback = CreateEvent(NULL, FALSE, FALSE, NULL);
    odata.is_stderr = 0;
    odata.busy = odata.done = 0;
    if (!CreateThread(NULL, 0, stdout_write_thread,
		      &odata, 0, &out_threadid)) {
	fprintf(stderr, "Unable to create output thread\n");
	exit(1);
    }
    edata.event = stderrevent;
    edata.eventback = CreateEvent(NULL, FALSE, FALSE, NULL);
    edata.is_stderr = 1;
    edata.busy = edata.done = 0;
    if (!CreateThread(NULL, 0, stdout_write_thread,
		      &edata, 0, &err_threadid)) {
	fprintf(stderr, "Unable to create error output thread\n");
	exit(1);
    }

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
	    idata.eventback = CreateEvent(NULL, FALSE, FALSE, NULL);
	    if (!CreateThread(NULL, 0, stdin_read_thread,
			      &idata, 0, &in_threadid)) {
		fprintf(stderr, "Unable to create input thread\n");
		exit(1);
	    }
	    sending = TRUE;
	}

	n = WaitForMultipleObjects(4, handles, FALSE, INFINITE);
	if (n == 0) {
	    WSANETWORKEVENTS things;
	    SOCKET socket;
	    extern SOCKET first_socket(int *), next_socket(int *);
	    extern int select_result(WPARAM, LPARAM);
	    int i, socketstate;

	    /*
	     * We must not call select_result() for any socket
	     * until we have finished enumerating within the tree.
	     * This is because select_result() may close the socket
	     * and modify the tree.
	     */
	    /* Count the active sockets. */
	    i = 0;
	    for (socket = first_socket(&socketstate);
		 socket != INVALID_SOCKET;
		 socket = next_socket(&socketstate)) i++;

	    /* Expand the buffer if necessary. */
	    if (i > sksize) {
		sksize = i + 16;
		sklist = srealloc(sklist, sksize * sizeof(*sklist));
	    }

	    /* Retrieve the sockets into sklist. */
	    skcount = 0;
	    for (socket = first_socket(&socketstate);
		 socket != INVALID_SOCKET;
		 socket = next_socket(&socketstate)) {
		sklist[skcount++] = socket;
	    }

	    /* Now we're done enumerating; go through the list. */
	    for (i = 0; i < skcount; i++) {
		WPARAM wp;
		socket = sklist[i];
		wp = (WPARAM) socket;
		if (!WSAEnumNetworkEvents(socket, NULL, &things)) {
		    noise_ultralight(socket);
		    noise_ultralight(things.lNetworkEvents);
		    if (things.lNetworkEvents & FD_READ)
			connopen &= select_result(wp, (LPARAM) FD_READ);
		    if (things.lNetworkEvents & FD_CLOSE)
			connopen &= select_result(wp, (LPARAM) FD_CLOSE);
		    if (things.lNetworkEvents & FD_OOB)
			connopen &= select_result(wp, (LPARAM) FD_OOB);
		    if (things.lNetworkEvents & FD_WRITE)
			connopen &= select_result(wp, (LPARAM) FD_WRITE);
    		    if (things.lNetworkEvents & FD_ACCEPT)
			connopen &= select_result(wp, (LPARAM) FD_ACCEPT);

		}
	    }
	} else if (n == 1) {
	    reading = 0;
	    noise_ultralight(idata.len);
	    if (idata.len > 0) {
		back->send(idata.buffer, idata.len);
	    } else {
		back->special(TS_EOF);
	    }
	} else if (n == 2) {
	    odata.busy = 0;
	    if (!odata.writeret) {
		fprintf(stderr, "Unable to write to standard output\n");
		exit(0);
	    }
	    bufchain_consume(&stdout_data, odata.lenwritten);
	    if (bufchain_size(&stdout_data) > 0)
		try_output(0);
	    back->unthrottle(bufchain_size(&stdout_data) +
			     bufchain_size(&stderr_data));
	} else if (n == 3) {
	    edata.busy = 0;
	    if (!edata.writeret) {
		fprintf(stderr, "Unable to write to standard output\n");
		exit(0);
	    }
	    bufchain_consume(&stderr_data, edata.lenwritten);
	    if (bufchain_size(&stderr_data) > 0)
		try_output(1);
	    back->unthrottle(bufchain_size(&stdout_data) +
			     bufchain_size(&stderr_data));
	}
	if (!reading && back->sendbuffer() < MAX_STDIN_BACKLOG) {
	    SetEvent(idata.eventback);
	    reading = 1;
	}
	if (!connopen || back->socket() == NULL)
	    break;		       /* we closed the connection */
    }
    WSACleanup();
    return 0;
}
