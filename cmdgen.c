/*
 * cmdgen.c - command-line form of PuTTYgen
 */

/*
 * TODO:
 * 
 *  - Test thoroughly.
 *     + a neat way to do this might be to have a -DTESTMODE for
 * 	 this file, which #defines console_get_line and
 * 	 get_random_noise to different names in order to be able to
 * 	 link them to test stubs rather than the real ones. That
 * 	 way I can have a test rig which checks whether passphrases
 * 	 are being prompted for.
 */

#define PUTTY_DO_GLOBALS

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
#include <time.h>

#include "putty.h"
#include "ssh.h"

#ifdef TESTMODE
#define get_random_data get_random_data_diagnostic
char *get_random_data(int len)
{
    char *buf = snewn(len, char);
    memset(buf, 'x', len);
    return buf;
}
#endif

struct progress {
    int phase, current;
};

static void progress_update(void *param, int action, int phase, int iprogress)
{
    struct progress *p = (struct progress *)param;
    if (action != PROGFN_PROGRESS)
	return;
    if (phase > p->phase) {
	if (p->phase >= 0)
	    fputc('\n', stderr);
	p->phase = phase;
	if (iprogress >= 0)
	    p->current = iprogress - 1;
	else
	    p->current = iprogress;
    }
    while (p->current < iprogress) {
	fputc('+', stdout);
	p->current++;
    }
    fflush(stdout);
}

static void no_progress(void *param, int action, int phase, int iprogress)
{
}

void modalfatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    cleanup_exit(1);
}

/*
 * Stubs to let everything else link sensibly.
 */
void log_eventlog(void *handle, const char *event)
{
}
char *x_get_default(const char *key)
{
    return NULL;
}
void sk_cleanup(void)
{
}

void showversion(void)
{
    char *verstr = dupstr(ver);
    verstr[0] = tolower(verstr[0]);
    printf("PuTTYgen %s\n", verstr);
    sfree(verstr);
}

void usage(void)
{
    fprintf(stderr,
	    "Usage: puttygen ( keyfile | -t type [ -b bits ] )\n"
	    "                [ -C comment ] [ -P ]\n"
	    "                [ -o output-keyfile ] [ -O type | -l | -L"
	    " | -p ]\n");
}

void help(void)
{
    /*
     * Help message is an extended version of the usage message. So
     * start with that, plus a version heading.
     */
    showversion();
    usage();
    fprintf(stderr,
	    "  -t    specify key type when generating (rsa, dsa, rsa1)\n"
	    "  -b    specify number of bits when generating key\n"
	    "  -C    change or specify key comment\n"
	    "  -P    change key passphrase\n"
	    "  -O    specify output type:\n"
	    "           private             output PuTTY private key format\n"
	    "           private-openssh     export OpenSSH private key\n"
	    "           private-sshcom      export ssh.com private key\n"
	    "           public              standard / ssh.com public key\n"
	    "           public-openssh      OpenSSH public key\n"
	    "           fingerprint         output the key fingerprint\n"
	    "  -o    specify output file\n"
	    "  -l    equivalent to `-O fingerprint'\n"
	    "  -L    equivalent to `-O public-openssh'\n"
	    "  -p    equivalent to `-O public'\n"
	    );
}

static int save_ssh2_pubkey(char *filename, char *comment,
			    void *v_pub_blob, int pub_len)
{
    unsigned char *pub_blob = (unsigned char *)v_pub_blob;
    char *p;
    int i, column;
    FILE *fp;

    if (filename) {
	fp = fopen(filename, "wb");
	if (!fp)
	    return 0;
    } else
	fp = stdout;

    fprintf(fp, "---- BEGIN SSH2 PUBLIC KEY ----\n");

    if (comment) {
	fprintf(fp, "Comment: \"");
	for (p = comment; *p; p++) {
	    if (*p == '\\' || *p == '\"')
		fputc('\\', fp);
	    fputc(*p, fp);
	}
	fprintf(fp, "\"\n");
    }

    i = 0;
    column = 0;
    while (i < pub_len) {
	char buf[5];
	int n = (pub_len - i < 3 ? pub_len - i : 3);
	base64_encode_atom(pub_blob + i, n, buf);
	i += n;
	buf[4] = '\0';
	fputs(buf, fp);
	if (++column >= 16) {
	    fputc('\n', fp);
	    column = 0;
	}
    }
    if (column > 0)
	fputc('\n', fp);
    
    fprintf(fp, "---- END SSH2 PUBLIC KEY ----\n");
    if (filename)
	fclose(fp);
    return 1;
}

static void move(char *from, char *to)
{
    int ret;

    ret = rename(from, to);
    if (ret) {
	/*
	 * This OS may require us to remove the original file first.
	 */
	remove(to);
	ret = rename(from, to);
    }
    if (ret) {
	perror("puttygen: cannot move new file on to old one");
	exit(1);
    }
}

static char *blobfp(char *alg, int bits, char *blob, int bloblen)
{
    char buffer[128];
    unsigned char digest[16];
    struct MD5Context md5c;
    int i;

    MD5Init(&md5c);
    MD5Update(&md5c, blob, bloblen);
    MD5Final(digest, &md5c);

    sprintf(buffer, "%s ", alg);
    if (bits > 0)
	sprintf(buffer + strlen(buffer), "%d ", bits);
    for (i = 0; i < 16; i++)
	sprintf(buffer + strlen(buffer), "%s%02x", i ? ":" : "",
		digest[i]);

    return dupstr(buffer);
}

int main(int argc, char **argv)
{
    char *infile = NULL;
    Filename infilename;
    enum { NOKEYGEN, RSA1, RSA2, DSA } keytype = NOKEYGEN;    
    char *outfile = NULL, *outfiletmp = NULL;
    Filename outfilename;
    enum { PRIVATE, PUBLIC, PUBLICO, FP, OPENSSH, SSHCOM } outtype = PRIVATE;
    int bits = 1024;
    char *comment = NULL, *origcomment = NULL;
    int change_passphrase = FALSE;
    int errs = FALSE, nogo = FALSE;
    int intype = SSH_KEYTYPE_UNOPENABLE;
    int sshver = 0;
    struct ssh2_userkey *ssh2key = NULL;
    struct RSAKey *ssh1key = NULL;
    char *ssh2blob = NULL, *ssh2alg = NULL;
    const struct ssh_signkey *ssh2algf = NULL;
    int ssh2bloblen;
    char *passphrase = NULL;
    int load_encrypted;
    progfn_t progressfn = is_interactive() ? progress_update : no_progress;

    /* ------------------------------------------------------------------
     * Parse the command line to figure out what we've been asked to do.
     */

    /*
     * If run with no arguments at all, print the usage message and
     * return success.
     */
    if (argc <= 1) {
	usage();
	return 0;
    }

    /*
     * Parse command line arguments.
     */
    while (--argc) {
	char *p = *++argv;
	if (*p == '-') {
	    /*
	     * An option.
	     */
	    while (p && *++p) {
		char c = *p;
		switch (c) {
		  case '-':
		    /*
		     * Long option.
		     */
		    {
			char *opt, *val;
			opt = p++;     /* opt will have _one_ leading - */
			while (*p && *p != '=')
			    p++;	       /* find end of option */
			if (*p == '=') {
			    *p++ = '\0';
			    val = p;
			} else
			    val = NULL;
			if (!strcmp(opt, "-help")) {
			    help();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-version")) {
			    showversion();
			    nogo = TRUE;
			}
			/*
			 * A sample option requiring an argument:
			 * 
			 * else if (!strcmp(opt, "-output")) {
			 *     if (!val)
			 *         errs = TRUE, error(err_optnoarg, opt);
			 *     else
			 *         ofile = val;
			 * }
			 */
			else {
			    errs = TRUE;
			    fprintf(stderr,
				    "puttygen: no such option `--%s'\n", opt);
			}
		    }
		    p = NULL;
		    break;
		  case 'h':
		  case 'V':
		  case 'P':
		  case 'l':
		  case 'L':
		  case 'p':
		  case 'q':
		    /*
		     * Option requiring no parameter.
		     */
		    switch (c) {
		      case 'h':
			help();
			nogo = TRUE;
			break;
		      case 'V':
			showversion();
			nogo = TRUE;
			break;
		      case 'P':
			change_passphrase = TRUE;
			break;
		      case 'l':
			outtype = FP;
			break;
		      case 'L':
			outtype = PUBLICO;
			break;
		      case 'p':
			outtype = PUBLIC;
			break;
		      case 'q':
			progressfn = no_progress;
			break;
		    }
		    break;
		  case 't':
		  case 'b':
		  case 'C':
		  case 'O':
		  case 'o':
		    /*
		     * Option requiring parameter.
		     */
		    p++;
		    if (!*p && argc > 1)
			--argc, p = *++argv;
		    else if (!*p) {
			fprintf(stderr, "puttygen: option `-%c' expects a"
				" parameter\n", c);
			errs = TRUE;
		    }
		    /*
		     * Now c is the option and p is the parameter.
		     */
		    switch (c) {
		      case 't':
			if (!strcmp(p, "rsa") || !strcmp(p, "rsa2"))
			    keytype = RSA2, sshver = 2;
			else if (!strcmp(p, "rsa1"))
			    keytype = RSA1, sshver = 1;
			else if (!strcmp(p, "dsa") || !strcmp(p, "dss"))
			    keytype = DSA, sshver = 2;
			else {
			    fprintf(stderr,
				    "puttygen: unknown key type `%s'\n", p);
			    errs = TRUE;
			}
                        break;
		      case 'b':
			bits = atoi(p);
                        break;
		      case 'C':
			comment = p;
                        break;
		      case 'O':
			if (!strcmp(p, "public"))
			    outtype = PUBLIC;
			else if (!strcmp(p, "public-openssh"))
			    outtype = PUBLICO;
			else if (!strcmp(p, "private"))
			    outtype = PRIVATE;
			else if (!strcmp(p, "fingerprint"))
			    outtype = FP;
			else if (!strcmp(p, "private-openssh"))
			    outtype = OPENSSH, sshver = 2;
			else if (!strcmp(p, "private-sshcom"))
			    outtype = SSHCOM, sshver = 2;
			else {
			    fprintf(stderr,
				    "puttygen: unknown output type `%s'\n", p);
			    errs = TRUE;
			}
                        break;
		      case 'o':
			outfile = p;
                        break;
		    }
		    p = NULL;	       /* prevent continued processing */
		    break;
		  default:
		    /*
		     * Unrecognised option.
		     */
		    errs = TRUE;
		    fprintf(stderr, "puttygen: no such option `-%c'\n", c);
		    break;
		}
	    }
	} else {
	    /*
	     * A non-option argument.
	     */
	    if (!infile)
		infile = p;
	    else {
		errs = TRUE;
		fprintf(stderr, "puttygen: cannot handle more than one"
			" input file\n");
	    }
	}
    }

    if (errs)
	return 1;

    if (nogo)
	return 0;

    /*
     * If run with at least one argument _but_ not the required
     * ones, print the usage message and return failure.
     */
    if (!infile && keytype == NOKEYGEN) {
	usage();
	return 1;
    }

    /* ------------------------------------------------------------------
     * Figure out further details of exactly what we're going to do.
     */

    /*
     * Bomb out if we've been asked to both load and generate a
     * key.
     */
    if (keytype != NOKEYGEN && intype) {
	fprintf(stderr, "puttygen: cannot both load and generate a key\n");
	return 1;
    }

    /*
     * Analyse the type of the input file, in case this affects our
     * course of action.
     */
    if (infile) {
	infilename = filename_from_str(infile);

	intype = key_type(&infilename);

	switch (intype) {
	    /*
	     * It would be nice here to be able to load _public_
	     * key files, in any of a number of forms, and (a)
	     * convert them to other public key types, (b) print
	     * out their fingerprints. Or, I suppose, for real
	     * orthogonality, (c) change their comment!
	     * 
	     * In fact this opens some interesting possibilities.
	     * Suppose ssh2_userkey_loadpub() were able to load
	     * public key files as well as extracting the public
	     * key from private ones. And suppose I did the thing
	     * I've been wanting to do, where specifying a
	     * particular private key file for authentication
	     * causes any _other_ key in the agent to be discarded.
	     * Then, if you had an agent forwarded to the machine
	     * you were running Unix PuTTY or Plink on, and you
	     * needed to specify which of the keys in the agent it
	     * should use, you could do that by supplying a
	     * _public_ key file, thus not needing to trust even
	     * your encrypted private key file to the network. Ooh!
	     */

	  case SSH_KEYTYPE_UNOPENABLE:
	  case SSH_KEYTYPE_UNKNOWN:
	    fprintf(stderr, "puttygen: unable to load file `%s': %s\n",
		    infile, key_type_to_str(intype));
	    return 1;

	  case SSH_KEYTYPE_SSH1:
	    if (sshver == 2) {
		fprintf(stderr, "puttygen: conversion from SSH1 to SSH2 keys"
			" not supported\n");
		return 1;
	    }
	    sshver = 1;
	    break;

	  case SSH_KEYTYPE_SSH2:
	  case SSH_KEYTYPE_OPENSSH:
	  case SSH_KEYTYPE_SSHCOM:
	    if (sshver == 1) {
		fprintf(stderr, "puttygen: conversion from SSH2 to SSH1 keys"
			" not supported\n");
		return 1;
	    }
	    sshver = 2;
	    break;
	}
    }

    /*
     * Determine the default output file, if none is provided.
     * 
     * This will usually be equal to stdout, except that if the
     * input and output file formats are the same then the default
     * output is to overwrite the input.
     * 
     * Also in this code, we bomb out if the input and output file
     * formats are the same and no other action is performed.
     */
    if ((intype == SSH_KEYTYPE_SSH1 && outtype == PRIVATE) ||
	(intype == SSH_KEYTYPE_SSH2 && outtype == PRIVATE) ||
	(intype == SSH_KEYTYPE_OPENSSH && outtype == OPENSSH) ||
	(intype == SSH_KEYTYPE_SSHCOM && outtype == SSHCOM)) {
	if (!outfile) {
	    outfile = infile;
	    outfiletmp = dupcat(outfile, ".tmp");
	}

	if (!change_passphrase && !comment) {
	    fprintf(stderr, "puttygen: this command would perform no useful"
		    " action\n");
	    return 1;
	}
    } else {
	if (!outfile) {
	    /*
	     * Bomb out rather than automatically choosing to write
	     * a private key file to stdout.
	     */
	    if (outtype==PRIVATE || outtype==OPENSSH || outtype==SSHCOM) {
		fprintf(stderr, "puttygen: need to specify an output file\n");
		return 1;
	    }
	}
    }

    /*
     * Figure out whether we need to load the encrypted part of the
     * key. This will be the case if either (a) we need to write
     * out a private key format, or (b) the entire input key file
     * is encrypted.
     */
    if (outtype == PRIVATE || outtype == OPENSSH || outtype == SSHCOM ||
	intype == SSH_KEYTYPE_OPENSSH || intype == SSH_KEYTYPE_SSHCOM)
	load_encrypted = TRUE;
    else
	load_encrypted = FALSE;

    /* ------------------------------------------------------------------
     * Now we're ready to actually do some stuff.
     */

    /*
     * Either load or generate a key.
     */
    if (keytype != NOKEYGEN) {
	char *entropy;
	char default_comment[80];
	time_t t;
	struct tm *tm;
	struct progress prog;

	prog.phase = -1;
	prog.current = -1;

	time(&t);
	tm = localtime(&t);
	if (keytype == DSA)
	    strftime(default_comment, 30, "dsa-key-%Y%m%d", tm);
	else
	    strftime(default_comment, 30, "rsa-key-%Y%m%d", tm);

	random_init();
	entropy = get_random_data(bits / 8);
	random_add_heavynoise(entropy, bits / 8);
	memset(entropy, 0, bits/8);
	sfree(entropy);

	if (keytype == DSA) {
	    struct dss_key *dsskey = snew(struct dss_key);
	    dsa_generate(dsskey, bits, progressfn, &prog);
	    ssh2key = snew(struct ssh2_userkey);
	    ssh2key->data = dsskey;
	    ssh2key->alg = &ssh_dss;
	    ssh1key = NULL;
	} else {
	    struct RSAKey *rsakey = snew(struct RSAKey);
	    rsa_generate(rsakey, bits, progressfn, &prog);
	    if (keytype == RSA1) {
		ssh1key = rsakey;
	    } else {
		ssh2key = snew(struct ssh2_userkey);
		ssh2key->data = rsakey;
		ssh2key->alg = &ssh_rsa;
	    }
	}
	progressfn(&prog, PROGFN_PROGRESS, INT_MAX, -1);

	if (ssh2key)
	    ssh2key->comment = dupstr(default_comment);
	if (ssh1key)
	    ssh1key->comment = dupstr(default_comment);

    } else {
	const char *error = NULL;
	int encrypted;

	assert(infile != NULL);

	/*
	 * Find out whether the input key is encrypted.
	 */
	if (intype == SSH_KEYTYPE_SSH1)
	    encrypted = rsakey_encrypted(&infilename, &origcomment);
	else if (intype == SSH_KEYTYPE_SSH2)
	    encrypted = ssh2_userkey_encrypted(&infilename, &origcomment);
	else
	    encrypted = import_encrypted(&infilename, intype, &origcomment);

	/*
	 * If so, ask for a passphrase.
	 */
	if (encrypted && load_encrypted) {
	    passphrase = snewn(512, char);
	    if (!console_get_line("Enter passphrase to load key: ",
				  passphrase, 512, TRUE)) {
		perror("puttygen: unable to read passphrase");
		return 1;
	    }
	} else {
	    passphrase = NULL;
	}

	switch (intype) {
	    int ret;

	  case SSH_KEYTYPE_SSH1:
	    ssh1key = snew(struct RSAKey);
	    if (!load_encrypted) {
		void *vblob;
		char *blob;
		int n, bloblen;

		ret = rsakey_pubblob(&infilename, &vblob, &bloblen, &error);
		blob = (char *)vblob;

		n = 4;		       /* skip modulus bits */
		n += ssh1_read_bignum(blob + n, &ssh1key->exponent);
		n += ssh1_read_bignum(blob + n, &ssh1key->modulus);
		ssh1key->comment = NULL;
	    } else {
		ret = loadrsakey(&infilename, ssh1key, passphrase, &error);
	    }
	    if (ret)
		error = NULL;
	    else if (!error)
		error = "unknown error";
	    break;

	  case SSH_KEYTYPE_SSH2:
	    if (!load_encrypted) {
		ssh2blob = ssh2_userkey_loadpub(&infilename, &ssh2alg,
						&ssh2bloblen, &error);
		ssh2algf = find_pubkey_alg(ssh2alg);
		if (ssh2algf)
		    bits = ssh2algf->pubkey_bits(ssh2blob, ssh2bloblen);
		else
		    bits = -1;
	    } else {
		ssh2key = ssh2_load_userkey(&infilename, passphrase, &error);
	    }
	    if (ssh2key || ssh2blob)
		error = NULL;
	    else if (!error) {
		if (ssh2key == SSH2_WRONG_PASSPHRASE)
		    error = "wrong passphrase";
		else
		    error = "unknown error";
	    }
	    break;

	  case SSH_KEYTYPE_OPENSSH:
	  case SSH_KEYTYPE_SSHCOM:
	    ssh2key = import_ssh2(&infilename, intype, passphrase);
	    if (ssh2key)
		error = NULL;
	    else if (!error) {
		if (ssh2key == SSH2_WRONG_PASSPHRASE)
		    error = "wrong passphrase";
		else
		    error = "unknown error";
	    }
	    break;

	  default:
	    assert(0);
	}

	if (error) {
	    fprintf(stderr, "puttygen: error loading `%s': %s\n",
		    infile, error);
	    return 1;
	}
    }

    /*
     * Change the comment if asked to.
     */
    if (comment) {
	if (sshver == 1) {
	    assert(ssh1key);
	    sfree(ssh1key->comment);
	    ssh1key->comment = dupstr(comment);
	} else {
	    assert(ssh2key);
	    sfree(ssh2key->comment);
	    ssh2key->comment = dupstr(comment);
	}
    }

    /*
     * Prompt for a new passphrase if we have been asked to, or if
     * we have just generated a key.
     */
    if (change_passphrase || keytype != NOKEYGEN) {
	char *passphrase2;

	if (passphrase) {
	    memset(passphrase, 0, strlen(passphrase));
	    sfree(passphrase);
	}

	passphrase = snewn(512, char);
	passphrase2 = snewn(512, char);
	if (!console_get_line("Enter passphrase to save key: ",
			      passphrase, 512, TRUE) ||
	    !console_get_line("Re-enter passphrase to verify: ",
			      passphrase2, 512, TRUE)) {
	    perror("puttygen: unable to read new passphrase");
	    return 1;
	}
	if (strcmp(passphrase, passphrase2)) {
	    fprintf(stderr, "puttygen: passphrases do not match\n");
	    return 1;
	}
	memset(passphrase2, 0, strlen(passphrase2));
	sfree(passphrase2);
	if (!*passphrase) {
	    sfree(passphrase);
	    passphrase = NULL;
	}
    }

    /*
     * Write output.
     * 
     * (In the case where outfile and outfiletmp are both NULL,
     * there is no semantic reason to initialise outfilename at
     * all; but we have to write _something_ to it or some compiler
     * will probably complain that it might be used uninitialised.)
     */
    if (outfiletmp)
	outfilename = filename_from_str(outfiletmp);
    else
	outfilename = filename_from_str(outfile ? outfile : "");

    switch (outtype) {
	int ret;

      case PRIVATE:
	if (sshver == 1) {
	    assert(ssh1key);
	    ret = saversakey(&outfilename, ssh1key, passphrase);
	    if (!ret) {
		fprintf(stderr, "puttygen: unable to save SSH1 private key\n");
		return 1;
	    }
	} else {
	    assert(ssh2key);
	    ret = ssh2_save_userkey(&outfilename, ssh2key, passphrase);
 	    if (!ret) {
		fprintf(stderr, "puttygen: unable to save SSH2 private key\n");
		return 1;
	    }
	}
	if (outfiletmp)
	    move(outfiletmp, outfile);
	break;

      case PUBLIC:
      case PUBLICO:
	if (sshver == 1) {
	    FILE *fp;
	    char *dec1, *dec2;

	    assert(ssh1key);

	    if (outfile)
		fp = f_open(outfilename, "w");
	    else
		fp = stdout;
	    dec1 = bignum_decimal(ssh1key->exponent);
	    dec2 = bignum_decimal(ssh1key->modulus);
	    fprintf(fp, "%d %s %s %s\n", bignum_bitcount(ssh1key->modulus),
		    dec1, dec2, ssh1key->comment);
	    sfree(dec1);
	    sfree(dec2);
	    if (outfile)
		fclose(fp);
	} else if (outtype == PUBLIC) {
	    if (!ssh2blob) {
		assert(ssh2key);
		ssh2blob = ssh2key->alg->public_blob(ssh2key->data,
						     &ssh2bloblen);
	    }
	    save_ssh2_pubkey(outfile, ssh2key ? ssh2key->comment : origcomment,
			     ssh2blob, ssh2bloblen);
	} else if (outtype == PUBLICO) {
	    char *buffer, *p;
	    int i;
	    FILE *fp;

	    if (!ssh2blob) {
		assert(ssh2key);
		ssh2blob = ssh2key->alg->public_blob(ssh2key->data,
						     &ssh2bloblen);
	    }
	    if (!ssh2alg) {
		assert(ssh2key);
		ssh2alg = ssh2key->alg->name;
	    }
	    if (ssh2key)
		comment = ssh2key->comment;
	    else
		comment = origcomment;

	    buffer = snewn(strlen(ssh2alg) +
			   4 * ((ssh2bloblen+2) / 3) +
			   strlen(comment) + 3, char);
	    strcpy(buffer, ssh2alg);
	    p = buffer + strlen(buffer);
	    *p++ = ' ';
	    i = 0;
	    while (i < ssh2bloblen) {
		int n = (ssh2bloblen - i < 3 ? ssh2bloblen - i : 3);
		base64_encode_atom(ssh2blob + i, n, p);
		i += n;
		p += 4;
	    }
	    if (*comment) {
		*p++ = ' ';
		strcpy(p, comment);
	    } else
		*p++ = '\0';

	    if (outfile)
		fp = f_open(outfilename, "w");
	    else
		fp = stdout;
	    fprintf(fp, "%s\n", buffer);
	    if (outfile)
		fclose(fp);

	    sfree(buffer);
	}
	break;

      case FP:
	{
	    FILE *fp;
	    char *fingerprint;

	    if (sshver == 1) {
		assert(ssh1key);
		fingerprint = snewn(128, char);
		rsa_fingerprint(fingerprint, 128, ssh1key);
	    } else {
		if (ssh2key) {
		    fingerprint = ssh2key->alg->fingerprint(ssh2key->data);
		} else {
		    assert(ssh2blob);
		    fingerprint = blobfp(ssh2alg, bits, ssh2blob, ssh2bloblen);
		}
	    }

	    if (outfile)
		fp = f_open(outfilename, "w");
	    else
		fp = stdout;
	    fprintf(fp, "%s\n", fingerprint);
	    if (outfile)
		fclose(fp);

	    sfree(fingerprint);
	}
	break;
	
      case OPENSSH:
      case SSHCOM:
	assert(sshver == 2);
	assert(ssh2key);
	ret = export_ssh2(&outfilename, outtype, ssh2key, passphrase);
	if (!ret) {
	    fprintf(stderr, "puttygen: unable to export key\n");
	    return 1;
	}
	if (outfiletmp)
	    move(outfiletmp, outfile);
	break;
    }

    if (passphrase) {
	memset(passphrase, 0, strlen(passphrase));
	sfree(passphrase);
    }

    if (ssh1key)
	freersakey(ssh1key);
    if (ssh2key) {
	ssh2key->alg->freekey(ssh2key->data);
	sfree(ssh2key);
    }

    return 0;
}
