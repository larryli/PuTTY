#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

#include <stdio.h>		       /* for FILENAME_MAX */
#include "charset.h"

struct Filename {
    char path[FILENAME_MAX];
};
#define f_open(filename, mode) ( fopen((filename).path, (mode)) )

struct FontSpec {
    char name[256];
};

typedef void *Context;                 /* FIXME: probably needs changing */

extern Backend pty_backend;

/*
 * Under GTK, we send MA_CLICK _and_ MA_2CLK, or MA_CLICK _and_
 * MA_3CLK, when a button is pressed for the second or third time.
 */
#define MULTICLICK_ONLY_EVENT 0

/*
 * Under GTK, there is no context help available.
 */
#define HELPCTX(x) P(NULL)
#define FILTER_KEY_FILES NULL          /* FIXME */

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
long get_windowid(void *frontend);

/* Things gtkdlg.c needs from pterm.c */
void *get_window(void *frontend);      /* void * to avoid depending on gtk.h */

/* Things uxstore.c needs from pterm.c */
char *x_get_default(const char *key);

/* Things uxstore.c provides to pterm.c */
void provide_xrm_string(char *string);

/* The interface used by uxsel.c */
void uxsel_init(void);
typedef int (*uxsel_callback_fn)(int fd, int event);
void uxsel_set(int fd, int rwx, uxsel_callback_fn callback);
void uxsel_del(int fd);
int select_result(int fd, int event);
int first_fd(int *state, int *rwx);
int next_fd(int *state, int *rwx);
/* The following are expected to be provided _to_ uxsel.c by the frontend */
int uxsel_input_add(int fd, int rwx);  /* returns an id */
void uxsel_input_remove(int id);

/* uxcfg.c */
struct controlbox;
void unix_setup_config_box(struct controlbox *b, int midsession);

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
