/*
 * macstuff.h -- Mac-specific definitions visible to the rest of PuTTY.
 */

typedef void *Context; /* FIXME */

/*
 * On the Mac, Unicode text copied to the clipboard has U+2028 line separators.
 * Non-Unicode text will have these converted to CR along with the rest of the
 * content.
 */
#define SEL_NL { 0x2028 }

#include <MacTypes.h>
#include <Events.h> /* Timing related goo */

#define GETTICKCOUNT TickCount
#define CURSORBLINK GetCaretTime()
#define TICKSPERSEC 60

#define DEFAULT_CODEPAGE 0	       /* FIXME: no idea how to do this */

#define WCHAR wchar_t
#define BYTE UInt8
#define DWORD UInt32

#define OPTIMISE_SCROLL

/*
 * sk_getxdmdata() does not exist under the Mac (SGT: I have no
 * idea whatsoever how to write it, and furthermore I'm unconvinced
 * it's necessary), so it's a macro which always returns FALSE.
 */
#define sk_getxdmdata(socket, ip, port) (0)

/* To make it compile */

#include <stdarg.h>
extern int vsnprintf(char *, size_t, char const *, va_list);

extern int stricmp(char const *, char const *);
extern int strnicmp(char const *, char const *, size_t);
