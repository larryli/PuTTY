/*
 * psftp.c: front end for PSFTP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "sftp.h"
#include "int64.h"

#define smalloc malloc
#define srealloc realloc
#define sfree free

/* ----------------------------------------------------------------------
 * String handling routines.
 */

char *dupstr(char *s) {
    int len = strlen(s);
    char *p = smalloc(len+1);
    strcpy(p, s);
    return p;
}

/* Allocate the concatenation of N strings. Terminate arg list with NULL. */
char *dupcat(char *s1, ...) {
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

    p = smalloc(len+1);
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
char *canonify(char *name) {
    char *fullname, *canonname;
    if (name[0] == '/') {
	fullname = dupstr(name);
    } else {
	fullname = dupcat(pwd, "/", name, NULL);
    }
    canonname = fxp_realpath(name);
    if (canonname) {
	sfree(fullname);
	return canonname;
    } else
	return fullname;
}

/* ----------------------------------------------------------------------
 * Actual sftp commands.
 */
struct sftp_command {
    char **words;
    int nwords, wordssize;
    int (*obey)(struct sftp_command *);/* returns <0 to quit */
};

int sftp_cmd_null(struct sftp_command *cmd) {
    return 0;
}

int sftp_cmd_unknown(struct sftp_command *cmd) {
    printf("psftp: unknown command \"%s\"\n", cmd->words[0]);
    return 0;
}

int sftp_cmd_quit(struct sftp_command *cmd) {
    return -1;
}

/*
 * List a directory. If no arguments are given, list pwd; otherwise
 * list the directory given in words[1].
 */
static int sftp_ls_compare(const void *av, const void *bv) {
    const struct fxp_name *a = (const struct fxp_name *)av;
    const struct fxp_name *b = (const struct fxp_name *)bv;
    return strcmp(a->filename, b->filename);
}
int sftp_cmd_ls(struct sftp_command *cmd) {
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
		ournames = srealloc(ournames, namesize * sizeof(*ournames));
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
int sftp_cmd_cd(struct sftp_command *cmd) {
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
int sftp_cmd_get(struct sftp_command *cmd) {
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

    offset = uint64_make(0,0);

    /*
     * FIXME: we can use FXP_FSTAT here to get the file size, and
     * thus put up a progress bar.
     */
    while (1) {
	char buffer[4096];
	int len;
	int wpos, wlen;

	len = fxp_read(fh, buffer, offset, sizeof(buffer));
	if ((len == -1 && fxp_error_type() == SSH_FX_EOF) ||
	    len == 0)
	    break;
	if (len == -1) {
	    printf("error while reading: %s\n", fxp_error());
	    break;
	}
	
	wpos = 0;
	while (wpos < len) {
	    wlen = fwrite(buffer, 1, len-wpos, fp);
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
int sftp_cmd_put(struct sftp_command *cmd) {
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
        fxp_close(fh);
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

    offset = uint64_make(0,0);

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

static struct sftp_cmd_lookup {
    char *name;
    int (*obey)(struct sftp_command *);
} sftp_lookup[] = {
    /*
     * List of sftp commands. This is binary-searched so it MUST be
     * in ASCII order.
     */
    {"bye", sftp_cmd_quit},
    {"cd", sftp_cmd_cd},
    {"exit", sftp_cmd_quit},
    {"get", sftp_cmd_get},
    {"ls", sftp_cmd_ls},
    {"put", sftp_cmd_put},
    {"quit", sftp_cmd_quit},
};

/* ----------------------------------------------------------------------
 * Command line reading and parsing.
 */
struct sftp_command *sftp_getcmd(void) {
    char *line;
    int linelen, linesize;
    struct sftp_command *cmd;
    char *p, *q, *r;
    int quoting;

    printf("psftp> ");
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
	ret = fgets(line+linelen, linesize-linelen, stdin);

	if (!ret || (linelen == 0 && line[0] == '\0')) {
	    cmd->obey = sftp_cmd_quit;
	    printf("quit\n");
	    return cmd;		       /* eof */
	}
	len = linelen + strlen(line+linelen);
	linelen += len;
	if (line[linelen-1] == '\n') {
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
	while (*p && (*p == ' ' || *p == '\t')) p++;
	/* mark start of word */
	q = r = p;		       /* q sits at start, r writes word */
	quoting = 0;
	while (*p) {
	    if (!quoting && (*p == ' ' || *p == '\t'))
		break;		       /* reached end of word */
	    else if (*p == '"' && p[1] == '"')
		p+=2, *r++ = '"';      /* a literal quote */
	    else if (*p == '"')
		p++, quoting = !quoting;
	    else
		*r++ = *p++;
	}
	if (*p) p++;			       /* skip over the whitespace */
	*r = '\0';
	if (cmd->nwords >= cmd->wordssize) {
	    cmd->wordssize = cmd->nwords + 16;
	    cmd->words = srealloc(cmd->words, cmd->wordssize*sizeof(char *));
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

void do_sftp(void) {
    /*
     * Do protocol initialisation. 
     */
    if (!fxp_init()) {
	fprintf(stderr,
		"Fatal: unable to initialise SFTP: %s\n",
		fxp_error());
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

    /* ------------------------------------------------------------------
     * Now we're ready to do Real Stuff.
     */
    while (1) {
	struct sftp_command *cmd;
	cmd = sftp_getcmd();
	if (!cmd)
	    break;
	if (cmd->obey(cmd) < 0)
	    break;
    }

    /* ------------------------------------------------------------------
     * We've received an exit command. Tidy up and leave.
     */
    io_finish();
}

int main(void) {
    io_init();
    do_sftp();
    return 0;
}
