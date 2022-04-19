#include "misc.h"
#include "ssh.h"

unsigned nullkey_supported_flags(const ssh_keyalg *self)
{
    return 0;
}

const char *nullkey_alternate_ssh_id(const ssh_keyalg *self, unsigned flags)
{
    /* There are no alternate ids */
    return self->ssh_id;
}
