/*
 * Stub definitions of the GSSAPI library list, for Unix pterm and
 * any other application that needs the symbols defined but has no
 * use for them.
 */

#include "putty.h"

#include "ssh/pgssapi.h"
#include "ssh/gss.h"
#include "ssh/gssc.h"

const int ngsslibs = 0;
const char *const gsslibnames[1] = { "dummy" };
const struct keyvalwhere gsslibkeywords[1] = { { "dummy", 0, -1, -1 } };

struct ssh_gss_liblist *ssh_gss_setup(Conf *conf)
{
    struct ssh_gss_liblist *list = snew(struct ssh_gss_liblist);

    list->libraries = NULL;
    list->nlibraries = 0;
    return list;
}

void ssh_gss_cleanup(struct ssh_gss_liblist *list)
{
    sfree(list->libraries); /* I know it's always NULL, but stay consistent */
    sfree(list);
}
