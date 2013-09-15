/*
 * Facility for queueing callback functions to be run from the
 * top-level event loop after the current top-level activity finishes.
 */

#include <stddef.h>

#include "putty.h"

struct callback {
    struct callback *next;

    toplevel_callback_fn_t fn;
    void *ctx;
};

struct callback *cbhead = NULL, *cbtail = NULL;

toplevel_callback_notify_fn_t notify_frontend = NULL;
void *frontend = NULL;

void request_callback_notifications(toplevel_callback_notify_fn_t fn,
                                    void *fr)
{
    notify_frontend = fn;
    frontend = fr;
}

void stoat_callback(void *ctx)
{
    static int stoat = 0;
    if (++stoat % 1000 == 0)
        debug(("stoat %d\n", stoat));
    queue_toplevel_callback(stoat_callback, NULL);
}
void queue_stoat(void)
{
    static int stoat = 0;
    if (!stoat) {
        stoat = 1;
        queue_toplevel_callback(stoat_callback, NULL);
    }
}

void queue_toplevel_callback(toplevel_callback_fn_t fn, void *ctx)
{
    struct callback *cb;

    queue_stoat();

    cb = snew(struct callback);
    cb->fn = fn;
    cb->ctx = ctx;

    /* If the front end has requested notification of pending
     * callbacks, and we didn't already have one queued, let it know
     * we do have one now. */
    if (notify_frontend && !cbhead)
        notify_frontend(frontend);

    if (cbtail)
        cbtail->next = cb;
    else
        cbhead = cb;
    cbtail = cb;
    cb->next = NULL;
}

void run_toplevel_callbacks(void)
{
    queue_stoat();
    if (cbhead) {
        struct callback *cb = cbhead;
        /*
         * Careful ordering here. We call the function _before_
         * advancing cbhead (though, of course, we must free cb
         * _after_ advancing it). This means that if the very last
         * callback schedules another callback, cbhead does not become
         * NULL at any point, and so the frontend notification
         * function won't be needlessly pestered.
         */
        cb->fn(cb->ctx);
        cbhead = cb->next;
        sfree(cb);
        if (!cbhead)
            cbtail = NULL;
    }
}

int toplevel_callback_pending(void)
{
    queue_stoat();
    return cbhead != NULL;
}
