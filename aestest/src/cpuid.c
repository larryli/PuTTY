/**
 * cpuid.c
 *
 * Checks if CPU has support of AES instructions
 *
 * @author kryukov@frtk.ru
 * @version 3.0
 *
 * For Putty AES NI project
 * http://putty-aes-ni.googlecode.com/
 */

#ifndef SILENT
#include <stdio.h>
#endif

#ifdef __GNUC__
static void __cpuid(unsigned int* CPUInfo, int func)
{
    __asm__ __volatile__
    (
        "cpuid"
        : "=a" (CPUInfo[0])
        , "=b" (CPUInfo[1])
        , "=c" (CPUInfo[2])
        , "=d" (CPUInfo[3])
        : "a"  (func)
    );
}
#endif

static int CheckCPUsupportAES()
{
    unsigned int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    return CPUInfo[2] & (1 << 25);
}

int main(int argc, char ** argv)
{
    const int res = !CheckCPUsupportAES();
#ifndef SILENT
    printf("This CPU %s AES-NI\n", res ? "does not support" : "supports");
#endif
    return res;
}
