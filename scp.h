/*
 *  scp.h
 *  Joris van Rantwijk, Aug 1999.
 */


/*
 *  Exported from scp.c
 */
extern int verbose;
void ssh_get_password(char *prompt, char *str, int maxlen);


/*
 *  Exported from scpssh.c
 */
char * ssh_init(char *host, int port, char *cmd, char **realhost);
int ssh_recv(unsigned char *buf, int len);
void ssh_send(unsigned char *buf, int len);
void ssh_send_eof(void);

