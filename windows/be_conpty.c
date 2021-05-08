#include <stdio.h>
#include "putty.h"

const char *const appname = "pterm";

const int be_default_protocol = -1;

const struct BackendVtable *const backends[] = {
    &conpty_backend,
    NULL
};

const size_t n_ui_backends = 1;
