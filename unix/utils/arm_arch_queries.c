/*
 * Unix implementation of the OS query functions that detect Arm
 * architecture extensions.
 */

#include "putty.h"
#include "ssh.h"

#include "utils/arm_arch_queries.h"

#if defined __arm__ || defined __aarch64__

bool platform_aes_neon_available(void)
{
#if defined HWCAP_AES
    return getauxval(AT_HWCAP) & HWCAP_AES;
#elif defined HWCAP2_AES
    return getauxval(AT_HWCAP2) & HWCAP2_AES;
#elif defined __APPLE__
    SysctlResult res = test_sysctl_flag("hw.optional.arm.FEAT_AES");
    /* Older M1 macOS didn't provide this flag, but as far as I know
     * implemented the crypto extension anyway, so treat 'feature
     * missing' as 'implemented' */
    return res != SYSCTL_OFF;
#else
    return false;
#endif
}

bool platform_pmull_neon_available(void)
{
#if defined HWCAP_PMULL
    return getauxval(AT_HWCAP) & HWCAP_PMULL;
#elif defined HWCAP2_PMULL
    return getauxval(AT_HWCAP2) & HWCAP2_PMULL;
#elif defined __APPLE__
    SysctlResult res = test_sysctl_flag("hw.optional.arm.FEAT_PMULL");
    /* As above, treat 'missing' as enabled */
    return res != SYSCTL_OFF;
#else
    return false;
#endif
}

bool platform_sha256_neon_available(void)
{
#if defined HWCAP_SHA2
    return getauxval(AT_HWCAP) & HWCAP_SHA2;
#elif defined HWCAP2_SHA2
    return getauxval(AT_HWCAP2) & HWCAP2_SHA2;
#elif defined __APPLE__
    SysctlResult res = test_sysctl_flag("hw.optional.arm.FEAT_SHA256");
    /* As above, treat 'missing' as enabled */
    return res != SYSCTL_OFF;
#else
    return false;
#endif
}

bool platform_sha1_neon_available(void)
{
#if defined HWCAP_SHA1
    return getauxval(AT_HWCAP) & HWCAP_SHA1;
#elif defined HWCAP2_SHA1
    return getauxval(AT_HWCAP2) & HWCAP2_SHA1;
#elif defined __APPLE__
    SysctlResult res = test_sysctl_flag("hw.optional.arm.FEAT_SHA1");
    /* As above, treat 'missing' as enabled */
    return res != SYSCTL_OFF;
#else
    return false;
#endif
}

bool platform_sha512_neon_available(void)
{
#if defined HWCAP_SHA512
    return getauxval(AT_HWCAP) & HWCAP_SHA512;
#elif defined HWCAP2_SHA512
    return getauxval(AT_HWCAP2) & HWCAP2_SHA512;
#elif defined __APPLE__
    /* There are two sysctl flags for this, apparently invented at
     * different times. Try both, falling back to the older one. */
    SysctlResult res = test_sysctl_flag("hw.optional.arm.FEAT_SHA512");
    if (res != SYSCTL_MISSING)
        return res == SYSCTL_ON;

    res = test_sysctl_flag("hw.optional.armv8_2_sha512");
    return res == SYSCTL_ON;
#else
    return false;
#endif
}

#else /* defined __arm__ || defined __aarch64__ */

/*
 * Include _something_ in this file to prevent an annoying compiler
 * warning, and to avoid having to condition out this file in
 * CMakeLists. It's in a library, so this variable shouldn't end up in
 * any actual program, because nothing will refer to it.
 */
const int arm_arch_queries_dummy_variable = 0;

#endif /* defined __arm__ || defined __aarch64__ */
