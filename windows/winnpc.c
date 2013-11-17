/*
 * Windows support module which deals with being a named-pipe client.
 */

#include <stdio.h>
#include <assert.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"
#include "ssh.h"

#if !defined NO_SECURITY

#include <aclapi.h>

Socket make_handle_socket(HANDLE send_H, HANDLE recv_H, Plug plug,
                          int overlapped);

Socket new_named_pipe_client(const char *pipename, Plug plug)
{
    HANDLE pipehandle;
    PSID usersid, pipeowner;
    PSECURITY_DESCRIPTOR psd;
    char *err;
    Socket ret;

    extern int advapi_initialised;
    init_advapi();           /* for get_user_sid. FIXME: do better. */

    assert(strncmp(pipename, "\\\\.\\pipe\\", 9) == 0);
    assert(strchr(pipename + 9, '\\') == NULL);

    pipehandle = CreateFile(pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (pipehandle == INVALID_HANDLE_VALUE) {
        err = dupprintf("Unable to open named pipe '%s': %s",
                        pipename, win_strerror(GetLastError()));
        ret = new_error_socket(err, plug);
        sfree(err);
        return ret;
    }

    if ((usersid = get_user_sid()) == NULL) {
        CloseHandle(pipehandle);
        err = dupprintf("Unable to get user SID");
        ret = new_error_socket(err, plug);
        sfree(err);
        return ret;
    }

    if (GetSecurityInfo(pipehandle, SE_KERNEL_OBJECT,
                        OWNER_SECURITY_INFORMATION,
                        &pipeowner, NULL, NULL, NULL,
                        &psd) != ERROR_SUCCESS) {
        err = dupprintf("Unable to get named pipe security information: %s",
                        win_strerror(GetLastError()));
        ret = new_error_socket(err, plug);
        sfree(err);
        CloseHandle(pipehandle);
        sfree(usersid);
        return ret;
    }

    if (!EqualSid(pipeowner, usersid)) {
        err = dupprintf("Owner of named pipe '%s' is not us", pipename);
        ret = new_error_socket(err, plug);
        sfree(err);
        CloseHandle(pipehandle);
        LocalFree(psd);
        sfree(usersid);
        return ret;
    }

    LocalFree(psd);
    sfree(usersid);

    return make_handle_socket(pipehandle, pipehandle, plug, TRUE);
}

#endif /* !defined NO_SECURITY */
