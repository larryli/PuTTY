#include "ssh.h"
#include "mpint.h"

key_components *key_components_new(void)
{
    key_components *kc = snew(key_components);
    kc->ncomponents = 0;
    kc->componentsize = 0;
    kc->components = NULL;
    return kc;
}

void key_components_add_text(key_components *kc,
                             const char *name, const char *value)
{
    sgrowarray(kc->components, kc->componentsize, kc->ncomponents);
    size_t n = kc->ncomponents++;
    kc->components[n].name = dupstr(name);
    kc->components[n].is_mp_int = false;
    kc->components[n].text = dupstr(value);
}

void key_components_add_mp(key_components *kc,
                           const char *name, mp_int *value)
{
    sgrowarray(kc->components, kc->componentsize, kc->ncomponents);
    size_t n = kc->ncomponents++;
    kc->components[n].name = dupstr(name);
    kc->components[n].is_mp_int = true;
    kc->components[n].mp = mp_copy(value);
}

void key_components_free(key_components *kc)
{
    for (size_t i = 0; i < kc->ncomponents; i++) {
        sfree(kc->components[i].name);
        if (kc->components[i].is_mp_int) {
            mp_free(kc->components[i].mp);
        } else {
            smemclr(kc->components[i].text, strlen(kc->components[i].text));
            sfree(kc->components[i].text);
        }
    }
    sfree(kc->components);
    sfree(kc);
}
