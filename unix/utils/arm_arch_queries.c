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
    /* M1 macOS defines no optional sysctl flag indicating presence of
     * the AES extension, which I assume to be because it's always
     * present */
    return true;
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
    /* Assume always present on M1 macOS, similarly to AES */
    return true;
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
    /* Assume always present on M1 macOS, similarly to AES */
    return true;
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
    return test_sysctl_flag("hw.optional.armv8_2_sha512");
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
