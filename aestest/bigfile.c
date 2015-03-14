/**
 * bigfile.c
 *
 * Generating files full of pseudorandom numbers
 *
 * @author kryukov@frtk.ru
 * @version 1.0
 *
 * For Putty AES NI project
 * http://putty-aes-ni.googlecode.com/
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Syntax error!\n");
        fprintf(stderr, "First argument is the amount of symbols, second is filename\n");
        return 1;
    }

    srand(time(NULL));

    const unsigned long N = strtoul(argv[1], NULL, 0);
    unsigned i;

    FILE* fp = fopen(argv[2], "w");

    for (i = 0; i < N; ++i)
        fprintf(fp, "%c", rand() & 0xFF);

    fclose(fp);

    return 0;
}
