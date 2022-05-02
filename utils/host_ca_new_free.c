#include "defs.h"
#include "misc.h"
#include "storage.h"

host_ca *host_ca_new(void)
{
    host_ca *hca = snew(host_ca);
    memset(hca, 0, sizeof(*hca));
    hca->opts.permit_rsa_sha1 = false;
    hca->opts.permit_rsa_sha256 = true;
    hca->opts.permit_rsa_sha512 = true;
    return hca;
}

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
