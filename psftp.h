/*
 * psftp.h: interface between psftp.c and each platform-specific
 * SFTP module.
 */

#ifndef PUTTY_PSFTP_H
#define PUTTY_PSFTP_H

/*
 * psftp_getcwd returns the local current directory. The returned
 * string must be freed by the caller.
 */
char *psftp_getcwd(void);

/*
 * psftp_lcd changes the local current directory. The return value
 * is NULL on success, or else an error message which must be freed
 * by the caller.
 */
char *psftp_lcd(char *newdir);

/*
 * One iteration of the PSFTP event loop: wait for network data and
 * process it, once.
 */
int ssh_sftp_loop_iteration(void);

/*
 * The main program in psftp.c. Called from main() in the platform-
 * specific code, after doing any platform-specific initialisation.
 */
int psftp_main(int argc, char *argv[]);

#endif /* PUTTY_PSFTP_H */
