/* $Id: testback.c,v 1.1.2.1 1999/03/07 23:23:38 ben Exp $ */
/*
 * Copyright (c) 1999 Simon Tatham
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* PuTTY test backends */

#include <stdlib.h>

#include "putty.h"

static char *null_init(char *, int, char **);
static int null_msg(void);
static void null_send(char *, int);
static void loop_send(*char *, int);
static void null_size(void);
static void null_special(Telnet_Special);

Backend null_backend = {
    null_init, null_msg, null_send, null_size, null_special
};

Backend loop_backend = {
    null_init, null_msg, loop_send, null_size, null_special
};

static char *null_init(char *host, int port, char **realhost) {

    return NULL;
}

static int null_msg(void) {

    return 1;
}

static void null_send(char *buf, int len) {

}

static void lo_send (char *buf, int len) {
    while (len--) {
	int new_head = (inbuf_head + 1) & INBUF_MASK;
	int c = (unsigned char) *buf;
	if (new_head != inbuf_reap) {
	    inbuf[inbuf_head] = *buf++;
	    inbuf_head = new_head;
	}
    }
}



static void null_size(void) {

}

static void null_special(Telnet_Special code) {

}
