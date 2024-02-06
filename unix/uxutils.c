#include "putty.h"
#include "ssh.h"

#if defined __linux__ && (defined __arm__ || defined __aarch64__) && \
    HAVE_SYS_AUXV_H && HAVE_ASM_HWCAP_H

#include <sys/auxv.h>
#include <asm/hwcap.h>

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

#else

bool platform_aes_hw_available(void)
{
    return false;
}

bool platform_sha256_hw_available(void)
{
    return false;
}

bool platform_sha1_hw_available(void)
{
    return false;
}

#endif
