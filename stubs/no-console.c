/*
 * Stub functions for when console.c is not linked into a program.
 */

#include "putty.h"

bool console_set_batch_mode(bool newvalue)
{
    return false;
}
