/*
 * psftp.c: front end for PSFTP.
 */

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#define PUTTY_DO_GLOBALS
#include "putty.h"
#include "storage.h"
#include "ssh.h"
#include "sftp.h"
#include "int64.h"

/* ----------------------------------------------------------------------
 * String handling routines.
 */

char *dupstr(char *s)
{
    int len = strlen(s);
    char *p = smalloc(len + 1);
    strcpy(p, s);
    return p;
}

/* Allocate the concatenation of N strings. Terminate arg list with NULL. */
char *dupcat(char *s1, ...)
{
    int len;
    char *p, *q, *sn;
    va_list ap;

    len = strlen(s1);
    va_start(ap, s1);
    while (1) {
	sn = va_arg(ap, char *);
	if (!sn)
	    break;
	len += strlen(sn);
    }
    va_end(ap);

    p = smalloc(len + 1);
    strcpy(p, s1);
    q = p + strlen(p);

    va_start(ap, s1);
    while (1) {
	sn = va_arg(ap, char *);
	if (!sn)
	    break;
	strcpy(q, sn);
	q += strlen(q);
    }
    va_end(ap);

    return p;
}

/* ----------------------------------------------------------------------
 * sftp client state.
 */

char *pwd, *homedir;

/* ----------------------------------------------------------------------
 * Higher-level helper functions used in commands.
 */

/*
 * Attempt to canonify a pathname starting from the pwd. If
 * canonification fails, at least fall back to returning a _valid_
 * pathname (though it may be ugly, eg /home/simon/../foobar).
 */
char *canonify(char *name)
{
    char *fullname, *canonname;

    if (name[0] == '/') {
	fullname = dupstr(name);
    } else {
	char *slash;
	if (pwd[strlen(pwd) - 1] == '/')
	    slash = "";
	else
	    slash = "/";
	fullname = dupcat(pwd, slash, name, NULL);
    }

    canonname = fxp_realpath(fullname);

    if (canonname) {
	sfree(fullname);
	return canonname;
    } else {
	/*
	 * Attempt number 2. Some FXP_REALPATH implementations
	 * (glibc-based ones, in particular) require the _whole_
	 * path to point to something that exists, whereas others
	 * (BSD-based) only require all but the last component to
	 * exist. So if the first call failed, we should strip off
	 * everything from the last slash onwards and try again,
	 * then put the final component back on.
	 * 
	 * Special cases:
	 * 
	 *  - if the last component is "/." or "/..", then we don't
	 *    bother trying this because there's no way it can work.
	 * 
	 *  - if the thing actually ends with a "/", we remove it
	 *    before we start. Except if the string is "/" itself
	 *    (although I can't see why we'd have got here if so,
	 *    because surely "/" would have worked the first
	 *    time?), in which case we don't bother.
	 * 
	 *  - if there's no slash in the string at all, give up in
	 *    confusion (we expect at least one because of the way
	 *    we constructed the string).
	 */

	int i;
	char *returnname;

	i = strlen(fullname);
	if (i > 2 && fullname[i - 1] == '/')
	    fullname[--i] = '\0';      /* strip trailing / unless at pos 0 */
	while (i > 0 && fullname[--i] != '/');

	/*
	 * Give up on special cases.
	 */
	if (fullname[i] != '/' ||      /* no slash at all */
	    !strcmp(fullname + i, "/.") ||	/* ends in /. */
	    !strcmp(fullname + i, "/..") ||	/* ends in /.. */
	    !strcmp(fullname, "/")) {
	    return fullname;
	}

	/*
	 * Now i points at the slash. Deal with the final special
	 * case i==0 (ie the whole path was "/nonexistentfile").
	 */
	fullname[i] = '\0';	       /* separate the string */
	if (i == 0) {
	    canonname = fxp_realpath("/");
	} else {
	    canonname = fxp_realpath(fullname);
	}

	if (!canonname)
	    return fullname;	       /* even that failed; give up */

	/*
	 * We have a canonical name for all but the last path
	 * component. Concatenate the last component and return.
	 */
	returnname = dupcat(canonname,
			    canonname[strlen(canonname) - 1] ==
			    '/' ? "" : "/", fullname + i + 1, NULL);
	sfree(fullname);
	sfree(canonname);
	return returnname;
    }
}

/* ----------------------------------------------------------------------
 * Actual sftp commands.
 */
struct sftp_command {
    char **words;
    int nwords, wordssize;
    int (*obey) (struct sftp_command *);	/* returns <0 to quit */
};

int sftp_cmd_null(struct sftp_command *cmd)
{
    return 0;
}

int sftp_cmd_unknown(struct sftp_command *cmd)
{
    printf("psftp: unknown command \"%s\"\n", cmd->words[0]);
    return 0;
}

int sftp_cmd_quit(struct sftp_command *cmd)
{
    return -1;
}

/*
 * List a directory. If no arguments are given, list pwd; otherwise
 * list the directory given in words[1].
 */
static int sftp_ls_compare(const void *av, const void *bv)
{
    const struct fxp_name *a = (const struct fxp_name *) av;
    const struct fxp_name *b = (const struct fxp_name *) bv;
    return strcmp(a->filename, b->filename);
}
int sftp_cmd_ls(struct sftp_command *cmd)
{
    struct fxp_handle *dirh;
    struct fxp_names *names;
    struct fxp_name *ournames;
    int nnames, namesize;
    char *dir, *cdir;
    int i;

    if (cmd->nwords < 2)
	dir = ".";
    else
	dir = cmd->words[1];

    cdir = canonify(dir);
    if (!cdir) {
	printf("%s: %s\n", dir, fxp_error());
	return 0;
    }

    printf("Listing directory %s\n", cdir);

    dirh = fxp_opendir(cdir);
    if (dirh == NULL) {
	printf("Unable to open %s: %s\n", dir, fxp_error());
    } else {
	nnames = namesize = 0;
	ournames = NULL;

	while (1) {

	    names = fxp_readdir(dirh);
	    if (names == NULL) {
		if (fxp_error_type() == SSH_FX_EOF)
		    break;
		printf("Reading directory %s: %s\n", dir, fxp_error());
		break;
	    }
	    if (names->nnames == 0) {
		fxp_free_names(names);
		break;
	    }

	    if (nnames + names->nnames >= namesize) {
		namesize += names->nnames + 128;
		ournames =
		    srealloc(ournames, namesize * sizeof(*ournames));
	    }

	    for (i = 0; i < names->nnames; i++)
		ournames[nnames++] = names->names[i];

	    names->nnames = 0;	       /* prevent free_names */
	    fxp_free_names(names);
	}
	fxp_close(dirh);

	/*
	 * Now we have our filenames. Sort them by actual file
	 * name, and then output the longname parts.
	 */
	qsort(ournames, nnames, sizeof(*ournames), sftp_ls_compare);

	/*
	 * And print them.
	 */
	for (i = 0; i < nnames; i++)
	    printf("%s\n", ournames[i].longname);
    }

    sfree(cdir);

    return 0;
}

/*
 * Change directories. We do this by canonifying the new name, then
 * trying to OPENDIR it. Only if that succeeds do we set the new pwd.
 */
int sftp_cmd_cd(struct sftp_command *cmd)
{
    struct fxp_handle *dirh;
    char *dir;

    if (cmd->nwords < 2)
	dir = dupstr(homedir);
    else
	dir = canonify(cmd->words[1]);

    if (!dir) {
	printf("%s: %s\n", dir, fxp_error());
	return 0;
    }

    dirh = fxp_opendir(dir);
    if (!dirh) {
	printf("Directory %s: %s\n", dir, fxp_error());
	sfree(dir);
	return 0;
    }

    fxp_close(dirh);

    sfree(pwd);
    pwd = dir;
    printf("Remote directory is now %s\n", pwd);

    return 0;
}

/*
 * Get a file and save it at the local end.
 */
int sftp_cmd_get(struct sftp_command *cmd)
{
    struct fxp_handle *fh;
    char *fname, *outfname;
    uint64 offset;
    FILE *fp;

    if (cmd->nwords < 2) {
	printf("get: expects a filename\n");
	return 0;
    }

    fname = canonify(cmd->words[1]);
    if (!fname) {
	printf("%s: %s\n", cmd->words[1], fxp_error());
	return 0;
    }
    outfname = (cmd->nwords == 2 ? cmd->words[1] : cmd->words[2]);

    fh = fxp_open(fname, SSH_FXF_READ);
    if (!fh) {
	printf("%s: %s\n", fname, fxp_error());
	sfree(fname);
	return 0;
    }
    fp = fopen(outfname, "wb");
    if (!fp) {
	printf("local: unable to open %s\n", outfname);
	fxp_close(fh);
	sfree(fname);
	return 0;
    }

    printf("remote:%s => local:%s\n", fname, outfname);

    offset = uint64_make(0, 0);

    /*
     * FIXME: we can use FXP_FSTAT here to get the file size, and
     * thus put up a progress bar.
     */
    while (1) {
	char buffer[4096];
	int len;
	int wpos, wlen;

	len = fxp_read(fh, buffer, offset, sizeof(buffer));
	if ((len == -1 && fxp_error_type() == SSH_FX_EOF) || len == 0)
	    break;
	if (len == -1) {
	    printf("error while reading: %s\n", fxp_error());
	    break;
	}

	wpos = 0;
	while (wpos < len) {
	    wlen = fwrite(buffer, 1, len - wpos, fp);
	    if (wlen <= 0) {
		printf("error while writing local file\n");
		break;
	    }
	    wpos += wlen;
	}
	if (wpos < len)		       /* we had an error */
	    break;
	offset = uint64_add32(offset, len);
    }

    fclose(fp);
    fxp_close(fh);
    sfree(fname);

    return 0;
}

/*
 * Send a file and store it at the remote end.
 */
int sftp_cmd_put(struct sftp_command *cmd)
{
    struct fxp_handle *fh;
    char *fname, *origoutfname, *outfname;
    uint64 offset;
    FILE *fp;

    if (cmd->nwords < 2) {
	printf("put: expects a filename\n");
	return 0;
    }

    fname = cmd->words[1];
    origoutfname = (cmd->nwords == 2 ? cmd->words[1] : cmd->words[2]);
    outfname = canonify(origoutfname);
    if (!outfname) {
	printf("%s: %s\n", origoutfname, fxp_error());
	return 0;
    }

    fp = fopen(fname, "rb");
    if (!fp) {
	printf("local: unable to open %s\n", fname);
	sfree(outfname);
	return 0;
    }
    fh = fxp_open(outfname, SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC);
    if (!fh) {
	printf("%s: %s\n", outfname, fxp_error());
	sfree(outfname);
	return 0;
    }

    printf("local:%s => remote:%s\n", fname, outfname);

    offset = uint64_make(0, 0);

    /*
     * FIXME: we can use FXP_FSTAT here to get the file size, and
     * thus put up a progress bar.
     */
    while (1) {
	char buffer[4096];
	int len;

	len = fread(buffer, 1, sizeof(buffer), fp);
	if (len == -1) {
	    printf("error while reading local file\n");
	    break;
	} else if (len == 0) {
	    break;
	}
	if (!fxp_write(fh, buffer, offset, len)) {
	    printf("error while writing: %s\n", fxp_error());
	    break;
	}
	offset = uint64_add32(offset, len);
    }

    fxp_close(fh);
    fclose(fp);
    sfree(outfname);

    return 0;
}

int sftp_cmd_mkdir(struct sftp_command *cmd)
{
    char *dir;
    int result;


    if (cmd->nwords < 2) {
	printf("mkdir: expects a directory\n");
	return 0;
    }

    dir = canonify(cmd->words[1]);
    if (!dir) {
	printf("%s: %s\n", dir, fxp_error());
	return 0;
    }

    result = fxp_mkdir(dir);
    if (!result) {
	printf("mkdir %s: %s\n", dir, fxp_error());
	sfree(dir);
	return 0;
    }

	sfree(dir);
	return 0;

}

int sftp_cmd_rmdir(struct sftp_command *cmd)
{
    char *dir;
    int result;


    if (cmd->nwords < 2) {
	printf("rmdir: expects a directory\n");
	return 0;
    }

    dir = canonify(cmd->words[1]);
    if (!dir) {
	printf("%s: %s\n", dir, fxp_error());
	return 0;
    }

    result = fxp_rmdir(dir);
    if (!result) {
	printf("rmdir %s: %s\n", dir, fxp_error());
	sfree(dir);
	return 0;
    }

	sfree(dir);
	return 0;

}

int sftp_cmd_rm(struct sftp_command *cmd)
{
    char *fname;
    int result;


    if (cmd->nwords < 2) {
	printf("rm: expects a filename\n");
	return 0;
    }

    fname = canonify(cmd->words[1]);
    if (!fname) {
	printf("%s: %s\n", fname, fxp_error());
	return 0;
    }

    result = fxp_rm(fname);
    if (!result) {
	printf("rm %s: %s\n", fname, fxp_error());
	sfree(fname);
	return 0;
    }

	sfree(fname);
	return 0;

}


static struct sftp_cmd_lookup {
    char *name;
    int (*obey) (struct sftp_command *);
} sftp_lookup[] = {
    /*
     * List of sftp commands. This is binary-searched so it MUST be
     * in ASCII order.
     */
    {
    "bye", sftp_cmd_quit}, {
    "cd", sftp_cmd_cd}, {
    "dir", sftp_cmd_ls}, {
    "exit", sftp_cmd_quit}, {
    "get", sftp_cmd_get}, {
    "ls", sftp_cmd_ls}, {
    "mkdir", sftp_cmd_mkdir}, {
    "put", sftp_cmd_put}, {
	"quit", sftp_cmd_quit}, {
	"rm", sftp_cmd_rm}, {
	"rmdir", sftp_cmd_rmdir},};

/* ----------------------------------------------------------------------
 * Command line reading and parsing.
 */
struct sftp_command *sftp_getcmd(FILE *fp, int mode, int modeflags)
{
    char *line;
    int linelen, linesize;
    struct sftp_command *cmd;
    char *p, *q, *r;
    int quoting;

	if ((mode == 0) || (modeflags & 1)) {
	    printf("psftp> ");
	}
    fflush(stdout);

    cmd = smalloc(sizeof(struct sftp_command));
    cmd->words = NULL;
    cmd->nwords = 0;
    cmd->wordssize = 0;

    line = NULL;
    linesize = linelen = 0;
    while (1) {
	int len;
	char *ret;

	linesize += 512;
	line = srealloc(line, linesize);
	ret = fgets(line + linelen, linesize - linelen, fp);
	if (modeflags & 1) {
		printf("%s", ret);
	}

	if (!ret || (linelen == 0 && line[0] == '\0')) {
	    cmd->obey = sftp_cmd_quit;
	    printf("quit\n");
	    return cmd;		       /* eof */
	}
	len = linelen + strlen(line + linelen);
	linelen += len;
	if (line[linelen - 1] == '\n') {
	    linelen--;
	    line[linelen] = '\0';
	    break;
	}
    }

    /*
     * Parse the command line into words. The syntax is:
     *  - double quotes are removed, but cause spaces within to be
     *    treated as non-separating.
     *  - a double-doublequote pair is a literal double quote, inside
     *    _or_ outside quotes. Like this:
     * 
     *      firstword "second word" "this has ""quotes"" in" sodoes""this""
     * 
     * becomes
     * 
     *      >firstword<
     *      >second word<
     *      >this has "quotes" in<
     *      >sodoes"this"<
     */
    p = line;
    while (*p) {
	/* skip whitespace */
	while (*p && (*p == ' ' || *p == '\t'))
	    p++;
	/* mark start of word */
	q = r = p;		       /* q sits at start, r writes word */
	quoting = 0;
	while (*p) {
	    if (!quoting && (*p == ' ' || *p == '\t'))
		break;		       /* reached end of word */
	    else if (*p == '"' && p[1] == '"')
		p += 2, *r++ = '"';    /* a literal quote */
	    else if (*p == '"')
		p++, quoting = !quoting;
	    else
		*r++ = *p++;
	}
	if (*p)
	    p++;		       /* skip over the whitespace */
	*r = '\0';
	if (cmd->nwords >= cmd->wordssize) {
	    cmd->wordssize = cmd->nwords + 16;
	    cmd->words =
		srealloc(cmd->words, cmd->wordssize * sizeof(char *));
	}
	cmd->words[cmd->nwords++] = q;
    }

    /*
     * Now parse the first word and assign a function.
     */

    if (cmd->nwords == 0)
	cmd->obey = sftp_cmd_null;
    else {
	int i, j, k, cmp;

	cmd->obey = sftp_cmd_unknown;

	i = -1;
	j = sizeof(sftp_lookup) / sizeof(*sftp_lookup);
	while (j - i > 1) {
	    k = (j + i) / 2;
	    cmp = strcmp(cmd->words[0], sftp_lookup[k].name);
	    if (cmp < 0)
		j = k;
	    else if (cmp > 0)
		i = k;
	    else {
		cmd->obey = sftp_lookup[k].obey;
		break;
	    }
	}
    }

    return cmd;
}

void do_sftp(int mode, int modeflags, char *batchfile)
{
    FILE *fp;

    /*
     * Do protocol initialisation. 
     */
    if (!fxp_init()) {
	fprintf(stderr,
		"Fatal: unable to initialise SFTP: %s\n", fxp_error());
	return;
    }

    /*
     * Find out where our home directory is.
     */
    homedir = fxp_realpath(".");
    if (!homedir) {
	fprintf(stderr,
		"Warning: failed to resolve home directory: %s\n",
		fxp_error());
	homedir = dupstr(".");
    } else {
	printf("Remote working directory is %s\n", homedir);
    }
    pwd = dupstr(homedir);

    /*
     * Batch mode?
     */
    if (mode == 0) {

        /* ------------------------------------------------------------------
         * Now we're ready to do Real Stuff.
         */
        while (1) {
    	struct sftp_command *cmd;
    	cmd = sftp_getcmd(stdin, 0, 0);
    	if (!cmd)
    	    break;
		if (cmd->obey(cmd) < 0)
		    break;
	    }
    } else {
        fp = fopen(batchfile, "r");
        if (!fp) {
        printf("Fatal: unable to open %s\n", batchfile);
        return;
        }
        while (1) {
    	struct sftp_command *cmd;
    	cmd = sftp_getcmd(fp, mode, modeflags);
    	if (!cmd)
    	    break;
		if (cmd->obey(cmd) < 0)
		    break;
		if (fxp_error() != NULL) {
			if (!(modeflags & 2))
				break;
		}
        }
	    fclose(fp);

    }
}

/* ----------------------------------------------------------------------
 * Dirty bits: integration with PuTTY.
 */

static int verbose = 0;

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
    fprintf(stderr, str);

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
    fprintf(stderr, str);

    exit(1);
}

void logevent(char *string)
{
}

void ldisc_send(char *buf, int len)
{
    /*
     * This is only here because of the calls to ldisc_send(NULL,
     * 0) in ssh.c. Nothing in PSFTP actually needs to use the
     * ldisc as an ldisc. So if we get called with any real data, I
     * want to know about it.
     */
    assert(len == 0);
}

/*
 * Be told what socket we're supposed to be using.
 */
static SOCKET sftp_ssh_socket;
char *do_select(SOCKET skt, int startup)
{
    if (startup)
	sftp_ssh_socket = skt;
    else
	sftp_ssh_socket = INVALID_SOCKET;
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
int sftp_recvdata(char *buf, int len)
{
    outptr = (unsigned char *) buf;
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
	    return 1;
    }

    while (outlen > 0) {
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(sftp_ssh_socket, &readfds);
	if (select(1, &readfds, NULL, NULL, NULL) < 0)
	    return 0;		       /* doom */
	select_result((WPARAM) sftp_ssh_socket, (LPARAM) FD_READ);
    }

    return 1;
}
int sftp_senddata(char *buf, int len)
{
    back->send((unsigned char *) buf, len);
    return 1;
}

/*
 * Loop through the ssh connection and authentication process.
 */
static void ssh_sftp_init(void)
{
    if (sftp_ssh_socket == INVALID_SOCKET)
	return;
    while (!back->sendok()) {
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sftp_ssh_socket, &readfds);
	if (select(1, &readfds, NULL, NULL, NULL) < 0)
	    return;		       /* doom */
	select_result((WPARAM) sftp_ssh_socket, (LPARAM) FD_READ);
    }
}

static char *password = NULL;
static int get_line(const char *prompt, char *str, int maxlen, int is_pw)
{
    HANDLE hin, hout;
    DWORD savemode, newmode, i;

    if (password) {
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
	fprintf(stderr, "Cannot get standard input/output handles\n");
	exit(1);
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

/*
 *  Initialize the Win$ock driver.
 */
static void init_winsock(void)
{
    WORD winsock_ver;
    WSADATA wsadata;

    winsock_ver = MAKEWORD(1, 1);
    if (WSAStartup(winsock_ver, &wsadata)) {
	fprintf(stderr, "Unable to initialise WinSock");
	exit(1);
    }
    if (LOBYTE(wsadata.wVersion) != 1 || HIBYTE(wsadata.wVersion) != 1) {
	fprintf(stderr, "WinSock version is incompatible with 1.1");
	exit(1);
    }
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Secure File Transfer (SFTP) client\n");
    printf("%s\n", ver);
    printf("Usage: psftp [options] user@host\n");
    printf("Options:\n");
    printf("  -b file   use specified batchfile\n");
    printf("  -bc       output batchfile commands\n");
    printf("  -be       don't stop batchfile processing if errors\n");
    printf("  -v        show verbose messages\n");
    printf("  -P port   connect to specified port\n");
    printf("  -pw passw login with specified password\n");
    exit(1);
}

/*
 * Main program. Parse arguments etc.
 */
int main(int argc, char *argv[])
{
    int i;
    int portnumber = 0;
    char *user, *host, *userhost, *realhost;
    char *err;
    int mode = 0;
    int modeflags = 0;
    char *batchfile = NULL;

    flags = FLAG_STDERR;
    ssh_get_line = &get_line;
    init_winsock();
    sk_init();

    userhost = user = NULL;

    for (i = 1; i < argc; i++) {
	if (argv[i][0] != '-') {
	    if (userhost)
		usage();
	    else
		userhost = dupstr(argv[i]);
	} else if (strcmp(argv[i], "-v") == 0) {
	    verbose = 1, flags |= FLAG_VERBOSE;
	} else if (strcmp(argv[i], "-h") == 0 ||
		   strcmp(argv[i], "-?") == 0) {
	    usage();
	} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
	    user = argv[++i];
	} else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
	    portnumber = atoi(argv[++i]);
	} else if (strcmp(argv[i], "-pw") == 0 && i + 1 < argc) {
	    password = argv[++i];
    } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
	    mode = 1;
        batchfile = argv[++i];
    } else if (strcmp(argv[i], "-bc") == 0 && i + 1 < argc) {
	    modeflags = modeflags | 1;
    } else if (strcmp(argv[i], "-be") == 0 && i + 1 < argc) {
	    modeflags = modeflags | 2;
	} else if (strcmp(argv[i], "--") == 0) {
	    i++;
	    break;
	} else {
	    usage();
	}
    }
    argc -= i;
    argv += i;
    back = NULL;

    if (argc > 0 || !userhost)
	usage();

    /* Separate host and username */
    host = userhost;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = userhost;
    } else {
	*host++ = '\0';
	if (user) {
	    printf("psftp: multiple usernames specified; using \"%s\"\n",
		   user);
	} else
	    user = userhost;
    }

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
    }
    if (!cfg.username[0]) {
	printf("login as: ");
	if (!fgets(cfg.username, sizeof(cfg.username), stdin)) {
	    fprintf(stderr, "psftp: aborting\n");
	    exit(1);
	} else {
	    int len = strlen(cfg.username);
	    if (cfg.username[len - 1] == '\n')
		cfg.username[len - 1] = '\0';
	}
    }

    if (cfg.protocol != PROT_SSH)
	cfg.port = 22;

    if (portnumber)
	cfg.port = portnumber;

    /* SFTP uses SSH2 by default always */
    cfg.sshprot = 2;

    /* Set up subsystem name. FIXME: fudge for SSH1. */
    strcpy(cfg.remote_cmd, "sftp");
    cfg.ssh_subsys = TRUE;
    cfg.nopty = TRUE;

    back = &ssh_backend;

    err = back->init(cfg.host, cfg.port, &realhost);
    if (err != NULL) {
	fprintf(stderr, "ssh_init: %s", err);
	return 1;
    }
    ssh_sftp_init();
    if (verbose && realhost != NULL)
	printf("Connected to %s\n", realhost);

    do_sftp(mode, modeflags, batchfile);

    if (back != NULL && back->socket() != NULL) {
	char ch;
	back->special(TS_EOF);
	sftp_recvdata(&ch, 1);
    }
    WSACleanup();
    random_save_seed();

    return 0;
}
