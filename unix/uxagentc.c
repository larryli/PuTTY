/*
 * SSH agent client code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "misc.h"
#include "puttymem.h"

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

int agent_exists(void)
{
    if (getenv("SSH_AUTH_SOCK") != NULL)
	return TRUE;
    return FALSE;
}

void agent_query(void *in, int inlen, void **out, int *outlen)
{
    char *name;
    int sock;
    struct sockaddr_un addr;
    int done;
    int retsize, retlen;
    char sizebuf[4], *retbuf;

    name = getenv("SSH_AUTH_SOCK");
    if (!name)
	goto failure;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	perror("socket(PF_UNIX)");
	exit(1);
    }

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, name, sizeof(addr.sun_path));
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	close(sock);
	goto failure;
    }

    for (done = 0; done < inlen ;) {
	int ret = write(sock, (char *)in + done, inlen - done);
	if (ret <= 0) {
	    close(sock);
	    goto failure;
	}
	done += ret;
    }

    retbuf = sizebuf;
    retsize = 4;
    retlen = 0;

    while (retlen < retsize) {
	int ret = read(sock, retbuf + retlen, retsize - retlen);
	if (ret <= 0) {
	    close(sock);
	    if (retbuf != sizebuf) sfree(retbuf);
	    goto failure;
	}
	retlen += ret;
	if (retsize == 4 && retlen == 4) {
	    retsize = GET_32BIT(retbuf);
	    if (retsize <= 0) {
		close(sock);
		goto failure;
	    }
	    retsize += 4;
	    assert(retbuf == sizebuf);
	    retbuf = snewn(retsize, char);
	    memcpy(retbuf, sizebuf, 4);
	}
    }

    assert(retbuf != sizebuf);
    *out = retbuf;
    *outlen = retlen;
    return;

    failure:
    *out = NULL;
    *outlen = 0;
}
