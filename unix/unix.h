#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

typedef void *Context;                 /* FIXME: probably needs changing */

extern Backend pty_backend;

/* Simple wraparound timer function */
unsigned long getticks(void);	       /* based on gettimeofday(2) */
#define GETTICKCOUNT getticks
#define TICKSPERSEC 1000000	       /* gettimeofday returns microseconds */
#define CURSORBLINK  400000	       /* FIXME: need right way to do this */

#define WCHAR wchar_t
#define BYTE unsigned char

int is_dbcs_leadbyte(int codepage, char byte);
int mb_to_wc(int codepage, int flags, char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen);
void init_ucs(void);

#define DEFAULT_CODEPAGE 0	       /* FIXME: no idea how to do this */

#endif
