#include "defs.h"
#include "misc.h"
#include "storage.h"

void host_ca_free(host_ca *hca)
{
    sfree(hca->name);
    if (hca->ca_public_key)
        strbuf_free(hca->ca_public_key);
    for (size_t i = 0; i < hca->n_hostname_wildcards; i++)
        sfree(hca->hostname_wildcards[i]);
    sfree(hca->hostname_wildcards);
    sfree(hca);
}
