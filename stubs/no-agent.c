#include "putty.h"

bool agent_exists(void) { return false; }
Socket *agent_connect(Plug *plug) {
    return new_error_socket_fmt(
        plug, "no actual networking in this application");
}
void agent_cancel_query(agent_pending_query *pq) {}
agent_pending_query *agent_query(
    strbuf *query, void **out, int *outlen,
    void (*callback)(void *, void *, int), void *callback_ctx) {return NULL;}
