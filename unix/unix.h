#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

typedef void *Context;                 /* FIXME: probably needs changing */

/* Simple wraparound timer function */
unsigned long getticks(void);	       /* based on gettimeofday(2) */
#define GETTICKCOUNT getticks
#define TICKSPERSEC 1000000	       /* gettimeofday returns microseconds */
#define CURSORBLINK  400000	       /* FIXME: need right way to do this */

#define WCHAR wchar_t
#define BYTE unsigned char


#define DEFAULT_CODEPAGE 0	       /* FIXME: no idea how to do this */

#endif
