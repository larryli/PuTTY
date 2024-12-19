/*
 * Stub version of the callback.c functions. Doesn't let anyone
 * _schedule_ a callback (because that would lead them into the false
 * assumption that it would actually happen later on), but permits the
 * other functions without error, on the grounds that it's well
 * defined what they would do if nobody had scheduled any callbacks.
 */

#include "putty.h"

void queue_idempotent_callback(struct IdempotentCallback *ic)
{
    unreachable("callbacks are not supported in this application");
}

void delete_callbacks_for_context(void *ctx)
{
}

void queue_toplevel_callback(toplevel_callback_fn_t fn, void *ctx)
{
    unreachable("callbacks are not supported in this application");
}

bool run_toplevel_callbacks(void)
{
    return false;
}

bool toplevel_callback_pending(void)
{
    return false;
}
