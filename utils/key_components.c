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
    kc->components[n].type = KCT_TEXT;
    kc->components[n].str = strbuf_dup_nm(ptrlen_from_asciz(value));
}

void key_components_add_mp(key_components *kc,
                           const char *name, mp_int *value)
{
    sgrowarray(kc->components, kc->componentsize, kc->ncomponents);
    size_t n = kc->ncomponents++;
    kc->components[n].name = dupstr(name);
    kc->components[n].type = KCT_MPINT;
    kc->components[n].mp = mp_copy(value);
}

void key_components_free(key_components *kc)
{
    for (size_t i = 0; i < kc->ncomponents; i++) {
        key_component *comp = &kc->components[i];
        sfree(comp->name);
        switch (comp->type) {
          case KCT_MPINT:
            mp_free(comp->mp);
            break;
          case KCT_TEXT:
            strbuf_free(comp->str);
            break;
          default:
            unreachable("bad key component type");
        }
    }
    sfree(kc->components);
    sfree(kc);
}
