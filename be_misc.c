/*
 * be_misc.c: helper functions shared between main network backends.
 */

#include "putty.h"
#include "network.h"

void backend_socket_log(void *frontend, int type, SockAddr addr, int port,
                        const char *error_msg, int error_code)
{
    char addrbuf[256], *msg;

    switch (type) {
      case 0:
        sk_getaddr(addr, addrbuf, lenof(addrbuf));
        if (sk_addr_needs_port(addr)) {
            msg = dupprintf("Connecting to %s port %d", addrbuf, port);
        } else {
            msg = dupprintf("Connecting to %s", addrbuf);
        }
        break;
      case 1:
        sk_getaddr(addr, addrbuf, lenof(addrbuf));
        msg = dupprintf("Failed to connect to %s: %s", addrbuf, error_msg);
        break;
      default:
        msg = NULL;  /* shouldn't happen, but placate optimiser */
        break;
    }

    if (msg) {
        logevent(frontend, msg);
        sfree(msg);
    }
}

