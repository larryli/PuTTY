/*
 *  scp.c  -  Scp (Secure Copy) client for PuTTY.
 *  Joris van Rantwijk, Simon Tatham
 *
 *  This is mainly based on ssh-1.2.26/scp.c by Timo Rinne & Tatu Ylonen.
 *  They, in turn, used stuff from BSD rcp.
 *
 *  Adaptations to enable connecting a GUI by L. Gunnarsson - Sept 2000
 */

#include <windows.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
/* GUI Adaptation - Sept 2000 */
#include <winuser.h>
#include <winbase.h>

#define PUTTY_DO_GLOBALS
#include "putty.h"
#include "winstuff.h"
#include "storage.h"

#define TIME_POSIX_TO_WIN(t, ft) (*(LONGLONG*)&(ft) = \
	((LONGLONG) (t) + (LONGLONG) 11644473600) * (LONGLONG) 10000000)
#define TIME_WIN_TO_POSIX(ft, t) ((t) = (unsigned long) \
	((*(LONGLONG*)&(ft)) / (LONGLONG) 10000000 - (LONGLONG) 11644473600))

/* GUI Adaptation - Sept 2000 */
#define   WM_APP_BASE		0x8000
#define   WM_STD_OUT_CHAR	( WM_APP_BASE+400 )
#define   WM_STD_ERR_CHAR	( WM_APP_BASE+401 )
#define   WM_STATS_CHAR		( WM_APP_BASE+402 )
#define   WM_STATS_SIZE 	( WM_APP_BASE+403 )
#define   WM_STATS_PERCENT	( WM_APP_BASE+404 )
#define   WM_STATS_ELAPSED	( WM_APP_BASE+405 )
#define   WM_RET_ERR_CNT	( WM_APP_BASE+406 )
#define   WM_LS_RET_ERR_CNT	( WM_APP_BASE+407 )

static int list = 0;
static int verbose = 0;
static int recursive = 0;
static int preserve = 0;
static int targetshouldbedirectory = 0;
static int statistics = 1;
static int portnumber = 0;
static char *password = NULL;
static int errs = 0;
/* GUI Adaptation - Sept 2000 */
#define NAME_STR_MAX 2048
static char statname[NAME_STR_MAX + 1];
static unsigned long statsize = 0;
static int statperct = 0;
static unsigned long statelapsed = 0;
static int gui_mode = 0;
static char *gui_hwnd = NULL;

static void source(char *src);
static void rsource(char *src);
static void sink(char *targ, char *src);
/* GUI Adaptation - Sept 2000 */
static void tell_char(FILE * stream, char c);
static void tell_str(FILE * stream, char *str);
static void tell_user(FILE * stream, char *fmt, ...);
static void send_char_msg(unsigned int msg_id, char c);
static void send_str_msg(unsigned int msg_id, char *str);
static void gui_update_stats(char *name, unsigned long size,
			     int percentage, unsigned long elapsed);

void logevent(char *string)
{
}

void ldisc_send(char *buf, int len)
{
    /*
     * This is only here because of the calls to ldisc_send(NULL,
     * 0) in ssh.c. Nothing in PSCP actually needs to use the ldisc
     * as an ldisc. So if we get called with any real data, I want
     * to know about it.
     */
    assert(len == 0);
}

void verify_ssh_host_key(char *host, int port, char *keytype,
			 char *keystr, char *fingerprint)
{
    int ret;

    static const char absentmsg[] =
	"The server's host key is not cached in the registry. You\n"
	"have no guarantee that the server is the computer you\n"
	"think it is.\n"
	"The server's key fingerprint is:\n"
	"%s\n"
	"If you trust this host, enter \"y\" to add the key to\n"
	"PuTTY's cache and carry on connecting.\n"
	"If you do not trust this host, enter \"n\" to abandon the\n"
	"connection.\n" "Continue connecting? (y/n) ";

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
	"enter Yes to update PuTTY's cache and continue connecting.\n"
	"If you want to carry on connecting but without updating\n"
	"the cache, enter No.\n"
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
	if (fgets(line, sizeof(line), stdin) &&
	    line[0] != '\0' && line[0] != '\n') {
	    if (line[0] == 'y' || line[0] == 'Y')
		store_host_key(host, port, keytype, keystr);
	} else {
	    fprintf(stderr, abandoned);
	    fflush(stderr);
	    exit(0);
	}
    }
    if (ret == 1) {		       /* key was absent */
	fprintf(stderr, absentmsg, fingerprint);
	if (fgets(line, sizeof(line), stdin) &&
	    (line[0] == 'y' || line[0] == 'Y'))
	    store_host_key(host, port, keytype, keystr);
	else {
	    fprintf(stderr, abandoned);
	    exit(0);
	}
    }
}

/* GUI Adaptation - Sept 2000 */
static void send_msg(HWND h, UINT message, WPARAM wParam)
{
    while (!PostMessage(h, message, wParam, 0))
	SleepEx(1000, TRUE);
}

static void tell_char(FILE * stream, char c)
{
    if (!gui_mode)
	fputc(c, stream);
    else {
	unsigned int msg_id = WM_STD_OUT_CHAR;
	if (stream == stderr)
	    msg_id = WM_STD_ERR_CHAR;
	send_msg((HWND) atoi(gui_hwnd), msg_id, (WPARAM) c);
    }
}

static void tell_str(FILE * stream, char *str)
{
    unsigned int i;

    for (i = 0; i < strlen(str); ++i)
	tell_char(stream, str[i]);
}

static void tell_user(FILE * stream, char *fmt, ...)
{
    char str[0x100];		       /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    vsprintf(str, fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stream, str);
}

static void gui_update_stats(char *name, unsigned long size,
			     int percentage, unsigned long elapsed)
{
    unsigned int i;

    if (strcmp(name, statname) != 0) {
	for (i = 0; i < strlen(name); ++i)
	    send_msg((HWND) atoi(gui_hwnd), WM_STATS_CHAR,
		     (WPARAM) name[i]);
	send_msg((HWND) atoi(gui_hwnd), WM_STATS_CHAR, (WPARAM) '\n');
	strcpy(statname, name);
    }
    if (statsize != size) {
	send_msg((HWND) atoi(gui_hwnd), WM_STATS_SIZE, (WPARAM) size);
	statsize = size;
    }
    if (statelapsed != elapsed) {
	send_msg((HWND) atoi(gui_hwnd), WM_STATS_ELAPSED,
		 (WPARAM) elapsed);
	statelapsed = elapsed;
    }
    if (statperct != percentage) {
	send_msg((HWND) atoi(gui_hwnd), WM_STATS_PERCENT,
		 (WPARAM) percentage);
	statperct = percentage;
    }
}

/*
 *  Print an error message and perform a fatal exit.
 */
void fatalbox(char *fmt, ...)
{
    char str[0x100];		       /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    strcpy(str, "Fatal:");
    vsprintf(str + strlen(str), fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stderr, str);
    errs++;

    if (gui_mode) {
	unsigned int msg_id = WM_RET_ERR_CNT;
	if (list)
	    msg_id = WM_LS_RET_ERR_CNT;
	while (!PostMessage
	       ((HWND) atoi(gui_hwnd), msg_id, (WPARAM) errs,
		0 /*lParam */ ))SleepEx(1000, TRUE);
    }

    exit(1);
}
void connection_fatal(char *fmt, ...)
{
    char str[0x100];		       /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    strcpy(str, "Fatal:");
    vsprintf(str + strlen(str), fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stderr, str);
    errs++;

    if (gui_mode) {
	unsigned int msg_id = WM_RET_ERR_CNT;
	if (list)
	    msg_id = WM_LS_RET_ERR_CNT;
	while (!PostMessage
	       ((HWND) atoi(gui_hwnd), msg_id, (WPARAM) errs,
		0 /*lParam */ ))SleepEx(1000, TRUE);
    }

    exit(1);
}

/*
 * Be told what socket we're supposed to be using.
 */
static SOCKET scp_ssh_socket;
char *do_select(SOCKET skt, int startup)
{
    if (startup)
	scp_ssh_socket = skt;
    else
	scp_ssh_socket = INVALID_SOCKET;
    return NULL;
}
extern int select_result(WPARAM, LPARAM);

/*
 * Receive a block of data from the SSH link. Block until all data
 * is available.
 *
 * To do this, we repeatedly call the SSH protocol module, with our
 * own trap in from_backend() to catch the data that comes back. We
 * do this until we have enough data.
 */

static unsigned char *outptr;	       /* where to put the data */
static unsigned outlen;		       /* how much data required */
static unsigned char *pending = NULL;  /* any spare data */
static unsigned pendlen = 0, pendsize = 0;	/* length and phys. size of buffer */
void from_backend(int is_stderr, char *data, int datalen)
{
    unsigned char *p = (unsigned char *) data;
    unsigned len = (unsigned) datalen;

    /*
     * stderr data is just spouted to local stderr and otherwise
     * ignored.
     */
    if (is_stderr) {
	fwrite(data, 1, len, stderr);
	return;
    }

    inbuf_head = 0;

    /*
     * If this is before the real session begins, just return.
     */
    if (!outptr)
	return;

    if (outlen > 0) {
	unsigned used = outlen;
	if (used > len)
	    used = len;
	memcpy(outptr, p, used);
	outptr += used;
	outlen -= used;
	p += used;
	len -= used;
    }

    if (len > 0) {
	if (pendsize < pendlen + len) {
	    pendsize = pendlen + len + 4096;
	    pending = (pending ? srealloc(pending, pendsize) :
		       smalloc(pendsize));
	    if (!pending)
		fatalbox("Out of memory");
	}
	memcpy(pending + pendlen, p, len);
	pendlen += len;
    }
}
static int ssh_scp_recv(unsigned char *buf, int len)
{
    outptr = buf;
    outlen = len;

    /*
     * See if the pending-input block contains some of what we
     * need.
     */
    if (pendlen > 0) {
	unsigned pendused = pendlen;
	if (pendused > outlen)
	    pendused = outlen;
	memcpy(outptr, pending, pendused);
	memmove(pending, pending + pendused, pendlen - pendused);
	outptr += pendused;
	outlen -= pendused;
	pendlen -= pendused;
	if (pendlen == 0) {
	    pendsize = 0;
	    sfree(pending);
	    pending = NULL;
	}
	if (outlen == 0)
	    return len;
    }

    while (outlen > 0) {
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(scp_ssh_socket, &readfds);
	if (select(1, &readfds, NULL, NULL, NULL) < 0)
	    return 0;		       /* doom */
	select_result((WPARAM) scp_ssh_socket, (LPARAM) FD_READ);
    }

    return len;
}

/*
 * Loop through the ssh connection and authentication process.
 */
static void ssh_scp_init(void)
{
    if (scp_ssh_socket == INVALID_SOCKET)
	return;
    while (!back->sendok()) {
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(scp_ssh_socket, &readfds);
	if (select(1, &readfds, NULL, NULL, NULL) < 0)
	    return;		       /* doom */
	select_result((WPARAM) scp_ssh_socket, (LPARAM) FD_READ);
    }
}

/*
 *  Print an error message and exit after closing the SSH link.
 */
static void bump(char *fmt, ...)
{
    char str[0x100];		       /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    strcpy(str, "Fatal:");
    vsprintf(str + strlen(str), fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stderr, str);
    errs++;

    if (back != NULL && back->socket() != NULL) {
	char ch;
	back->special(TS_EOF);
	ssh_scp_recv(&ch, 1);
    }

    if (gui_mode) {
	unsigned int msg_id = WM_RET_ERR_CNT;
	if (list)
	    msg_id = WM_LS_RET_ERR_CNT;
	while (!PostMessage
	       ((HWND) atoi(gui_hwnd), msg_id, (WPARAM) errs,
		0 /*lParam */ ))SleepEx(1000, TRUE);
    }

    exit(1);
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

    /* GUI Adaptation - Sept 2000 */
    if (gui_mode) {
	if (maxlen > 0)
	    str[0] = '\0';
    } else {
	hin = GetStdHandle(STD_INPUT_HANDLE);
	hout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE)
	    bump("Cannot get standard input/output handles");

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
    }

    return 1;
}

/*
 *  Open an SSH connection to user@host and execute cmd.
 */
static void do_cmd(char *host, char *user, char *cmd)
{
    char *err, *realhost;
    DWORD namelen;

    if (host == NULL || host[0] == '\0')
	bump("Empty host name");

    /* Try to load settings for this host */
    do_defaults(host, &cfg);
    if (cfg.host[0] == '\0') {
	/* No settings for this host; use defaults */
	do_defaults(NULL, &cfg);
	strncpy(cfg.host, host, sizeof(cfg.host) - 1);
	cfg.host[sizeof(cfg.host) - 1] = '\0';
	cfg.port = 22;
    }

    /* Set username */
    if (user != NULL && user[0] != '\0') {
	strncpy(cfg.username, user, sizeof(cfg.username) - 1);
	cfg.username[sizeof(cfg.username) - 1] = '\0';
    } else if (cfg.username[0] == '\0') {
	namelen = 0;
	if (GetUserName(user, &namelen) == FALSE)
	    bump("Empty user name");
	user = smalloc(namelen * sizeof(char));
	GetUserName(user, &namelen);
	if (verbose)
	    tell_user(stderr, "Guessing user name: %s", user);
	strncpy(cfg.username, user, sizeof(cfg.username) - 1);
	cfg.username[sizeof(cfg.username) - 1] = '\0';
	free(user);
    }

    if (cfg.protocol != PROT_SSH)
	cfg.port = 22;

    if (portnumber)
	cfg.port = portnumber;

    strncpy(cfg.remote_cmd, cmd, sizeof(cfg.remote_cmd));
    cfg.remote_cmd[sizeof(cfg.remote_cmd) - 1] = '\0';
    cfg.nopty = TRUE;

    back = &ssh_backend;

    err = back->init(cfg.host, cfg.port, &realhost);
    if (err != NULL)
	bump("ssh_init: %s", err);
    ssh_scp_init();
    if (verbose && realhost != NULL)
	tell_user(stderr, "Connected to %s\n", realhost);
    sfree(realhost);
}

/*
 *  Update statistic information about current file.
 */
static void print_stats(char *name, unsigned long size, unsigned long done,
			time_t start, time_t now)
{
    float ratebs;
    unsigned long eta;
    char etastr[10];
    int pct;

    /* GUI Adaptation - Sept 2000 */
    if (gui_mode)
	gui_update_stats(name, size, (int) (100 * (done * 1.0 / size)),
			 (unsigned long) difftime(now, start));
    else {
	if (now > start)
	    ratebs = (float) done / (now - start);
	else
	    ratebs = (float) done;

	if (ratebs < 1.0)
	    eta = size - done;
	else
	    eta = (unsigned long) ((size - done) / ratebs);
	sprintf(etastr, "%02ld:%02ld:%02ld",
		eta / 3600, (eta % 3600) / 60, eta % 60);

	pct = (int) (100.0 * (float) done / size);

	printf("\r%-25.25s | %10ld kB | %5.1f kB/s | ETA: %8s | %3d%%",
	       name, done / 1024, ratebs / 1024.0, etastr, pct);

	if (done == size)
	    printf("\n");
    }
}

/*
 *  Find a colon in str and return a pointer to the colon.
 *  This is used to separate hostname from filename.
 */
static char *colon(char *str)
{
    /* We ignore a leading colon, since the hostname cannot be
       empty. We also ignore a colon as second character because
       of filenames like f:myfile.txt. */
    if (str[0] == '\0' || str[0] == ':' || str[1] == ':')
	return (NULL);
    while (*str != '\0' && *str != ':' && *str != '/' && *str != '\\')
	str++;
    if (*str == ':')
	return (str);
    else
	return (NULL);
}

/*
 *  Wait for a response from the other side.
 *  Return 0 if ok, -1 if error.
 */
static int response(void)
{
    char ch, resp, rbuf[2048];
    int p;

    if (ssh_scp_recv(&resp, 1) <= 0)
	bump("Lost connection");

    p = 0;
    switch (resp) {
      case 0:			       /* ok */
	return (0);
      default:
	rbuf[p++] = resp;
	/* fallthrough */
      case 1:			       /* error */
      case 2:			       /* fatal error */
	do {
	    if (ssh_scp_recv(&ch, 1) <= 0)
		bump("Protocol error: Lost connection");
	    rbuf[p++] = ch;
	} while (p < sizeof(rbuf) && ch != '\n');
	rbuf[p - 1] = '\0';
	if (resp == 1)
	    tell_user(stderr, "%s\n", rbuf);
	else
	    bump("%s", rbuf);
	errs++;
	return (-1);
    }
}

/*
 *  Send an error message to the other side and to the screen.
 *  Increment error counter.
 */
static void run_err(const char *fmt, ...)
{
    char str[2048];
    va_list ap;
    va_start(ap, fmt);
    errs++;
    strcpy(str, "scp: ");
    vsprintf(str + strlen(str), fmt, ap);
    strcat(str, "\n");
    back->send("\001", 1);	       /* scp protocol error prefix */
    back->send(str, strlen(str));
    tell_user(stderr, "%s", str);
    va_end(ap);
}

/*
 *  Execute the source part of the SCP protocol.
 */
static void source(char *src)
{
    char buf[2048];
    unsigned long size;
    char *last;
    HANDLE f;
    DWORD attr;
    unsigned long i;
    unsigned long stat_bytes;
    time_t stat_starttime, stat_lasttime;

    attr = GetFileAttributes(src);
    if (attr == (DWORD) - 1) {
	run_err("%s: No such file or directory", src);
	return;
    }

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
	if (recursive) {
	    /*
	     * Avoid . and .. directories.
	     */
	    char *p;
	    p = strrchr(src, '/');
	    if (!p)
		p = strrchr(src, '\\');
	    if (!p)
		p = src;
	    else
		p++;
	    if (!strcmp(p, ".") || !strcmp(p, ".."))
		/* skip . and .. */ ;
	    else
		rsource(src);
	} else {
	    run_err("%s: not a regular file", src);
	}
	return;
    }

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    f = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL,
		   OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) {
	run_err("%s: Cannot open file", src);
	return;
    }

    if (preserve) {
	FILETIME actime, wrtime;
	unsigned long mtime, atime;
	GetFileTime(f, NULL, &actime, &wrtime);
	TIME_WIN_TO_POSIX(actime, atime);
	TIME_WIN_TO_POSIX(wrtime, mtime);
	sprintf(buf, "T%lu 0 %lu 0\n", mtime, atime);
	back->send(buf, strlen(buf));
	if (response())
	    return;
    }

    size = GetFileSize(f, NULL);
    sprintf(buf, "C0644 %lu %s\n", size, last);
    if (verbose)
	tell_user(stderr, "Sending file modes: %s", buf);
    back->send(buf, strlen(buf));
    if (response())
	return;

    if (statistics) {
	stat_bytes = 0;
	stat_starttime = time(NULL);
	stat_lasttime = 0;
    }

    for (i = 0; i < size; i += 4096) {
	char transbuf[4096];
	DWORD j, k = 4096;
	if (i + k > size)
	    k = size - i;
	if (!ReadFile(f, transbuf, k, &j, NULL) || j != k) {
	    if (statistics)
		printf("\n");
	    bump("%s: Read error", src);
	}
	back->send(transbuf, k);
	if (statistics) {
	    stat_bytes += k;
	    if (time(NULL) != stat_lasttime || i + k == size) {
		stat_lasttime = time(NULL);
		print_stats(last, size, stat_bytes,
			    stat_starttime, stat_lasttime);
	    }
	}
    }
    CloseHandle(f);

    back->send("", 1);
    (void) response();
}

/*
 *  Recursively send the contents of a directory.
 */
static void rsource(char *src)
{
    char buf[2048];
    char *last;
    HANDLE dir;
    WIN32_FIND_DATA fdat;
    int ok;

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    /* maybe send filetime */

    sprintf(buf, "D0755 0 %s\n", last);
    if (verbose)
	tell_user(stderr, "Entering directory: %s", buf);
    back->send(buf, strlen(buf));
    if (response())
	return;

    sprintf(buf, "%s/*", src);
    dir = FindFirstFile(buf, &fdat);
    ok = (dir != INVALID_HANDLE_VALUE);
    while (ok) {
	if (strcmp(fdat.cFileName, ".") == 0 ||
	    strcmp(fdat.cFileName, "..") == 0) {
	} else if (strlen(src) + 1 + strlen(fdat.cFileName) >= sizeof(buf)) {
	    run_err("%s/%s: Name too long", src, fdat.cFileName);
	} else {
	    sprintf(buf, "%s/%s", src, fdat.cFileName);
	    source(buf);
	}
	ok = FindNextFile(dir, &fdat);
    }
    FindClose(dir);

    sprintf(buf, "E\n");
    back->send(buf, strlen(buf));
    (void) response();
}

/*
 *  Execute the sink part of the SCP protocol.
 */
static void sink(char *targ, char *src)
{
    char buf[2048];
    char namebuf[2048];
    char ch;
    int targisdir = 0;
    int settime;
    int exists;
    DWORD attr;
    HANDLE f;
    unsigned long mtime, atime;
    unsigned int mode;
    unsigned long size, i;
    int wrerror = 0;
    unsigned long stat_bytes;
    time_t stat_starttime, stat_lasttime;
    char *stat_name;

    attr = GetFileAttributes(targ);
    if (attr != (DWORD) - 1 && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
	targisdir = 1;

    if (targetshouldbedirectory && !targisdir)
	bump("%s: Not a directory", targ);

    back->send("", 1);
    while (1) {
	settime = 0;
      gottime:
	if (ssh_scp_recv(&ch, 1) <= 0)
	    return;
	if (ch == '\n')
	    bump("Protocol error: Unexpected newline");
	i = 0;
	buf[i++] = ch;
	do {
	    if (ssh_scp_recv(&ch, 1) <= 0)
		bump("Lost connection");
	    buf[i++] = ch;
	} while (i < sizeof(buf) && ch != '\n');
	buf[i - 1] = '\0';
	switch (buf[0]) {
	  case '\01':		       /* error */
	    tell_user(stderr, "%s\n", buf + 1);
	    errs++;
	    continue;
	  case '\02':		       /* fatal error */
	    bump("%s", buf + 1);
	  case 'E':
	    back->send("", 1);
	    return;
	  case 'T':
	    if (sscanf(buf, "T%ld %*d %ld %*d", &mtime, &atime) == 2) {
		settime = 1;
		back->send("", 1);
		goto gottime;
	    }
	    bump("Protocol error: Illegal time format");
	  case 'C':
	  case 'D':
	    break;
	  default:
	    bump("Protocol error: Expected control record");
	}

	if (sscanf(buf + 1, "%u %lu %[^\n]", &mode, &size, namebuf) != 3)
	    bump("Protocol error: Illegal file descriptor format");
	/* Security fix: ensure the file ends up where we asked for it. */
	if (targisdir) {
	    char t[2048];
	    char *p;
	    strcpy(t, targ);
	    if (targ[0] != '\0')
		strcat(t, "/");
	    p = namebuf + strlen(namebuf);
	    while (p > namebuf && p[-1] != '/' && p[-1] != '\\')
		p--;
	    strcat(t, p);
	    strcpy(namebuf, t);
	} else {
	    strcpy(namebuf, targ);
	}
	attr = GetFileAttributes(namebuf);
	exists = (attr != (DWORD) - 1);

	if (buf[0] == 'D') {
	    if (exists && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		run_err("%s: Not a directory", namebuf);
		continue;
	    }
	    if (!exists) {
		if (!CreateDirectory(namebuf, NULL)) {
		    run_err("%s: Cannot create directory", namebuf);
		    continue;
		}
	    }
	    sink(namebuf, NULL);
	    /* can we set the timestamp for directories ? */
	    continue;
	}

	f = CreateFile(namebuf, GENERIC_WRITE, 0, NULL,
		       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (f == INVALID_HANDLE_VALUE) {
	    run_err("%s: Cannot create file", namebuf);
	    continue;
	}

	back->send("", 1);

	if (statistics) {
	    stat_bytes = 0;
	    stat_starttime = time(NULL);
	    stat_lasttime = 0;
	    if ((stat_name = strrchr(namebuf, '/')) == NULL)
		stat_name = namebuf;
	    else
		stat_name++;
	    if (strrchr(stat_name, '\\') != NULL)
		stat_name = strrchr(stat_name, '\\') + 1;
	}

	for (i = 0; i < size; i += 4096) {
	    char transbuf[4096];
	    DWORD j, k = 4096;
	    if (i + k > size)
		k = size - i;
	    if (ssh_scp_recv(transbuf, k) == 0)
		bump("Lost connection");
	    if (wrerror)
		continue;
	    if (!WriteFile(f, transbuf, k, &j, NULL) || j != k) {
		wrerror = 1;
		if (statistics)
		    printf("\r%-25.25s | %50s\n",
			   stat_name,
			   "Write error.. waiting for end of file");
		continue;
	    }
	    if (statistics) {
		stat_bytes += k;
		if (time(NULL) > stat_lasttime || i + k == size) {
		    stat_lasttime = time(NULL);
		    print_stats(stat_name, size, stat_bytes,
				stat_starttime, stat_lasttime);
		}
	    }
	}
	(void) response();

	if (settime) {
	    FILETIME actime, wrtime;
	    TIME_POSIX_TO_WIN(atime, actime);
	    TIME_POSIX_TO_WIN(mtime, wrtime);
	    SetFileTime(f, NULL, &actime, &wrtime);
	}

	CloseHandle(f);
	if (wrerror) {
	    run_err("%s: Write error", namebuf);
	    continue;
	}
	back->send("", 1);
    }
}

/*
 *  We will copy local files to a remote server.
 */
static void toremote(int argc, char *argv[])
{
    char *src, *targ, *host, *user;
    char *cmd;
    int i;

    targ = argv[argc - 1];

    /* Separate host from filename */
    host = targ;
    targ = colon(targ);
    if (targ == NULL)
	bump("targ == NULL in toremote()");
    *targ++ = '\0';
    if (*targ == '\0')
	targ = ".";
    /* Substitute "." for emtpy target */

    /* Separate host and username */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    if (argc == 2) {
	/* Find out if the source filespec covers multiple files
	   if so, we should set the targetshouldbedirectory flag */
	HANDLE fh;
	WIN32_FIND_DATA fdat;
	if (colon(argv[0]) != NULL)
	    bump("%s: Remote to remote not supported", argv[0]);
	fh = FindFirstFile(argv[0], &fdat);
	if (fh == INVALID_HANDLE_VALUE)
	    bump("%s: No such file or directory\n", argv[0]);
	if (FindNextFile(fh, &fdat))
	    targetshouldbedirectory = 1;
	FindClose(fh);
    }

    cmd = smalloc(strlen(targ) + 100);
    sprintf(cmd, "scp%s%s%s%s -t %s",
	    verbose ? " -v" : "",
	    recursive ? " -r" : "",
	    preserve ? " -p" : "",
	    targetshouldbedirectory ? " -d" : "", targ);
    do_cmd(host, user, cmd);
    sfree(cmd);

    (void) response();

    for (i = 0; i < argc - 1; i++) {
	HANDLE dir;
	WIN32_FIND_DATA fdat;
	src = argv[i];
	if (colon(src) != NULL) {
	    tell_user(stderr, "%s: Remote to remote not supported\n", src);
	    errs++;
	    continue;
	}
	dir = FindFirstFile(src, &fdat);
	if (dir == INVALID_HANDLE_VALUE) {
	    run_err("%s: No such file or directory", src);
	    continue;
	}
	do {
	    char *last;
	    char namebuf[2048];
	    /*
	     * Ensure that . and .. are never matched by wildcards,
	     * but only by deliberate action.
	     */
	    if (!strcmp(fdat.cFileName, ".") ||
		!strcmp(fdat.cFileName, "..")) {
		/*
		 * Find*File has returned a special dir. We require
		 * that _either_ `src' ends in a backslash followed
		 * by that string, _or_ `src' is precisely that
		 * string.
		 */
		int len = strlen(src), dlen = strlen(fdat.cFileName);
		if (len == dlen && !strcmp(src, fdat.cFileName)) {
		    /* ok */ ;
		} else if (len > dlen + 1 && src[len - dlen - 1] == '\\' &&
			   !strcmp(src + len - dlen, fdat.cFileName)) {
		    /* ok */ ;
		} else
		    continue;	       /* ignore this one */
	    }
	    if (strlen(src) + strlen(fdat.cFileName) >= sizeof(namebuf)) {
		tell_user(stderr, "%s: Name too long", src);
		continue;
	    }
	    strcpy(namebuf, src);
	    if ((last = strrchr(namebuf, '/')) == NULL)
		last = namebuf;
	    else
		last++;
	    if (strrchr(last, '\\') != NULL)
		last = strrchr(last, '\\') + 1;
	    if (last == namebuf && strrchr(namebuf, ':') != NULL)
		last = strchr(namebuf, ':') + 1;
	    strcpy(last, fdat.cFileName);
	    source(namebuf);
	} while (FindNextFile(dir, &fdat));
	FindClose(dir);
    }
}

/*
 *  We will copy files from a remote server to the local machine.
 */
static void tolocal(int argc, char *argv[])
{
    char *src, *targ, *host, *user;
    char *cmd;

    if (argc != 2)
	bump("More than one remote source not supported");

    src = argv[0];
    targ = argv[1];

    /* Separate host from filename */
    host = src;
    src = colon(src);
    if (src == NULL)
	bump("Local to local copy not supported");
    *src++ = '\0';
    if (*src == '\0')
	src = ".";
    /* Substitute "." for empty filename */

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = smalloc(strlen(src) + 100);
    sprintf(cmd, "scp%s%s%s%s -f %s",
	    verbose ? " -v" : "",
	    recursive ? " -r" : "",
	    preserve ? " -p" : "",
	    targetshouldbedirectory ? " -d" : "", src);
    do_cmd(host, user, cmd);
    sfree(cmd);

    sink(targ, src);
}

/*
 *  We will issue a list command to get a remote directory.
 */
static void get_dir_list(int argc, char *argv[])
{
    char *src, *host, *user;
    char *cmd, *p, *q;
    char c;

    src = argv[0];

    /* Separate host from filename */
    host = src;
    src = colon(src);
    if (src == NULL)
	bump("Local to local copy not supported");
    *src++ = '\0';
    if (*src == '\0')
	src = ".";
    /* Substitute "." for empty filename */

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = smalloc(4 * strlen(src) + 100);
    strcpy(cmd, "ls -la '");
    p = cmd + strlen(cmd);
    for (q = src; *q; q++) {
	if (*q == '\'') {
	    *p++ = '\'';
	    *p++ = '\\';
	    *p++ = '\'';
	    *p++ = '\'';
	} else {
	    *p++ = *q;
	}
    }
    *p++ = '\'';
    *p = '\0';

    do_cmd(host, user, cmd);
    sfree(cmd);

    while (ssh_scp_recv(&c, 1) > 0)
	tell_char(stdout, c);
}

/*
 *  Initialize the Win$ock driver.
 */
static void init_winsock(void)
{
    WORD winsock_ver;
    WSADATA wsadata;

    winsock_ver = MAKEWORD(1, 1);
    if (WSAStartup(winsock_ver, &wsadata))
	bump("Unable to initialise WinSock");
    if (LOBYTE(wsadata.wVersion) != 1 || HIBYTE(wsadata.wVersion) != 1)
	bump("WinSock version is incompatible with 1.1");
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Secure Copy client\n");
    printf("%s\n", ver);
    printf("Usage: pscp [options] [user@]host:source target\n");
    printf
	("       pscp [options] source [source...] [user@]host:target\n");
    printf("       pscp [options] -ls user@host:filespec\n");
    printf("Options:\n");
    printf("  -p        preserve file attributes\n");
    printf("  -q        quiet, don't show statistics\n");
    printf("  -r        copy directories recursively\n");
    printf("  -v        show verbose messages\n");
    printf("  -P port   connect to specified port\n");
    printf("  -pw passw login with specified password\n");
#if 0
    /*
     * -gui is an internal option, used by GUI front ends to get
     * pscp to pass progress reports back to them. It's not an
     * ordinary user-accessible option, so it shouldn't be part of
     * the command-line help. The only people who need to know
     * about it are programmers, and they can read the source.
     */
    printf
	("  -gui hWnd GUI mode with the windows handle for receiving messages\n");
#endif
    exit(1);
}

/*
 *  Main program (no, really?)
 */
int main(int argc, char *argv[])
{
    int i;

    default_protocol = PROT_TELNET;

    flags = FLAG_STDERR;
    ssh_get_line = &get_line;
    init_winsock();
    sk_init();

    for (i = 1; i < argc; i++) {
	if (argv[i][0] != '-')
	    break;
	if (strcmp(argv[i], "-v") == 0)
	    verbose = 1, flags |= FLAG_VERBOSE;
	else if (strcmp(argv[i], "-r") == 0)
	    recursive = 1;
	else if (strcmp(argv[i], "-p") == 0)
	    preserve = 1;
	else if (strcmp(argv[i], "-q") == 0)
	    statistics = 0;
	else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-?") == 0)
	    usage();
	else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc)
	    portnumber = atoi(argv[++i]);
	else if (strcmp(argv[i], "-pw") == 0 && i + 1 < argc)
	    password = argv[++i];
	else if (strcmp(argv[i], "-gui") == 0 && i + 1 < argc) {
	    gui_hwnd = argv[++i];
	    gui_mode = 1;
	} else if (strcmp(argv[i], "-ls") == 0)
	    list = 1;
	else if (strcmp(argv[i], "--") == 0) {
	    i++;
	    break;
	} else
	    usage();
    }
    argc -= i;
    argv += i;
    back = NULL;

    if (list) {
	if (argc != 1)
	    usage();
	get_dir_list(argc, argv);

    } else {

	if (argc < 2)
	    usage();
	if (argc > 2)
	    targetshouldbedirectory = 1;

	if (colon(argv[argc - 1]) != NULL)
	    toremote(argc, argv);
	else
	    tolocal(argc, argv);
    }

    if (back != NULL && back->socket() != NULL) {
	char ch;
	back->special(TS_EOF);
	ssh_scp_recv(&ch, 1);
    }
    WSACleanup();
    random_save_seed();

    /* GUI Adaptation - August 2000 */
    if (gui_mode) {
	unsigned int msg_id = WM_RET_ERR_CNT;
	if (list)
	    msg_id = WM_LS_RET_ERR_CNT;
	while (!PostMessage
	       ((HWND) atoi(gui_hwnd), msg_id, (WPARAM) errs,
		0 /*lParam */ ))SleepEx(1000, TRUE);
    }
    return (errs == 0 ? 0 : 1);
}

/* end */
