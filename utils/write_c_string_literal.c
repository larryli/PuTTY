/*
 * Write data to a file in the form of a C string literal, with any
 * non-printable-ASCII character escaped appropriately.
 */

#include "defs.h"
#include "misc.h"

void write_c_string_literal(FILE *fp, ptrlen str)
{
    for (const char *p = str.ptr; p < (const char *)str.ptr + str.len; p++) {
        char c = *p;

        if (c == '\n')
            fputs("\\n", fp);
        else if (c == '\r')
            fputs("\\r", fp);
        else if (c == '\t')
            fputs("\\t", fp);
        else if (c == '\b')
            fputs("\\b", fp);
        else if (c == '\\')
            fputs("\\\\", fp);
        else if (c == '"')
            fputs("\\\"", fp);
        else if (c >= 32 && c <= 126)
            fputc(c, fp);
        else
            fprintf(fp, "\\%03o", (unsigned char)c);
    }
}
