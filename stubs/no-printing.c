/*
 * Stub module implementing the printing API for tools that don't
 * print.
 */

#include "putty.h"

printer_job *printer_start_job(char *printer) { return NULL; }
void printer_job_data(printer_job *pj, const void *data, size_t len) {}
void printer_finish_job(printer_job *pj) {}

printer_enum *printer_start_enum(int *nprinters_ptr)
{
    *nprinters_ptr = 0;
    return NULL;
}
char *printer_get_name(printer_enum *pe, int i) { return NULL; }
void printer_finish_enum(printer_enum *pe) {}
