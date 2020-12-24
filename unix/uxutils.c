#include "putty.h"
#include "ssh.h"

#include "uxutils.h"

#if defined __arm__ || defined __aarch64__

bool platform_aes_hw_available(void)
{
#if defined HWCAP_AES
    return getauxval(AT_HWCAP) & HWCAP_AES;
#elif defined HWCAP2_AES
    return getauxval(AT_HWCAP2) & HWCAP2_AES;
#elif defined __APPLE__
    /* M1 macOS defines no optional sysctl flag indicating presence of
     * the AES extension, which I assume to be because it's always
     * present */
    return true;
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
#elif defined __APPLE__
    /* Assume always present on M1 macOS, similarly to AES */
    return true;
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
#elif defined __APPLE__
    /* Assume always present on M1 macOS, similarly to AES */
    return true;
#else
    return false;
#endif
}

bool platform_sha512_hw_available(void)
{
#if defined HWCAP_SHA512
    return getauxval(AT_HWCAP) & HWCAP_SHA512;
#elif defined HWCAP2_SHA512
    return getauxval(AT_HWCAP2) & HWCAP2_SHA512;
#elif defined __APPLE__
    return test_sysctl_flag("hw.optional.armv8_2_sha512");
#else
    return false;
#endif
}

#endif /* defined __arm__ || defined __aarch64__ */
