/*
 * General mechanism for wrapping up reading/writing of Windows
 * HANDLEs into a PuTTY Socket abstraction.
 */

#include <stdio.h>
#include <assert.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "tree234.h"
#include "putty.h"
#include "network.h"

typedef struct Socket_handle_tag *Handle_Socket;

struct Socket_handle_tag {
    const struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */

    HANDLE send_H, recv_H;
    struct handle *send_h, *recv_h;

    char *error;

    Plug plug;

    void *privptr;
};

static int handle_gotdata(struct handle *h, void *data, int len)
{
    Handle_Socket ps = (Handle_Socket) handle_get_privdata(h);

    if (len < 0) {
	return plug_closing(ps->plug, "Read error from handle",
			    0, 0);
    } else if (len == 0) {
	return plug_closing(ps->plug, NULL, 0, 0);
    } else {
	return plug_receive(ps->plug, 0, data, len);
    }
}

static void handle_sentdata(struct handle *h, int new_backlog)
{
    Handle_Socket ps = (Handle_Socket) handle_get_privdata(h);
    
    plug_sent(ps->plug, new_backlog);
}

static Plug sk_handle_plug(Socket s, Plug p)
{
    Handle_Socket ps = (Handle_Socket) s;
    Plug ret = ps->plug;
    if (p)
	ps->plug = p;
    return ret;
}

static void sk_handle_close(Socket s)
{
    Handle_Socket ps = (Handle_Socket) s;

    handle_free(ps->send_h);
    handle_free(ps->recv_h);
    CloseHandle(ps->send_H);
    if (ps->recv_H != ps->send_H)
        CloseHandle(ps->recv_H);

    sfree(ps);
}

static int sk_handle_write(Socket s, const char *data, int len)
{
    Handle_Socket ps = (Handle_Socket) s;

    return handle_write(ps->send_h, data, len);
}

static int sk_handle_write_oob(Socket s, const char *data, int len)
{
    /*
     * oob data is treated as inband; nasty, but nothing really
     * better we can do
     */
    return sk_handle_write(s, data, len);
}

static void sk_handle_write_eof(Socket s)
{
    Handle_Socket ps = (Handle_Socket) s;

    handle_write_eof(ps->send_h);
}

static void sk_handle_flush(Socket s)
{
    /* Handle_Socket ps = (Handle_Socket) s; */
    /* do nothing */
}

static void sk_handle_set_private_ptr(Socket s, void *ptr)
{
    Handle_Socket ps = (Handle_Socket) s;
    ps->privptr = ptr;
}

static void *sk_handle_get_private_ptr(Socket s)
{
    Handle_Socket ps = (Handle_Socket) s;
    return ps->privptr;
}

static void sk_handle_set_frozen(Socket s, int is_frozen)
{
    Handle_Socket ps = (Handle_Socket) s;

    /*
     * FIXME
     */
}

static const char *sk_handle_socket_error(Socket s)
{
    Handle_Socket ps = (Handle_Socket) s;
    return ps->error;
}

Socket make_handle_socket(HANDLE send_H, HANDLE recv_H, Plug plug,
                          int overlapped)
{
    static const struct socket_function_table socket_fn_table = {
	sk_handle_plug,
	sk_handle_close,
	sk_handle_write,
	sk_handle_write_oob,
	sk_handle_write_eof,
	sk_handle_flush,
	sk_handle_set_private_ptr,
	sk_handle_get_private_ptr,
	sk_handle_set_frozen,
	sk_handle_socket_error
    };

    Handle_Socket ret;
    int flags = (overlapped ? HANDLE_FLAG_OVERLAPPED : 0);

    ret = snew(struct Socket_handle_tag);
    ret->fn = &socket_fn_table;
    ret->plug = plug;
    ret->error = NULL;

    ret->recv_H = recv_H;
    ret->recv_h = handle_input_new(ret->recv_H, handle_gotdata, ret, flags);
    ret->send_H = send_H;
    ret->send_h = handle_output_new(ret->send_H, handle_sentdata, ret, flags);

    return (Socket) ret;
}
