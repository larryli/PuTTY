#include <time.h>
#include <assert.h>

struct tm ltime(void)
{
    time_t t;
    time(&t);
    assert (t != ((time_t)-1));
    return *localtime(&t);
}
