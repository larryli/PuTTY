/*
 * Stub version of uxsel.c, for test programs.
 */

#include "putty.h"

void uxsel_init(void)
{
}

void uxsel_set(int fd, int rwx, uxsel_callback_fn callback)
{
}

void uxsel_del(int fd)
{
}

int next_fd(int *state, int *rwx)
{
    return -1;
}

int first_fd(int *state, int *rwx)
{
    return -1;
}

void select_result(int fd, int event)
{
}
