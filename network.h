/*
 * Networking abstraction in PuTTY.
 *
 * The way this works is: a back end can choose to open any number
 * of sockets - including zero, which might be necessary in some.
 * It can register a function to be called when data comes in on
 * any given one, and it can call the networking abstraction to
 * send data without having to worry about blocking. The stuff
 * behind the abstraction takes care of selects and nonblocking
 * writes and all that sort of painful gubbins.
 * 
 * If urgent data comes in on a socket, the back end will read and
 * discard up to the urgent pointer, then read the urgent byte and
 * send _that_ to the receiver function with `urgent' set.
 */

#ifndef PUTTY_NETWORK_H
#define PUTTY_NETWORK_H

typedef struct Socket_tag *Socket;
typedef struct SockAddr_tag *SockAddr;
typedef int (*sk_receiver_t)(Socket s, int urgent, char *data, int len);

void sk_init(void);		       /* called once at program startup */

SockAddr sk_namelookup(char *host, char **canonicalname);
void sk_addr_free(SockAddr addr);

Socket sk_new(SockAddr addr, int port, sk_receiver_t receiver);
void sk_close(Socket s);
void sk_write(Socket s, char *buf, int len);
void sk_write_oob(Socket s, char *buf, int len);

/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
void sk_set_private_ptr(Socket s, void *ptr);
void *sk_get_private_ptr(Socket s);

/*
 * Special error values are returned from sk_namelookup and sk_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
char *sk_addr_error(SockAddr addr);
char *sk_socket_error(Socket addr);

#endif
