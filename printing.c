/*
 * Printing interface for PuTTY.
 */

#include <windows.h>
#include "putty.h"

struct printer_enum_tag {
    int nprinters;
    LPPRINTER_INFO_5 info;
};

struct printer_job_tag {
    HANDLE hprinter;
};

printer_enum *printer_start_enum(int *nprinters_ptr)
{
    printer_enum *ret = smalloc(sizeof(printer_enum));
    char *buffer = NULL;
    DWORD needed, nprinters;

    *nprinters_ptr = 0;		       /* default return value */
    buffer = smalloc(512);
    if (EnumPrinters(PRINTER_ENUM_LOCAL, NULL, 5,
		     buffer, 512, &needed, &nprinters) == 0)
	goto error;

    if (needed) {
        buffer = srealloc(buffer, needed);

        if (EnumPrinters(PRINTER_ENUM_LOCAL, NULL, 5,
                         (LPBYTE)buffer, needed, &needed, &nprinters) == 0)
            goto error;
    } else {
        nprinters = 0;
        ret->info = NULL;
    }

    ret->info = (LPPRINTER_INFO_5)buffer;
    ret->nprinters = *nprinters_ptr = nprinters;
    
    return ret;

    error:
    sfree(buffer);
    sfree(ret);
    return NULL;
}

char *printer_get_name(printer_enum *pe, int i)
{
    if (!pe)
	return NULL;
    if (i < 0 || i >= pe->nprinters)
	return NULL;
    return pe->info[i].pPrinterName;
}

void printer_finish_enum(printer_enum *pe)
{
    if (!pe)
	return;
    sfree(pe->info);
    sfree(pe);
}

printer_job *printer_start_job(char *printer)
{
    printer_job *ret = smalloc(sizeof(printer_job));
    DOC_INFO_1 docinfo;
    int jobstarted = 0, pagestarted = 0;

    ret->hprinter = NULL;
    if (!OpenPrinter(printer, &ret->hprinter, NULL))
	goto error;

    docinfo.pDocName = "PuTTY remote printer output";
    docinfo.pOutputFile = NULL;
    docinfo.pDatatype = "RAW";

    if (!StartDocPrinter(ret->hprinter, 1, (LPSTR)&docinfo))
	goto error;
    jobstarted = 1;

    if (!StartPagePrinter(ret->hprinter))
	goto error;
    pagestarted = 1;

    return ret;

    error:
    if (pagestarted)
	EndPagePrinter(ret->hprinter);
    if (jobstarted)
	EndDocPrinter(ret->hprinter);
    if (ret->hprinter)
	ClosePrinter(ret->hprinter);
    sfree(ret);
    return NULL;
}

void printer_job_data(printer_job *pj, void *data, int len)
{
    DWORD written;

    if (!pj)
	return;

    WritePrinter(pj->hprinter, data, len, &written);
}

void printer_finish_job(printer_job *pj)
{
    if (!pj)
	return;

    EndPagePrinter(pj->hprinter);
    EndDocPrinter(pj->hprinter);
    ClosePrinter(pj->hprinter);
    sfree(pj);
}
