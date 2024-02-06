/*
 * Stub version of setup_ca_config_box, for tools that don't have SSH
 * code linked in.
 */

#include "putty.h"
#include "dialog.h"

const bool has_ca_config_box = false;

void setup_ca_config_box(struct controlbox *b)
{
    unreachable("should never call setup_ca_config_box in this application");
}
