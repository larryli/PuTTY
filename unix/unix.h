#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

#include "charset.h"

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

GLOBAL void *logctx;

/* Things pty.c needs from pterm.c */
char *get_x_display(void *frontend);
int font_dimension(void *frontend, int which);/* 0 for width, 1 for height */

/* Things uxstore.c needs from pterm.c */
char *x_get_default(const char *key);

/* Things uxstore.c provides to pterm.c */
void provide_xrm_string(char *string);

/* Things uxnet.c provides to the front end */
int select_result(int fd, int event);
int first_socket(int *state, int *rwx);
int next_socket(int *state, int *rwx);

/*
 * In the Unix Unicode layer, DEFAULT_CODEPAGE is a special value
 * which causes mb_to_wc and wc_to_mb to call _libc_ rather than
 * libcharset. That way, we can interface the various charsets
 * supported by libcharset with the one supported by mbstowcs and
 * wcstombs (which will be the character set in which stuff read
 * from the command line or config files is assumed to be encoded).
 */
#define DEFAULT_CODEPAGE 0xFFFF
#define CP_UTF8 CS_UTF8		       /* from libcharset */

#define strnicmp strncasecmp
#define stricmp strcasecmp

/* BSD-semantics version of signal() */
void (*putty_signal(int sig, void (*func)(int)))(int);

/*
 * Exports from unicode.c.
 */
struct unicode_data;
int init_ucs(struct unicode_data *ucsdata,
	     char *line_codepage, int font_charset);

/*
 * Spare function exported directly from uxnet.c.
 */
int sk_getxdmdata(void *sock, unsigned long *ip, int *port);

#endif
