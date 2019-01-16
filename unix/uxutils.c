#include "ssh.h"

#if defined __linux__ && (defined __arm__ || defined __aarch64__)

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

#else

bool platform_aes_hw_available(void)
{
    return false;
}

#endif
