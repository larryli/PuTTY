/*
 * Linking module for programs that do not support selection of backend
 * (such as pscp or pterm).
 */

#include <stdio.h>
#include "putty.h"

Backend *backends[] = {
    NULL
};
