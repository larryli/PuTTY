/*
 * Dummy (lack-of-)implementation of a GUI password/passphrase prompt.
 */

#include "putty.h"

void random_add_noise(NoiseSourceId source, const void *noise, int length)
{
    /* We have no keypress_prng here, so no need to implement this */
}

const bool buildinfo_gtk_relevant = false;

char *gtk_askpass_main(const char *display, const char *wintitle,
                       const char *prompt, bool *success)
{
    *success = false;
    return dupstr("this Pageant was built without GTK");
}
