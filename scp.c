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
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
/* GUI Adaptation - Sept 2000 */
#include <winuser.h>
#include <winbase.h>

#define PUTTY_DO_GLOBALS
#include "putty.h"
#include "scp.h"

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

static int verbose = 0;
static int recursive = 0;
static int preserve = 0;
static int targetshouldbedirectory = 0;
static int statistics = 1;
static int portnumber = 0;
static char *password = NULL;
static int errs = 0;
static int connection_open = 0;
/* GUI Adaptation - Sept 2000 */
#define NAME_STR_MAX 2048
static char statname[NAME_STR_MAX+1];
static unsigned long statsize = 0;
static int statperct = 0;
static time_t statelapsed = 0;
static int gui_mode = 0;
static char *gui_hwnd = NULL;

static void source(char *src);
static void rsource(char *src);
static void sink(char *targ);
/* GUI Adaptation - Sept 2000 */
static void tell_char(FILE *stream, char c);
static void tell_str(FILE *stream, char *str);
static void tell_user(FILE *stream, char *fmt, ...);
static void send_char_msg(unsigned int msg_id, char c);
static void send_str_msg(unsigned int msg_id, char *str);
static void gui_update_stats(char *name, unsigned long size, int percentage, time_t elapsed);

/*
 *  This function is needed to link with ssh.c, but it never gets called.
 */
void term_out(void)
{
    abort();
}

/* GUI Adaptation - Sept 2000 */
void send_msg(HWND h, UINT message, WPARAM wParam)
{
    while (!PostMessage( h, message, wParam, 0))
        SleepEx(1000,TRUE);
}

void tell_char(FILE *stream, char c)
{
    if (!gui_mode)
	fputc(c, stream);
    else
    {
	unsigned int msg_id = WM_STD_OUT_CHAR;
	if (stream = stderr) msg_id = WM_STD_ERR_CHAR;
	send_msg( (HWND)atoi(gui_hwnd), msg_id, (WPARAM)c );
    }
}

void tell_str(FILE *stream, char *str)
{
    unsigned int i;

    for( i = 0; i < strlen(str); ++i )
	tell_char(stream, str[i]);
}

void tell_user(FILE *stream, char *fmt, ...)
{
    char str[0x100]; /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    vsprintf(str, fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stream, str);
}

void gui_update_stats(char *name, unsigned long size, int percentage, time_t elapsed)
{
    unsigned int i;

    if (strcmp(name,statname) != 0)
    {
	for( i = 0; i < strlen(name); ++i )
	    send_msg( (HWND)atoi(gui_hwnd), WM_STATS_CHAR, (WPARAM)name[i]);
	send_msg( (HWND)atoi(gui_hwnd), WM_STATS_CHAR, (WPARAM)'\n' );
	strcpy(statname,name);
    }
    if (statsize != size)
    {
	send_msg( (HWND)atoi(gui_hwnd), WM_STATS_SIZE, (WPARAM)size );
	statsize = size;
    }
    if (statelapsed != elapsed)
    {
	send_msg( (HWND)atoi(gui_hwnd), WM_STATS_ELAPSED, (WPARAM)elapsed );
	statelapsed = elapsed;
    }
    if (statperct != percentage)
    {
	send_msg( (HWND)atoi(gui_hwnd), WM_STATS_PERCENT, (WPARAM)percentage );
	statperct = percentage;
    }
}

/*
 *  Print an error message and perform a fatal exit.
 */
void fatalbox(char *fmt, ...)
{
    char str[0x100]; /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    strcpy(str, "Fatal:");
    vsprintf(str+strlen(str), fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stderr, str);

    exit(1);
}

/*
 *  Print an error message and exit after closing the SSH link.
 */
static void bump(char *fmt, ...)
{
    char str[0x100]; /* Make the size big enough */
    va_list ap;
    va_start(ap, fmt);
    strcpy(str, "Fatal:");
    vsprintf(str+strlen(str), fmt, ap);
    va_end(ap);
    strcat(str, "\n");
    tell_str(stderr, str);

    if (connection_open) {
	char ch;
	ssh_scp_send_eof();
	ssh_scp_recv(&ch, 1);
    }
    exit(1);
}

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

    /* GUI Adaptation - Sept 2000 */
    if (gui_mode) {
	if (maxlen>0) str[0] = '\0';
    } else {
	hin = GetStdHandle(STD_INPUT_HANDLE);
	hout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE)
	    bump("Cannot get standard input/output handles");

	GetConsoleMode(hin, &savemode);
	SetConsoleMode(hin, (savemode & (~ENABLE_ECHO_INPUT)) |
		       ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT);

	WriteFile(hout, prompt, strlen(prompt), &i, NULL);
	ReadFile(hin, str, maxlen-1, &i, NULL);

	SetConsoleMode(hin, savemode);

	if ((int)i > maxlen) i = maxlen-1; else i = i - 2;
	str[i] = '\0';

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

    if (host == NULL || host[0] == '\0')
	bump("Empty host name");

    /* Try to load settings for this host */
    do_defaults(host);
    if (cfg.host[0] == '\0') {
	/* No settings for this host; use defaults */
	strncpy(cfg.host, host, sizeof(cfg.host)-1);
	cfg.host[sizeof(cfg.host)-1] = '\0';
	cfg.port = 22;
    }

    /* Set username */
    if (user != NULL && user[0] != '\0') {
	strncpy(cfg.username, user, sizeof(cfg.username)-1);
	cfg.username[sizeof(cfg.username)-1] = '\0';
    } else if (cfg.username[0] == '\0') {
	bump("Empty user name");
    }

    if (cfg.protocol != PROT_SSH)
	cfg.port = 22;

    if (portnumber)
	cfg.port = portnumber;

    err = ssh_scp_init(cfg.host, cfg.port, cmd, &realhost);
    if (err != NULL)
	bump("ssh_init: %s", err);
    if (verbose && realhost != NULL)
	tell_user(stderr, "Connected to %s\n", realhost);

    connection_open = 1;
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
	gui_update_stats(name, size, ((done *100) / size), now-start);
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
	       name, done / 1024, ratebs / 1024.0,
	       etastr, pct);

	if (done == size)
	    printf("\n");
    }
}

/*
 *  Find a colon in str and return a pointer to the colon.
 *  This is used to separate hostname from filename.
 */
static char * colon(char *str)
{
    /* We ignore a leading colon, since the hostname cannot be
     empty. We also ignore a colon as second character because
     of filenames like f:myfile.txt. */
    if (str[0] == '\0' ||
	str[0] == ':' ||
	str[1] == ':')
	return (NULL);
    while (*str != '\0' &&
	   *str != ':' &&
	   *str != '/' &&
	   *str != '\\')
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
      case 0:		/* ok */
	return (0);
      default:
	rbuf[p++] = resp;
	/* fallthrough */
      case 1:		/* error */
      case 2:		/* fatal error */
	do {
	    if (ssh_scp_recv(&ch, 1) <= 0)
		bump("Protocol error: Lost connection");
	    rbuf[p++] = ch;
	} while (p < sizeof(rbuf) && ch != '\n');
	rbuf[p-1] = '\0';
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
    strcpy(str, "\01scp: ");
    vsprintf(str+strlen(str), fmt, ap);
    strcat(str, "\n");
    ssh_scp_send(str, strlen(str));
    tell_user(stderr, "%s",str);
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
    if (attr == (DWORD)-1) {
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
                /* skip . and .. */;
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
	ssh_scp_send(buf, strlen(buf));
	if (response())
	    return;
    }

    size = GetFileSize(f, NULL);
    sprintf(buf, "C0644 %lu %s\n", size, last);
    if (verbose)
	tell_user(stderr, "Sending file modes: %s", buf);
    ssh_scp_send(buf, strlen(buf));
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
	if (i + k > size) k = size - i;
	if (! ReadFile(f, transbuf, k, &j, NULL) || j != k) {
	    if (statistics) printf("\n");
	    bump("%s: Read error", src);
	}
	ssh_scp_send(transbuf, k);
	if (statistics) {
	    stat_bytes += k;
	    if (time(NULL) != stat_lasttime ||
		i + k == size) {
		stat_lasttime = time(NULL);
		print_stats(last, size, stat_bytes,
			    stat_starttime, stat_lasttime);
	    }
	}
    }
    CloseHandle(f);

    ssh_scp_send("", 1);
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
    ssh_scp_send(buf, strlen(buf));
    if (response())
	return;

    sprintf(buf, "%s/*", src);
    dir = FindFirstFile(buf, &fdat);
    ok = (dir != INVALID_HANDLE_VALUE);
    while (ok) {
	if (strcmp(fdat.cFileName, ".") == 0 ||
	    strcmp(fdat.cFileName, "..") == 0) {
	} else if (strlen(src) + 1 + strlen(fdat.cFileName) >=
		   sizeof(buf)) {
	    run_err("%s/%s: Name too long", src, fdat.cFileName);
	} else {
	    sprintf(buf, "%s/%s", src, fdat.cFileName);
	    source(buf);
	}
	ok = FindNextFile(dir, &fdat);
    }
    FindClose(dir);

    sprintf(buf, "E\n");
    ssh_scp_send(buf, strlen(buf));
    (void) response();
}

/*
 *  Execute the sink part of the SCP protocol.
 */
static void sink(char *targ)
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
    if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
	targisdir = 1;

    if (targetshouldbedirectory && !targisdir)
	bump("%s: Not a directory", targ);

    ssh_scp_send("", 1);
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
	buf[i-1] = '\0';
	switch (buf[0]) {
	  case '\01':	/* error */
	    tell_user(stderr, "%s\n", buf+1);
	    errs++;
	    continue;
	  case '\02':	/* fatal error */
	    bump("%s", buf+1);
	  case 'E':
	    ssh_scp_send("", 1);
	    return;
	  case 'T':
	    if (sscanf(buf, "T%ld %*d %ld %*d",
		       &mtime, &atime) == 2) {
		settime = 1;
		ssh_scp_send("", 1);
		goto gottime;
	    }
	    bump("Protocol error: Illegal time format");
	  case 'C':
	  case 'D':
	    break;
	  default:
	    bump("Protocol error: Expected control record");
	}

	if (sscanf(buf+1, "%u %lu %[^\n]", &mode, &size, namebuf) != 3)
	    bump("Protocol error: Illegal file descriptor format");
	if (targisdir) {
	    char t[2048];
	    strcpy(t, targ);
	    if (targ[0] != '\0')
		strcat(t, "/");
	    strcat(t, namebuf);
	    strcpy(namebuf, t);
	} else {
	    strcpy(namebuf, targ);
	}
	attr = GetFileAttributes(namebuf);
	exists = (attr != (DWORD)-1);

	if (buf[0] == 'D') {
	    if (exists && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		run_err("%s: Not a directory", namebuf);
		continue;
	    }
	    if (!exists) {
		if (! CreateDirectory(namebuf, NULL)) {
		    run_err("%s: Cannot create directory",
			    namebuf);
		    continue;
		}
	    }
	    sink(namebuf);
	    /* can we set the timestamp for directories ? */
	    continue;
	}

	f = CreateFile(namebuf, GENERIC_WRITE, 0, NULL,
		       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (f == INVALID_HANDLE_VALUE) {
	    run_err("%s: Cannot create file", namebuf);
	    continue;
	}

	ssh_scp_send("", 1);

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
	    if (i + k > size) k = size - i;
	    if (ssh_scp_recv(transbuf, k) == 0)
		bump("Lost connection");
	    if (wrerror) continue;
	    if (! WriteFile(f, transbuf, k, &j, NULL) || j != k) {
		wrerror = 1;
		if (statistics)
		    printf("\r%-25.25s | %50s\n",
			   stat_name,
			   "Write error.. waiting for end of file");
		continue;
	    }
	    if (statistics) {
		stat_bytes += k;
		if (time(NULL) > stat_lasttime ||
		    i + k == size) {
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
	ssh_scp_send("", 1);
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

    targ = argv[argc-1];

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
	    targetshouldbedirectory ? " -d" : "",
	    targ);
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
	    if (strlen(src) + strlen(fdat.cFileName) >=
		sizeof(namebuf)) {
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
	    targetshouldbedirectory ? " -d" : "",
	    src);
    do_cmd(host, user, cmd);
    sfree(cmd);

    sink(targ);
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

    cmd = smalloc(4*strlen(src) + 100);
    strcpy(cmd, "ls -la '");
    p = cmd + strlen(cmd);
    for (q = src; *q; q++) {
	if (*q == '\'') {
	    *p++ = '\''; *p++ = '\\'; *p++ = '\''; *p++ = '\'';
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
    if (LOBYTE(wsadata.wVersion) != 1 ||
	HIBYTE(wsadata.wVersion) != 1)
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
    printf("       pscp [options] source [source...] [user@]host:target\n");
    printf("       pscp [options] -ls user@host:filespec\n");
    printf("Options:\n");
    printf("  -p        preserve file attributes\n");
    printf("  -q        quiet, don't show statistics\n");
    printf("  -r        copy directories recursively\n");
    printf("  -v        show verbose messages\n");
    printf("  -P port   connect to specified port\n");
    printf("  -pw passw login with specified password\n");
    /* GUI Adaptation - Sept 2000 */
    printf("  -gui hWnd GUI mode with the windows handle for receiving messages\n");
    exit(1);
}

/*
 *  Main program (no, really?)
 */
int main(int argc, char *argv[])
{
    int i;
    int list = 0;

    default_protocol = PROT_TELNET;

    flags = FLAG_STDERR;
    ssh_get_password = &get_password;
    init_winsock();

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
	else if (strcmp(argv[i], "-h") == 0 ||
		 strcmp(argv[i], "-?") == 0)
	    usage();
	else if (strcmp(argv[i], "-P") == 0 && i+1 < argc)
	    portnumber = atoi(argv[++i]);
	else if (strcmp(argv[i], "-pw") == 0 && i+1 < argc)
	    password = argv[++i];
	else if (strcmp(argv[i], "-gui") == 0 && i+1 < argc) {
	    gui_hwnd = argv[++i];
	    gui_mode = 1;
	} else if (strcmp(argv[i], "-ls") == 0)
		list = 1;
	else if (strcmp(argv[i], "--") == 0)
	{ i++; break; }
	else
	    usage();
    }
    argc -= i;
    argv += i;

    if (list) {
	if (argc != 1)
	    usage();
	get_dir_list(argc, argv);

    } else {

	if (argc < 2)
	    usage();
	if (argc > 2)
	    targetshouldbedirectory = 1;

	if (colon(argv[argc-1]) != NULL)
	    toremote(argc, argv);
	else
	    tolocal(argc, argv);
    }

    if (connection_open) {
	char ch;
	ssh_scp_send_eof();
	ssh_scp_recv(&ch, 1);
    }
    WSACleanup();
    random_save_seed();

    /* GUI Adaptation - August 2000 */
    if (gui_mode) {
	unsigned int msg_id = WM_RET_ERR_CNT;
	if (list) msg_id = WM_LS_RET_ERR_CNT;
	while (!PostMessage( (HWND)atoi(gui_hwnd), msg_id, (WPARAM)errs, 0/*lParam*/ ) )
	    SleepEx(1000,TRUE);
    }
    return (errs == 0 ? 0 : 1);
}

/* end */
