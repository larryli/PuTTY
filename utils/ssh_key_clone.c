/*
 * Make a copy of an existing ssh_key object, e.g. to survive after
 * the original is freed.
 */

#include "misc.h"
#include "ssh.h"

ssh_key *ssh_key_clone(ssh_key *key)
{
    /*
     * To avoid having to add a special method in the vtable API, we
     * clone by round-tripping through public and private blobs.
     */
    strbuf *pub = strbuf_new_nm();
    ssh_key_public_blob(key, BinarySink_UPCAST(pub));

    ssh_key *copy;

    if (ssh_key_has_private(key)) {
        strbuf *priv = strbuf_new_nm();
        ssh_key_private_blob(key, BinarySink_UPCAST(priv));
        copy = ssh_key_new_priv(ssh_key_alg(key), ptrlen_from_strbuf(pub),
                                ptrlen_from_strbuf(priv));
        strbuf_free(priv);
    } else {
        copy = ssh_key_new_pub(ssh_key_alg(key), ptrlen_from_strbuf(pub));
    }

    strbuf_free(pub);
    return copy;
}
