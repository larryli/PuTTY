/*
 * Printing interface for PuTTY.
 */

#include <assert.h>
#include <stdio.h>
#include "putty.h"

struct printer_job_tag {
    FILE *fp;
};

printer_job *printer_start_job(char *printer)
{
    printer_job *ret = smalloc(sizeof(printer_job));
    /*
     * On Unix, we treat the printer string as the name of a
     * command to pipe to - typically lpr, of course.
     */
    ret->fp = popen(printer, "w");
    if (!ret->fp) {
	sfree(ret);
	ret = NULL;
    }
    return ret;
}

void printer_job_data(printer_job *pj, void *data, int len)
{
    if (!pj)
	return;

    fwrite(data, 1, len, pj->fp);
}

void printer_finish_job(printer_job *pj)
{
    if (!pj)
	return;

    pclose(pj->fp);
    sfree(pj);
}
