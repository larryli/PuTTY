/*
 * Centralised functions for the Interactor trait.
 */

#include "putty.h"

Seat *interactor_borrow_seat(Interactor *itr)
{
    Seat *clientseat = interactor_get_seat(itr);
    if (!clientseat)
        return NULL;

    /* If the client has already had its Seat borrowed, then look
     * through the existing TempSeat to find the underlying one. */
    if (is_tempseat(clientseat))
        return tempseat_get_real(clientseat);

    /* Otherwise, make a new TempSeat and give that to the client. */
    Seat *tempseat = tempseat_new(clientseat);
    interactor_set_seat(itr, tempseat);
    return clientseat;
}

void interactor_return_seat(Interactor *itr)
{
    Seat *tempseat = interactor_get_seat(itr);
    if (!is_tempseat(tempseat))
        return;                        /* no-op */

    tempseat_flush(tempseat);
    Seat *realseat = tempseat_get_real(tempseat);
    interactor_set_seat(itr, realseat);
    tempseat_free(tempseat);

    /*
     * We're about to hand this seat back to the parent Interactor to
     * do its own thing with. It will typically expect to start in the
     * same state as if the seat had never been borrowed, i.e. in the
     * starting trust state.
     */
    seat_set_trust_status(realseat, true);
}

InteractionReadySeat interactor_announce(Interactor *itr)
{
    Seat *seat = interactor_get_seat(itr);

    /* TODO: print an announcement of this Interactor's identity, when
     * appropriate */

    InteractionReadySeat iseat;
    iseat.seat = seat;

    return iseat;
}
