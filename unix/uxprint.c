/*
 * Printing interface for PuTTY.
 */

#include <assert.h>
#include "putty.h"

printer_job *printer_start_job(char *printer)
{
    /* FIXME: open pipe to lpr */
    return NULL;
}

void printer_job_data(printer_job *pj, void *data, int len)
{
    /* FIXME: receive a pipe to lpr, write things to it */
    assert(!"We shouldn't get here");
}

void printer_finish_job(printer_job *pj)
{
    /* FIXME: receive a pipe to lpr, close it */
    assert(!"We shouldn't get here either");
}
