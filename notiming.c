/*
 * notiming.c: stub version of schedule_timer().
 * 
 * Used in key generation tools, which need the random number
 * generator but don't want the hassle of calling noise_regular()
 * at regular intervals - and don't _need_ it either, since they
 * have their own rigorous and different means of noise collection.
 */

#include "putty.h"

long schedule_timer(int ticks, timer_fn_t fn, void *ctx)
{
    return 0;
}
