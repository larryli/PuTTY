/*
 * winsecur.c: implementation of winsecur.h.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

#if !defined NO_SECURITY

#define WINSECUR_GLOBAL
#include "winsecur.h"

int got_advapi(void)
{
    static int attempted = FALSE;
    static int successful;
    static HMODULE advapi;

    if (!attempted) {
        attempted = TRUE;
        advapi = load_system32_dll("advapi32.dll");
        successful = advapi &&
            GET_WINDOWS_FUNCTION(advapi, GetSecurityInfo) &&
            GET_WINDOWS_FUNCTION(advapi, OpenProcessToken) &&
            GET_WINDOWS_FUNCTION(advapi, GetTokenInformation) &&
            GET_WINDOWS_FUNCTION(advapi, InitializeSecurityDescriptor) &&
            GET_WINDOWS_FUNCTION(advapi, SetSecurityDescriptorOwner);
    }
    return successful;
}

PSID get_user_sid(void)
{
    HANDLE proc = NULL, tok = NULL;
    TOKEN_USER *user = NULL;
    DWORD toklen, sidlen;
    PSID sid = NULL, ret = NULL;

    if (!got_advapi())
        goto cleanup;

    if ((proc = OpenProcess(MAXIMUM_ALLOWED, FALSE,
                            GetCurrentProcessId())) == NULL)
        goto cleanup;

    if (!p_OpenProcessToken(proc, TOKEN_QUERY, &tok))
        goto cleanup;

    if (!p_GetTokenInformation(tok, TokenUser, NULL, 0, &toklen) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        goto cleanup;

    if ((user = (TOKEN_USER *)LocalAlloc(LPTR, toklen)) == NULL)
        goto cleanup;

    if (!p_GetTokenInformation(tok, TokenUser, user, toklen, &toklen))
        goto cleanup;

    sidlen = GetLengthSid(user->User.Sid);

    sid = (PSID)smalloc(sidlen);

    if (!CopySid(sidlen, sid, user->User.Sid))
        goto cleanup;

    /* Success. Move sid into the return value slot, and null it out
     * to stop the cleanup code freeing it. */
    ret = sid;
    sid = NULL;

  cleanup:
    if (proc != NULL)
        CloseHandle(proc);
    if (tok != NULL)
        CloseHandle(tok);
    if (user != NULL)
        LocalFree(user);
    if (sid != NULL)
        sfree(sid);

    return ret;
}

#endif /* !defined NO_SECURITY */
