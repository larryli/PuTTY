#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "putty.h"

/* My own versions of malloc, realloc and free. Because I want malloc and
 * realloc to bomb out and exit the program if they run out of memory,
 * realloc to reliably call malloc if passed a NULL pointer, and free
 * to reliably do nothing if passed a NULL pointer. Of course we can also
 * put trace printouts in, if we need to. */

#ifdef MALLOC_LOG
static FILE *fp = NULL;

void mlog(char *file, int line) {
    if (!fp) {
	fp = fopen("putty_mem.log", "w");
	setvbuf(fp, NULL, _IONBF, BUFSIZ);
    }
    if (fp)
	fprintf (fp, "%s:%d: ", file, line);
}
#endif

void *safemalloc(size_t size) {
    void *p = malloc (size);
    if (!p) {
	MessageBox(NULL, "Out of memory!", "PuTTY Fatal Error",
		   MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
	exit(1);
    }
#ifdef MALLOC_LOG
    if (fp)
	fprintf(fp, "malloc(%d) returns %p\n", size, p);
#endif
    return p;
}

void *saferealloc(void *ptr, size_t size) {
    void *p;
    if (!ptr)
	p = malloc (size);
    else
	p = realloc (ptr, size);
    if (!p) {
	MessageBox(NULL, "Out of memory!", "PuTTY Fatal Error",
		   MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
	exit(1);
    }
#ifdef MALLOC_LOG
    if (fp)
	fprintf(fp, "realloc(%p,%d) returns %p\n", ptr, size, p);
#endif
    return p;
}

void safefree(void *ptr) {
    if (ptr) {
#ifdef MALLOC_LOG
	if (fp)
	    fprintf(fp, "free(%p)\n", ptr);
#endif
	free (ptr);
    }
#ifdef MALLOC_LOG
    else if (fp)
	fprintf(fp, "freeing null pointer - no action taken\n");
#endif
}

#ifdef DEBUG
static FILE *debug_fp = NULL;
static int debug_got_console = 0;

void dprintf(char *fmt, ...) {
    char buf[2048];
    DWORD dw;
    va_list ap;

    if (!debug_got_console) {
	AllocConsole();
	debug_got_console = 1;
    }
    if (!debug_fp) {
	debug_fp = fopen("debug.log", "w");
    }

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, strlen(buf), &dw, NULL);
    fputs(buf, debug_fp);
    fflush(debug_fp);
    va_end(ap);
}
#endif
