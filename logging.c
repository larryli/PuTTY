#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include <assert.h>

#include "putty.h"

/* log session to file stuff ... */
struct LogContext {
    FILE *lgfp;
    char currlogfilename[FILENAME_MAX];
    void *frontend;
};

static void xlatlognam(char *d, char *s, char *hostname, struct tm *tm);

/*
 * Log session traffic.
 */
void logtraffic(void *handle, unsigned char c, int logmode)
{
    struct LogContext *ctx = (struct LogContext *)handle;
    if (cfg.logtype > 0) {
	if (cfg.logtype == logmode) {
	    /* deferred open file from pgm start? */
	    if (!ctx->lgfp)
		logfopen(ctx);
	    if (ctx->lgfp)
		fputc(c, ctx->lgfp);
	}
    }
}

/*
 * Log an Event Log entry (used in SSH packet logging mode).
 */
void log_eventlog(void *handle, char *event)
{
    struct LogContext *ctx = (struct LogContext *)handle;
    if (cfg.logtype != LGTYP_PACKETS)
	return;
    if (!ctx->lgfp)
	logfopen(ctx);
    if (ctx->lgfp)
	fprintf(ctx->lgfp, "Event Log: %s\n", event);
}

/*
 * Log an SSH packet.
 */
void log_packet(void *handle, int direction, int type,
		char *texttype, void *data, int len)
{
    struct LogContext *ctx = (struct LogContext *)handle;
    int i, j;
    char dumpdata[80], smalldata[5];

    if (cfg.logtype != LGTYP_PACKETS)
	return;
    if (!ctx->lgfp)
	logfopen(ctx);
    if (ctx->lgfp) {
	fprintf(ctx->lgfp, "%s packet type %d / 0x%02x (%s)\n",
		direction == PKT_INCOMING ? "Incoming" : "Outgoing",
		type, type, texttype);
	for (i = 0; i < len; i += 16) {
	    sprintf(dumpdata, "  %08x%*s\n", i, 1+3*16+2+16, "");
	    for (j = 0; j < 16 && i+j < len; j++) {
		int c = ((unsigned char *)data)[i+j];
		sprintf(smalldata, "%02x", c);
		dumpdata[10+2+3*j] = smalldata[0];
		dumpdata[10+2+3*j+1] = smalldata[1];
		dumpdata[10+1+3*16+2+j] = (isprint(c) ? c : '.');
	    }
	    strcpy(dumpdata + 10+1+3*16+2+j, "\n");
	    fputs(dumpdata, ctx->lgfp);
	}
	fflush(ctx->lgfp);
    }
}

/* open log file append/overwrite mode */
void logfopen(void *handle)
{
    struct LogContext *ctx = (struct LogContext *)handle;
    char buf[256];
    time_t t;
    struct tm tm;
    char writemod[4];

    /* Prevent repeat calls */
    if (ctx->lgfp)
	return;

    if (!cfg.logtype)
	return;
    sprintf(writemod, "wb");	       /* default to rewrite */

    time(&t);
    tm = *localtime(&t);

    /* substitute special codes in file name */
    xlatlognam(ctx->currlogfilename, cfg.logfilename,cfg.host, &tm);

    ctx->lgfp = fopen(ctx->currlogfilename, "r");  /* file already present? */
    if (ctx->lgfp) {
	int i;
	fclose(ctx->lgfp);
	i = askappend(ctx->frontend, ctx->currlogfilename);
	if (i == 1)
	    writemod[0] = 'a';	       /* set append mode */
	else if (i == 0) {	       /* cancelled */
	    ctx->lgfp = NULL;
	    cfg.logtype = 0;	       /* disable logging */
	    return;
	}
    }

    ctx->lgfp = fopen(ctx->currlogfilename, writemod);
    if (ctx->lgfp) {			       /* enter into event log */
	/* --- write header line into log file */
	fputs("=~=~=~=~=~=~=~=~=~=~=~= PuTTY log ", ctx->lgfp);
	strftime(buf, 24, "%Y.%m.%d %H:%M:%S", &tm);
	fputs(buf, ctx->lgfp);
	fputs(" =~=~=~=~=~=~=~=~=~=~=~=\r\n", ctx->lgfp);

	sprintf(buf, "%s session log (%s mode) to file: ",
		(writemod[0] == 'a') ? "Appending" : "Writing new",
		(cfg.logtype == LGTYP_ASCII ? "ASCII" :
		 cfg.logtype == LGTYP_DEBUG ? "raw" :
		 cfg.logtype == LGTYP_PACKETS ? "SSH packets" : "<ukwn>"));
	/* Make sure we do not exceed the output buffer size */
	strncat(buf, ctx->currlogfilename, 128);
	buf[strlen(buf)] = '\0';
	logevent(ctx->frontend, buf);
    }
}

void logfclose(void *handle)
{
    struct LogContext *ctx = (struct LogContext *)handle;
    if (ctx->lgfp) {
	fclose(ctx->lgfp);
	ctx->lgfp = NULL;
    }
}

void *log_init(void *frontend)
{
    struct LogContext *ctx = smalloc(sizeof(struct LogContext));
    ctx->lgfp = NULL;
    ctx->frontend = frontend;
    return ctx;
}

/*
 * translate format codes into time/date strings
 * and insert them into log file name
 *
 * "&Y":YYYY   "&m":MM   "&d":DD   "&T":hhmm   "&h":<hostname>   "&&":&
 */
static void xlatlognam(char *d, char *s, char *hostname, struct tm *tm) {
    char buf[10], *bufp;
    int size;
    int len = FILENAME_MAX-1;

    while (*s) {
	/* Let (bufp, len) be the string to append. */
	bufp = buf;		       /* don't usually override this */
	if (*s == '&') {
	    char c;
	    s++;
	    size = 0;
	    if (*s) switch (c = *s++, tolower(c)) {
	      case 'y':
		size = strftime(buf, sizeof(buf), "%Y", tm);
		break;
	      case 'm':
		size = strftime(buf, sizeof(buf), "%m", tm);
		break;
	      case 'd':
		size = strftime(buf, sizeof(buf), "%d", tm);
		break;
	      case 't':
		size = strftime(buf, sizeof(buf), "%H%M%S", tm);
		break;
	      case 'h':
		bufp = hostname;
		size = strlen(bufp);
		break;
	      default:
		buf[0] = '&';
		size = 1;
		if (c != '&')
		    buf[size++] = c;
	    }
	} else {
	    buf[0] = *s++;
	    size = 1;
	}
	if (size > len)
	    size = len;
	memcpy(d, bufp, size);
	d += size;
	len -= size;
    }
    *d = '\0';
}
