#include "putty.h"
#ifndef NO_GSSAPI
#include "pgssapi.h"
#include "sshgss.h"
#include "sshgssc.h"

/* Unix code to set up the GSSAPI library list. */

struct ssh_gss_library ssh_gss_libraries[3];
int n_ssh_gss_libraries = 0;
static int initialised = FALSE;

const int ngsslibs = 3;
const char *const gsslibnames[3] = {
    "libgssapi (Heimdal)",
    "libgssapi_krb5 (MIT Kerberos)",
    "libgss (Sun)",
};
const struct keyval gsslibkeywords[] = {
    { "libgssapi", 0 },
    { "libgssapi_krb5", 1 },
    { "libgss", 2 },
};

#ifndef NO_LIBDL

/*
 * Run-time binding against a choice of GSSAPI implementations. We
 * try loading several libraries, and produce an entry in
 * ssh_gss_libraries[] for each one.
 */

static void gss_init(struct ssh_gss_library *lib, void *dlhandle,
		     int id, const char *msg)
{
    lib->id = id;
    lib->gsslogmsg = msg;

#define BIND_GSS_FN(name) \
    lib->u.gssapi.name = (t_gss_##name) dlsym(dlhandle, "gss_" #name)

    BIND_GSS_FN(delete_sec_context);
    BIND_GSS_FN(display_status);
    BIND_GSS_FN(get_mic);
    BIND_GSS_FN(import_name);
    BIND_GSS_FN(init_sec_context);
    BIND_GSS_FN(release_buffer);
    BIND_GSS_FN(release_cred);
    BIND_GSS_FN(release_name);

#undef BIND_GSS_FN

    ssh_gssapi_bind_fns(lib);
}

/* Dynamically load gssapi libs. */
void ssh_gss_init(void)
{
    void *gsslib;

    if (initialised) return;
    initialised = TRUE;

    /* Heimdal's GSSAPI Library */
    if ((gsslib = dlopen("libgssapi.so.2", RTLD_LAZY)) != NULL)
	gss_init(&ssh_gss_libraries[n_ssh_gss_libraries++], gsslib,
		 0, "Using GSSAPI from libgssapi.so.2");

    /* MIT Kerberos's GSSAPI Library */
    if ((gsslib = dlopen("libgssapi_krb5.so.2", RTLD_LAZY)) != NULL)
	gss_init(&ssh_gss_libraries[n_ssh_gss_libraries++], gsslib,
		 1, "Using GSSAPI from libgssapi_krb5.so.2");

    /* Sun's GSSAPI Library */
    if ((gsslib = dlopen("libgss.so.1", RTLD_LAZY)) != NULL)
	gss_init(&ssh_gss_libraries[n_ssh_gss_libraries++], gsslib,
		 2, "Using GSSAPI from libgss.so.1");
}

#else /* NO_LIBDL */

/*
 * Link-time binding against GSSAPI. Here we just construct a single
 * library structure containing pointers to the functions we linked
 * against.
 */

#include <gssapi/gssapi.h>

/* Dynamically load gssapi libs. */
void ssh_gss_init(void)
{
    if (initialised) return;
    initialised = TRUE;

    n_ssh_gss_libraries = 1;
    ssh_gss_libraries[0].gsslogmsg = "Using statically linked GSSAPI";

#define BIND_GSS_FN(name) \
    ssh_gss_libraries[0].u.gssapi.name = (t_gss_##name) gss_##name

    BIND_GSS_FN(delete_sec_context);
    BIND_GSS_FN(display_status);
    BIND_GSS_FN(get_mic);
    BIND_GSS_FN(import_name);
    BIND_GSS_FN(init_sec_context);
    BIND_GSS_FN(release_buffer);
    BIND_GSS_FN(release_cred);
    BIND_GSS_FN(release_name);

#undef BIND_GSS_FN

    ssh_gssapi_bind_fns(&ssh_gss_libraries[0]);
}

#endif /* NO_LIBDL */

#endif /* NO_GSSAPI */
