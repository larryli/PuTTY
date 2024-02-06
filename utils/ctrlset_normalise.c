/*
 * Helper function from the dialog.h mechanism.
 */

#include "putty.h"
#include "misc.h"
#include "dialog.h"

void ctrlset_normalise_aligns(struct controlset *s)
{
    /*
     * The algorithm in here is quadratic time. Never on very much data, but
     * even so, let's avoid bothering to use it where possible. In most
     * controlsets, there's no use of align_next_to in any case, so we have
     * nothing to do.
     */
    for (size_t j = 0; j < s->ncontrols; j++)
        if (s->ctrls[j]->align_next_to)
            goto must_do_something;
    /* If we fell out of this loop, there's nothing to do here */
    return;
  must_do_something:;

    size_t *idx = snewn(s->ncontrols, size_t);

    /*
     * Follow align_next_to links to identify, for each control, the least
     * index within this controlset of things it's linked to. That way,
     * controls with the same idx[j] will be in the same alignment class.
     */
    for (size_t j = 0; j < s->ncontrols; j++) {
        dlgcontrol *c = s->ctrls[j];
        idx[j] = j;
        if (c->align_next_to) {
            for (size_t k = 0; k < j; k++) {
                if (s->ctrls[k] == c->align_next_to) {
                    idx[j] = idx[k];
                    break;
                }
            }
        }
    }

    /*
     * Having done that, re-link each control to the most recent one in its
     * class, so that the links form a backward linked list.
     */
    for (size_t j = 0; j < s->ncontrols; j++) {
        dlgcontrol *c = s->ctrls[j];
        c->align_next_to = NULL;
        for (size_t k = j; k-- > 0 ;) {
            if (idx[k] == idx[j]) {
                c->align_next_to = s->ctrls[k];
                break;
            }
        }
    }

    sfree(idx);
}
