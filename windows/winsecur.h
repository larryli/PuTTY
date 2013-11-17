/*
 * winsecur.h: some miscellaneous security-related helper functions,
 * defined in winsecur.c, that use the advapi32 library. Also
 * centralises the machinery for dynamically loading that library.
 */

#if !defined NO_SECURITY

#include <aclapi.h>

#ifndef WINSECUR_GLOBAL
#define WINSECUR_GLOBAL extern
#endif

DECL_WINDOWS_FUNCTION(WINSECUR_GLOBAL, BOOL, OpenProcessToken,
		      (HANDLE, DWORD, PHANDLE));
DECL_WINDOWS_FUNCTION(WINSECUR_GLOBAL, BOOL, GetTokenInformation,
		      (HANDLE, TOKEN_INFORMATION_CLASS,
                       LPVOID, DWORD, PDWORD));
DECL_WINDOWS_FUNCTION(WINSECUR_GLOBAL, BOOL, InitializeSecurityDescriptor,
		      (PSECURITY_DESCRIPTOR, DWORD));
DECL_WINDOWS_FUNCTION(WINSECUR_GLOBAL, BOOL, SetSecurityDescriptorOwner,
		      (PSECURITY_DESCRIPTOR, PSID, BOOL));
DECL_WINDOWS_FUNCTION(WINSECUR_GLOBAL, DWORD, GetSecurityInfo,
		      (HANDLE, SE_OBJECT_TYPE, SECURITY_INFORMATION,
		       PSID *, PSID *, PACL *, PACL *,
		       PSECURITY_DESCRIPTOR *));

int got_advapi(void);
PSID get_user_sid(void);

#endif
