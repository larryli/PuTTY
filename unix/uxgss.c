#include <string.h>
#include <gssapi/gssapi.h>
#include "putty.h"
#include "sshgss.h"
#include "misc.h"

#ifndef NO_GSSAPI

static gss_OID_desc putty_gss_mech_krb5_desc =
    { 9, (void *)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };
static gss_OID const putty_gss_mech_krb5 = &putty_gss_mech_krb5_desc;

typedef struct uxSsh_gss_ctx {
    OM_uint32 maj_stat;
    OM_uint32 min_stat;
    gss_ctx_id_t ctx;
} uxSsh_gss_ctx;

int ssh_gss_init(void)
{
    /* On Windows this tries to load the SSPI library functions.  On
       Unix we assume we have GSSAPI at runtime if we were linked with
       it at compile time */
    return 1;
}

Ssh_gss_stat ssh_gss_indicate_mech(Ssh_gss_buf *mech)
{
    /* Copy constant into mech */
    mech->length  = putty_gss_mech_krb5->length;
    mech->value = putty_gss_mech_krb5->elements;

    return SSH_GSS_OK;
}

Ssh_gss_stat ssh_gss_import_name(char *host,
                                 Ssh_gss_name *srv_name)
{
    OM_uint32 min_stat,maj_stat;
    gss_buffer_desc host_buf;
    char *pStr;

    pStr = dupcat("host@", host, NULL);

    host_buf.value = pStr;
    host_buf.length = strlen(pStr);

    maj_stat = gss_import_name(&min_stat, &host_buf,
			       GSS_C_NT_HOSTBASED_SERVICE,
			       (gss_name_t *)srv_name);
    /* Release buffer */
    sfree(pStr);
    if (maj_stat == GSS_S_COMPLETE) return SSH_GSS_OK;
    return SSH_GSS_FAILURE;
}

Ssh_gss_stat ssh_gss_acquire_cred(Ssh_gss_ctx *ctx)
{
    uxSsh_gss_ctx *uxctx = snew(uxSsh_gss_ctx);

    uxctx->maj_stat =  uxctx->min_stat = GSS_S_COMPLETE;
    uxctx->ctx = GSS_C_NO_CONTEXT;
    *ctx = (Ssh_gss_ctx) uxctx;

    return SSH_GSS_OK;
}

Ssh_gss_stat ssh_gss_init_sec_context(Ssh_gss_ctx *ctx,
                                      Ssh_gss_name srv_name,
                                      int to_deleg,
				      Ssh_gss_buf *recv_tok,
				      Ssh_gss_buf *send_tok)
{
    uxSsh_gss_ctx *uxctx = (uxSsh_gss_ctx*) *ctx;
    OM_uint32 ret_flags;

    if (to_deleg) to_deleg = GSS_C_DELEG_FLAG;
    uxctx->maj_stat = gss_init_sec_context(&uxctx->min_stat,
					   GSS_C_NO_CREDENTIAL,
					   &uxctx->ctx,
					   (gss_name_t) srv_name,
					   (gss_OID) putty_gss_mech_krb5,
					   GSS_C_MUTUAL_FLAG |
					   GSS_C_INTEG_FLAG | to_deleg,
					   0,
					   GSS_C_NO_CHANNEL_BINDINGS,
					   recv_tok,
					   NULL,   /* ignore mech type */
					   send_tok,
					   &ret_flags,
					   NULL);  /* ignore time_rec */
  
    if (uxctx->maj_stat == GSS_S_COMPLETE) return SSH_GSS_S_COMPLETE;
    if (uxctx->maj_stat == GSS_S_CONTINUE_NEEDED) return SSH_GSS_S_CONTINUE_NEEDED;
    return SSH_GSS_FAILURE;
}

Ssh_gss_stat ssh_gss_display_status(Ssh_gss_ctx ctx, Ssh_gss_buf *buf)
{
    uxSsh_gss_ctx *uxctx = (uxSsh_gss_ctx *) ctx;
    OM_uint32 lmin,lmax;
    OM_uint32 ccc;
    gss_buffer_desc msg_maj=GSS_C_EMPTY_BUFFER;
    gss_buffer_desc msg_min=GSS_C_EMPTY_BUFFER;

    /* Return empty buffer in case of failure */
    SSH_GSS_CLEAR_BUF(buf);

    /* get first mesg from GSS */
    ccc=0;
    lmax=gss_display_status(&lmin,uxctx->maj_stat,GSS_C_GSS_CODE,(gss_OID) putty_gss_mech_krb5,&ccc,&msg_maj);

    if (lmax != GSS_S_COMPLETE) return SSH_GSS_FAILURE;

    /* get first mesg from Kerberos */
    ccc=0;
    lmax=gss_display_status(&lmin,uxctx->min_stat,GSS_C_MECH_CODE,(gss_OID) putty_gss_mech_krb5,&ccc,&msg_min);

    if (lmax != GSS_S_COMPLETE) {
	gss_release_buffer(&lmin, &msg_maj);
	return SSH_GSS_FAILURE;
    }

    /* copy data into buffer */
    buf->length = msg_maj.length + msg_min.length + 1;
    buf->value = snewn(buf->length + 1, char);
  
    /* copy mem */
    memcpy((char *)buf->value, msg_maj.value, msg_maj.length);
    ((char *)buf->value)[msg_maj.length] = ' ';
    memcpy((char *)buf->value + msg_maj.length + 1, msg_min.value, msg_min.length);
    ((char *)buf->value)[buf->length] = 0;
    /* free mem & exit */
    gss_release_buffer(&lmin, &msg_maj);
    gss_release_buffer(&lmin, &msg_min);
    return SSH_GSS_OK;
}

Ssh_gss_stat ssh_gss_free_tok(Ssh_gss_buf *send_tok)
{
    OM_uint32 min_stat,maj_stat;
    maj_stat = gss_release_buffer(&min_stat, send_tok);
  
    if (maj_stat == GSS_S_COMPLETE) return SSH_GSS_OK;
    return SSH_GSS_FAILURE;
}

Ssh_gss_stat ssh_gss_release_cred(Ssh_gss_ctx *ctx)
{
    uxSsh_gss_ctx *uxctx = (uxSsh_gss_ctx *) *ctx;
    OM_uint32 min_stat;
    OM_uint32 maj_stat=GSS_S_COMPLETE;
  
    if (uxctx == NULL) return SSH_GSS_FAILURE;
    if (uxctx->ctx != GSS_C_NO_CONTEXT)
	maj_stat = gss_delete_sec_context(&min_stat,&uxctx->ctx,GSS_C_NO_BUFFER);
    sfree(uxctx);
  
    if (maj_stat == GSS_S_COMPLETE) return SSH_GSS_OK;
    return SSH_GSS_FAILURE;
}


Ssh_gss_stat ssh_gss_release_name(Ssh_gss_name *srv_name)
{
    OM_uint32 min_stat,maj_stat;
    maj_stat = gss_release_name(&min_stat, (gss_name_t *) srv_name);
  
    if (maj_stat == GSS_S_COMPLETE) return SSH_GSS_OK;
    return SSH_GSS_FAILURE;
}

Ssh_gss_stat ssh_gss_get_mic(Ssh_gss_ctx ctx, Ssh_gss_buf *buf,
			     Ssh_gss_buf *hash) 
{
    uxSsh_gss_ctx *uxctx = (uxSsh_gss_ctx *) ctx;
    if (uxctx == NULL) return SSH_GSS_FAILURE;
    return gss_get_mic(&(uxctx->min_stat), uxctx->ctx, 0, buf, hash);
}

Ssh_gss_stat ssh_gss_free_mic(Ssh_gss_buf *hash)
{
    /* On Unix this is the same freeing process as ssh_gss_free_tok. */
    return ssh_gss_free_tok(hash);
}

#else

/* Dummy function so this source file defines something if NO_GSSAPI
   is defined. */

int ssh_gss_init(void)
{
    return 1;
}

#endif
