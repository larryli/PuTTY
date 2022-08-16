/*
 * Header included only by arm_arch_queries.c.
 *
 * The only reason this is a header file instead of a source file is
 * so that I can define 'static inline' functions which may or may not
 * be used, without provoking a compiler warning when I turn out not
 * to use them in the subsequent source file.
 */

#ifndef PUTTY_ARM_ARCH_QUERIES_H
#define PUTTY_ARM_ARCH_QUERIES_H

#if defined __APPLE__
#if HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#endif /* defined __APPLE__ */

#if defined __arm__ || defined __aarch64__

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_AUXV_H
#include <sys/auxv.h>
#endif

#if HAVE_ASM_HWCAP_H
#include <asm/hwcap.h>
#endif

#if HAVE_GETAUXVAL
/* No code needed: getauxval has just the API we want already */
#elif HAVE_ELF_AUX_INFO
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

#endif /* defined __arm__ || defined __aarch64__ */

#if defined __APPLE__
typedef enum { SYSCTL_MISSING, SYSCTL_OFF, SYSCTL_ON } SysctlResult;

static inline SysctlResult test_sysctl_flag(const char *flagname)
{
#if HAVE_SYSCTLBYNAME
    int value;
    size_t size = sizeof(value);
    if (sysctlbyname(flagname, &value, &size, NULL, 0) == 0 &&
        size == sizeof(value)) {
        return value != 0 ? SYSCTL_ON : SYSCTL_OFF;
    }
#else /* HAVE_SYSCTLBYNAME */
    return SYSCTL_MISSING;
#endif /* HAVE_SYSCTLBYNAME */
}
#endif /* defined __APPLE__ */

#endif /* PUTTY_ARM_ARCH_QUERIES_H */
