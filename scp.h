/*
 *  scp.h
 *  Joris van Rantwijk, Aug 1999, Jun 2000.
 */

#define SCP_FLAG	1
#define SCP_VERBOSE	2
#define IS_SCP ((scp_flags & SCP_FLAG) != 0)

/* Exported from ssh.c */
extern int scp_flags;
extern void (*ssh_get_password)(const char *prompt, char *str, int maxlen);
char * ssh_scp_init(char *host, int port, char *cmd, char **realhost);
int ssh_scp_recv(unsigned char *buf, int len);
void ssh_scp_send(unsigned char *buf, int len);
void ssh_scp_send_eof(void);

