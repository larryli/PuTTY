/*
 * winsftp.c: the Windows-specific parts of PSFTP.
 */

#include <windows.h>

#include "putty.h"
#include "psftp.h"

/*
 * Be told what socket we're supposed to be using.
 */
static SOCKET sftp_ssh_socket;
char *do_select(SOCKET skt, int startup)
{
    if (startup)
	sftp_ssh_socket = skt;
    else
	sftp_ssh_socket = INVALID_SOCKET;
    return NULL;
}
extern int select_result(WPARAM, LPARAM);

/*
 * Initialize the WinSock driver.
 */
static void init_winsock(void)
{
    WORD winsock_ver;
    WSADATA wsadata;

    winsock_ver = MAKEWORD(1, 1);
    if (WSAStartup(winsock_ver, &wsadata)) {
	fprintf(stderr, "Unable to initialise WinSock");
	cleanup_exit(1);
    }
    if (LOBYTE(wsadata.wVersion) != 1 || HIBYTE(wsadata.wVersion) != 1) {
	fprintf(stderr, "WinSock version is incompatible with 1.1");
	cleanup_exit(1);
    }
}

/*
 * Set local current directory. Returns NULL on success, or else an
 * error message which must be freed after printing.
 */
char *psftp_lcd(char *dir)
{
    char *ret = NULL;

    if (!SetCurrentDirectory(dir)) {
	LPVOID message;
	int i;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_SYSTEM |
		      FORMAT_MESSAGE_IGNORE_INSERTS,
		      NULL, GetLastError(),
		      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		      (LPTSTR)&message, 0, NULL);
	i = strcspn((char *)message, "\n");
	ret = dupprintf("%.*s", i, (LPCTSTR)message);
	LocalFree(message);
    }

    return ret;
}

/*
 * Get local current directory. Returns a string which must be
 * freed.
 */
char *psftp_getcwd(void)
{
    char *ret = snewn(256, char);
    int len = GetCurrentDirectory(256, ret);
    if (len > 256)
	ret = sresize(ret, len, char);
    GetCurrentDirectory(len, ret);
    return ret;
}

/*
 * Wait for some network data and process it.
 */
int ssh_sftp_loop_iteration(void)
{
    fd_set readfds;

    if (sftp_ssh_socket == INVALID_SOCKET)
	return -1;		       /* doom */

    FD_ZERO(&readfds);
    FD_SET(sftp_ssh_socket, &readfds);
    if (select(1, &readfds, NULL, NULL, NULL) < 0)
	return -1;		       /* doom */

    select_result((WPARAM) sftp_ssh_socket, (LPARAM) FD_READ);
    return 0;
}

/*
 * Main program. Parse arguments etc.
 */
int main(int argc, char *argv[])
{
    int ret;

    init_winsock();
    ret = psftp_main(argc, argv);
    WSACleanup();

    return ret;
}
