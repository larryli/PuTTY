#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

typedef void *Context;                 /* FIXME: probably needs changing */

extern Backend pty_backend;

/*
 * Under GTK, we send MA_CLICK _and_ MA_2CLK, or MA_CLICK _and_
 * MA_3CLK, when a button is pressed for the second or third time.
 */
#define MULTICLICK_ONLY_EVENT 0

/*
 * Under X, selection data must not be NUL-terminated.
 */
#define SELECTION_NUL_TERMINATED 0

/*
 * Under X, copying to the clipboard terminates lines with just LF.
 */
#define SEL_NL { 10 }

/* Simple wraparound timer function */
unsigned long getticks(void);	       /* based on gettimeofday(2) */
#define GETTICKCOUNT getticks
#define TICKSPERSEC 1000000	       /* gettimeofday returns microseconds */
#define CURSORBLINK  450000	       /* no standard way to set this */

#define WCHAR wchar_t
#define BYTE unsigned char

int is_dbcs_leadbyte(int codepage, char byte);
int mb_to_wc(int codepage, int flags, char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen);
int wc_to_mb(int codepage, int flags, wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, char *defchr, int *defused);
void init_ucs(void);

/* Things pty.c needs from pterm.c */
char *get_x_display(void);
int font_dimension(int which);	       /* 0 for width, 1 for height */

#define DEFAULT_CODEPAGE 0	       /* FIXME: no idea how to do this */

#endif
