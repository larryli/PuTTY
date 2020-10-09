#include "putty.h"
#include "ssh.h"

#if defined __arm__ || defined __aarch64__

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_AUXV_H
#include <sys/auxv.h>
#endif

#ifdef HAVE_ASM_HWCAP_H
#include <asm/hwcap.h>
#endif

#if defined HAVE_GETAUXVAL
/* No code needed: getauxval has just the API we want already */
#elif defined HAVE_ELF_AUX_INFO
/* Implement the simple getauxval API in terms of FreeBSD elf_aux_info */
static inline u_long getauxval(int which)
{
    u_long toret;
    if (elf_aux_info(which, &toret, sizeof(toret)) != 0)
        return 0;                      /* elf_aux_info didn't work */
    return toret;
}
#else
/* Implement a stub getauxval which returns no capabilities */
static inline u_long getauxval(int which) { return 0; }
#endif

bool platform_aes_hw_available(void)
{
#if defined HWCAP_AES
    return getauxval(AT_HWCAP) & HWCAP_AES;
#elif defined HWCAP2_AES
    return getauxval(AT_HWCAP2) & HWCAP2_AES;
#else
    return false;
#endif
}

bool platform_sha256_hw_available(void)
{
#if defined HWCAP_SHA2
    return getauxval(AT_HWCAP) & HWCAP_SHA2;
#elif defined HWCAP2_SHA2
    return getauxval(AT_HWCAP2) & HWCAP2_SHA2;
#else
    return false;
#endif
}

bool platform_sha1_hw_available(void)
{
#if defined HWCAP_SHA1
    return getauxval(AT_HWCAP) & HWCAP_SHA1;
#elif defined HWCAP2_SHA1
    return getauxval(AT_HWCAP2) & HWCAP2_SHA1;
#else
    return false;
#endif
}

#endif /* defined __arm__ || defined __aarch64__ */
