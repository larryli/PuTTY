/*
 * Centralised functions for the Interactor trait.
 */

#include "putty.h"

InteractionReadySeat interactor_announce(Interactor *itr)
{
    Seat *seat = interactor_get_seat(itr);

    /* TODO: print an announcement of this Interactor's identity, when
     * appropriate */

    InteractionReadySeat iseat;
    iseat.seat = seat;

    return iseat;
}
