/*
 * uxutils.h: header included only by uxutils.c.
 *
 * The only reason this is a header file instead of a source file is
 * so that I can define 'static inline' functions which may or may not
 * be used, without provoking a compiler warning when I turn out not
 * to use them in the subsequent source file.
 */

#ifndef PUTTY_UXUTILS_H
#define PUTTY_UXUTILS_H

#if defined __APPLE__
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#endif /* defined __APPLE__ */

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

#endif /* defined __arm__ || defined __aarch64__ */

#if defined __APPLE__
static inline bool test_sysctl_flag(const char *flagname)
{
#ifdef HAVE_SYSCTLBYNAME
    int value;
    size_t size = sizeof(value);
    return (sysctlbyname(flagname, &value, &size, NULL, 0) == 0 &&
            size == sizeof(value) && value != 0);
#else /* HAVE_SYSCTLBYNAME */
    return false;
#endif /* HAVE_SYSCTLBYNAME */
}
#endif /* defined __APPLE__ */

#endif /* PUTTY_UXUTILS_H */
