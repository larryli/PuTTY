/*
 * Supporting routines used in common by all the various components of
 * the SSH system.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "mpint.h"
#include "ssh.h"
#include "storage.h"
#include "bpp.h"
#include "ppl.h"
#include "channel.h"

/* ----------------------------------------------------------------------
 * Implementation of PacketQueue.
 */

static void pq_ensure_unlinked(PacketQueueNode *node)
{
    if (node->on_free_queue) {
        node->next->prev = node->prev;
        node->prev->next = node->next;
    } else {
        assert(!node->next);
        assert(!node->prev);
    }
}

void pq_base_push(PacketQueueBase *pqb, PacketQueueNode *node)
{
    pq_ensure_unlinked(node);
    node->next = &pqb->end;
    node->prev = pqb->end.prev;
    node->next->prev = node;
    node->prev->next = node;
    pqb->total_size += node->formal_size;

    if (pqb->ic)
        queue_idempotent_callback(pqb->ic);
}

void pq_base_push_front(PacketQueueBase *pqb, PacketQueueNode *node)
{
    pq_ensure_unlinked(node);
    node->prev = &pqb->end;
    node->next = pqb->end.next;
    node->next->prev = node;
    node->prev->next = node;
    pqb->total_size += node->formal_size;

    if (pqb->ic)
        queue_idempotent_callback(pqb->ic);
}

static PacketQueueNode pktin_freeq_head = {
    &pktin_freeq_head, &pktin_freeq_head, true
};

static void pktin_free_queue_callback(void *vctx)
{
    while (pktin_freeq_head.next != &pktin_freeq_head) {
        PacketQueueNode *node = pktin_freeq_head.next;
        PktIn *pktin = container_of(node, PktIn, qnode);
        pktin_freeq_head.next = node->next;
        sfree(pktin);
    }

    pktin_freeq_head.prev = &pktin_freeq_head;
}

static IdempotentCallback ic_pktin_free = {
    pktin_free_queue_callback, NULL, false
};

static inline void pq_unlink_common(PacketQueueBase *pqb,
                                    PacketQueueNode *node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;

    /* Check total_size doesn't drift out of sync downwards, by
     * ensuring it doesn't underflow when we do this subtraction */
    assert(pqb->total_size >= node->formal_size);
    pqb->total_size -= node->formal_size;

    /* Check total_size doesn't drift out of sync upwards, by checking
     * that it's returned to exactly zero whenever a queue is
     * emptied */
    assert(pqb->end.next != &pqb->end || pqb->total_size == 0);
}

static PktIn *pq_in_after(PacketQueueBase *pqb,
                          PacketQueueNode *prev, bool pop)
{
    PacketQueueNode *node = prev->next;
    if (node == &pqb->end)
        return NULL;

    if (pop) {
        pq_unlink_common(pqb, node);

        node->prev = pktin_freeq_head.prev;
        node->next = &pktin_freeq_head;
        node->next->prev = node;
        node->prev->next = node;
        node->on_free_queue = true;

        queue_idempotent_callback(&ic_pktin_free);
    }

    return container_of(node, PktIn, qnode);
}

static PktOut *pq_out_after(PacketQueueBase *pqb,
                            PacketQueueNode *prev, bool pop)
{
    PacketQueueNode *node = prev->next;
    if (node == &pqb->end)
        return NULL;

    if (pop) {
        pq_unlink_common(pqb, node);

        node->prev = node->next = NULL;
    }

    return container_of(node, PktOut, qnode);
}

void pq_in_init(PktInQueue *pq)
{
    pq->pqb.ic = NULL;
    pq->pqb.end.next = pq->pqb.end.prev = &pq->pqb.end;
    pq->after = pq_in_after;
    pq->pqb.total_size = 0;
}

void pq_out_init(PktOutQueue *pq)
{
    pq->pqb.ic = NULL;
    pq->pqb.end.next = pq->pqb.end.prev = &pq->pqb.end;
    pq->after = pq_out_after;
    pq->pqb.total_size = 0;
}

void pq_in_clear(PktInQueue *pq)
{
    PktIn *pkt;
    pq->pqb.ic = NULL;
    while ((pkt = pq_pop(pq)) != NULL) {
        /* No need to actually free these packets: pq_pop on a
         * PktInQueue will automatically move them to the free
         * queue. */
    }
}

void pq_out_clear(PktOutQueue *pq)
{
    PktOut *pkt;
    pq->pqb.ic = NULL;
    while ((pkt = pq_pop(pq)) != NULL)
        ssh_free_pktout(pkt);
}

/*
 * Concatenate the contents of the two queues q1 and q2, and leave the
 * result in qdest. qdest must be either empty, or one of the input
 * queues.
 */
void pq_base_concatenate(PacketQueueBase *qdest,
                         PacketQueueBase *q1, PacketQueueBase *q2)
{
    struct PacketQueueNode *head1, *tail1, *head2, *tail2;

    size_t total_size = q1->total_size + q2->total_size;

    /*
     * Extract the contents from both input queues, and empty them.
     */

    head1 = (q1->end.next == &q1->end ? NULL : q1->end.next);
    tail1 = (q1->end.prev == &q1->end ? NULL : q1->end.prev);
    head2 = (q2->end.next == &q2->end ? NULL : q2->end.next);
    tail2 = (q2->end.prev == &q2->end ? NULL : q2->end.prev);

    q1->end.next = q1->end.prev = &q1->end;
    q2->end.next = q2->end.prev = &q2->end;
    q1->total_size = q2->total_size = 0;

    /*
     * Link the two lists together, handling the case where one or
     * both is empty.
     */

    if (tail1)
        tail1->next = head2;
    else
        head1 = head2;

    if (head2)
        head2->prev = tail1;
    else
        tail2 = tail1;

    /*
     * Check the destination queue is currently empty. (If it was one
     * of the input queues, then it will be, because we emptied both
     * of those just a moment ago.)
     */

    assert(qdest->end.next == &qdest->end);
    assert(qdest->end.prev == &qdest->end);

    /*
     * If our concatenated list has anything in it, then put it in
     * dest.
     */

    if (!head1) {
        assert(!tail2);
    } else {
        assert(tail2);
        qdest->end.next = head1;
        qdest->end.prev = tail2;
        head1->prev = &qdest->end;
        tail2->next = &qdest->end;

        if (qdest->ic)
            queue_idempotent_callback(qdest->ic);
    }

    qdest->total_size = total_size;
}

/* ----------------------------------------------------------------------
 * Low-level functions for the packet structures themselves.
 */

static void ssh_pkt_BinarySink_write(BinarySink *bs,
                                     const void *data, size_t len);
PktOut *ssh_new_packet(void)
{
    PktOut *pkt = snew(PktOut);

    BinarySink_INIT(pkt, ssh_pkt_BinarySink_write);
    pkt->data = NULL;
    pkt->length = 0;
    pkt->maxlen = 0;
    pkt->downstream_id = 0;
    pkt->additional_log_text = NULL;
    pkt->qnode.next = pkt->qnode.prev = NULL;
    pkt->qnode.on_free_queue = false;

    return pkt;
}

static void ssh_pkt_adddata(PktOut *pkt, const void *data, int len)
{
    sgrowarrayn_nm(pkt->data, pkt->maxlen, pkt->length, len);
    memcpy(pkt->data + pkt->length, data, len);
    pkt->length += len;
    pkt->qnode.formal_size = pkt->length;
}

static void ssh_pkt_BinarySink_write(BinarySink *bs,
                                     const void *data, size_t len)
{
    PktOut *pkt = BinarySink_DOWNCAST(bs, PktOut);
    ssh_pkt_adddata(pkt, data, len);
}

void ssh_free_pktout(PktOut *pkt)
{
    sfree(pkt->data);
    sfree(pkt);
}

/* ----------------------------------------------------------------------
 * Implement zombiechan_new() and its trivial vtable.
 */

static void zombiechan_free(Channel *chan);
static size_t zombiechan_send(
    Channel *chan, bool is_stderr, const void *, size_t);
static void zombiechan_set_input_wanted(Channel *chan, bool wanted);
static void zombiechan_do_nothing(Channel *chan);
static void zombiechan_open_failure(Channel *chan, const char *);
static bool zombiechan_want_close(Channel *chan, bool sent_eof, bool rcvd_eof);
static char *zombiechan_log_close_msg(Channel *chan) { return NULL; }

static const ChannelVtable zombiechan_channelvt = {
    .free = zombiechan_free,
    .open_confirmation = zombiechan_do_nothing,
    .open_failed = zombiechan_open_failure,
    .send = zombiechan_send,
    .send_eof = zombiechan_do_nothing,
    .set_input_wanted = zombiechan_set_input_wanted,
    .log_close_msg = zombiechan_log_close_msg,
    .want_close = zombiechan_want_close,
    .rcvd_exit_status = chan_no_exit_status,
    .rcvd_exit_signal = chan_no_exit_signal,
    .rcvd_exit_signal_numeric = chan_no_exit_signal_numeric,
    .run_shell = chan_no_run_shell,
    .run_command = chan_no_run_command,
    .run_subsystem = chan_no_run_subsystem,
    .enable_x11_forwarding = chan_no_enable_x11_forwarding,
    .enable_agent_forwarding = chan_no_enable_agent_forwarding,
    .allocate_pty = chan_no_allocate_pty,
    .set_env = chan_no_set_env,
    .send_break = chan_no_send_break,
    .send_signal = chan_no_send_signal,
    .change_window_size = chan_no_change_window_size,
    .request_response = chan_no_request_response,
};

Channel *zombiechan_new(void)
{
    Channel *chan = snew(Channel);
    chan->vt = &zombiechan_channelvt;
    chan->initial_fixed_window_size = 0;
    return chan;
}

static void zombiechan_free(Channel *chan)
{
    assert(chan->vt == &zombiechan_channelvt);
    sfree(chan);
}

static void zombiechan_do_nothing(Channel *chan)
{
    assert(chan->vt == &zombiechan_channelvt);
}

static void zombiechan_open_failure(Channel *chan, const char *errtext)
{
    assert(chan->vt == &zombiechan_channelvt);
}

static size_t zombiechan_send(Channel *chan, bool is_stderr,
                              const void *data, size_t length)
{
    assert(chan->vt == &zombiechan_channelvt);
    return 0;
}

static void zombiechan_set_input_wanted(Channel *chan, bool enable)
{
    assert(chan->vt == &zombiechan_channelvt);
}

static bool zombiechan_want_close(Channel *chan, bool sent_eof, bool rcvd_eof)
{
    return true;
}

/* ----------------------------------------------------------------------
 * Common routines for handling SSH tty modes.
 */

static unsigned real_ttymode_opcode(unsigned our_opcode, int ssh_version)
{
    switch (our_opcode) {
      case TTYMODE_ISPEED:
        return ssh_version == 1 ? TTYMODE_ISPEED_SSH1 : TTYMODE_ISPEED_SSH2;
      case TTYMODE_OSPEED:
        return ssh_version == 1 ? TTYMODE_OSPEED_SSH1 : TTYMODE_OSPEED_SSH2;
      default:
        return our_opcode;
    }
}

static unsigned our_ttymode_opcode(unsigned real_opcode, int ssh_version)
{
    if (ssh_version == 1) {
        switch (real_opcode) {
          case TTYMODE_ISPEED_SSH1:
            return TTYMODE_ISPEED;
          case TTYMODE_OSPEED_SSH1:
            return TTYMODE_OSPEED;
          default:
            return real_opcode;
        }
    } else {
        switch (real_opcode) {
          case TTYMODE_ISPEED_SSH2:
            return TTYMODE_ISPEED;
          case TTYMODE_OSPEED_SSH2:
            return TTYMODE_OSPEED;
          default:
            return real_opcode;
        }
    }
}

struct ssh_ttymodes get_ttymodes_from_conf(Seat *seat, Conf *conf)
{
    struct ssh_ttymodes modes;
    size_t i;

    static const struct mode_name_type {
        const char *mode;
        int opcode;
        enum { TYPE_CHAR, TYPE_BOOL } type;
    } modes_names_types[] = {
        #define TTYMODE_CHAR(name, val, index) { #name, val, TYPE_CHAR },
        #define TTYMODE_FLAG(name, val, field, mask) { #name, val, TYPE_BOOL },
        #include "ttymode-list.h"
        #undef TTYMODE_CHAR
        #undef TTYMODE_FLAG
    };

    memset(&modes, 0, sizeof(modes));

    for (i = 0; i < lenof(modes_names_types); i++) {
        const struct mode_name_type *mode = &modes_names_types[i];
        const char *sval = conf_get_str_str(conf, CONF_ttymodes, mode->mode);
        char *to_free = NULL;

        if (!sval)
            sval = "N";                /* just in case */

        /*
         * sval[0] can be
         *  - 'V', indicating that an explicit value follows it;
         *  - 'A', indicating that we should pass the value through from
         *    the local environment via get_ttymode; or
         *  - 'N', indicating that we should explicitly not send this
         *    mode.
         */
        if (sval[0] == 'A') {
            sval = to_free = seat_get_ttymode(seat, mode->mode);
        } else if (sval[0] == 'V') {
            sval++;                    /* skip the 'V' */
        } else {
            /* else 'N', or something from the future we don't understand */
            continue;
        }

        if (sval) {
            /*
             * Parse the string representation of the tty mode
             * into the integer value it will take on the wire.
             */
            unsigned ival = 0;

            switch (mode->type) {
              case TYPE_CHAR:
                if (*sval) {
                    char *next = NULL;
                    /* We know ctrlparse won't write to the string, so
                     * casting away const is ugly but allowable. */
                    ival = ctrlparse((char *)sval, &next);
                    if (!next)
                        ival = sval[0];
                } else {
                    ival = 255; /* special value meaning "don't set" */
                }
                break;
              case TYPE_BOOL:
                if (stricmp(sval, "yes") == 0 ||
                    stricmp(sval, "on") == 0 ||
                    stricmp(sval, "true") == 0 ||
                    stricmp(sval, "+") == 0)
                    ival = 1;      /* true */
                else if (stricmp(sval, "no") == 0 ||
                         stricmp(sval, "off") == 0 ||
                         stricmp(sval, "false") == 0 ||
                         stricmp(sval, "-") == 0)
                    ival = 0;      /* false */
                else
                    ival = (atoi(sval) != 0);
                break;
              default:
                unreachable("Bad mode->type");
            }

            modes.have_mode[mode->opcode] = true;
            modes.mode_val[mode->opcode] = ival;
        }

        sfree(to_free);
    }

    {
        unsigned ospeed, ispeed;

        /* Unpick the terminal-speed config string. */
        ospeed = ispeed = 38400;           /* last-resort defaults */
        sscanf(conf_get_str(conf, CONF_termspeed), "%u,%u", &ospeed, &ispeed);
        /* Currently we unconditionally set these */
        modes.have_mode[TTYMODE_ISPEED] = true;
        modes.mode_val[TTYMODE_ISPEED] = ispeed;
        modes.have_mode[TTYMODE_OSPEED] = true;
        modes.mode_val[TTYMODE_OSPEED] = ospeed;
    }

    return modes;
}

struct ssh_ttymodes read_ttymodes_from_packet(
    BinarySource *bs, int ssh_version)
{
    struct ssh_ttymodes modes;
    memset(&modes, 0, sizeof(modes));

    while (1) {
        unsigned real_opcode, our_opcode;

        real_opcode = get_byte(bs);
        if (real_opcode == TTYMODE_END_OF_LIST)
            break;
        if (real_opcode >= 160) {
            /*
             * RFC 4254 (and the SSH 1.5 spec): "Opcodes 160 to 255
             * are not yet defined, and cause parsing to stop (they
             * should only be used after any other data)."
             *
             * My interpretation of this is that if one of these
             * opcodes appears, it's not a parse _error_, but it is
             * something that we don't know how to parse even well
             * enough to step over it to find the next opcode, so we
             * stop parsing now and assume that the rest of the string
             * is composed entirely of things we don't understand and
             * (as usual for unsupported terminal modes) silently
             * ignore.
             */
            return modes;
        }

        our_opcode = our_ttymode_opcode(real_opcode, ssh_version);
        assert(our_opcode < TTYMODE_LIMIT);
        modes.have_mode[our_opcode] = true;

        if (ssh_version == 1 && real_opcode >= 1 && real_opcode <= 127)
            modes.mode_val[our_opcode] = get_byte(bs);
        else
            modes.mode_val[our_opcode] = get_uint32(bs);
    }

    return modes;
}

void write_ttymodes_to_packet(BinarySink *bs, int ssh_version,
                              struct ssh_ttymodes modes)
{
    unsigned i;

    for (i = 0; i < TTYMODE_LIMIT; i++) {
        if (modes.have_mode[i]) {
            unsigned val = modes.mode_val[i];
            unsigned opcode = real_ttymode_opcode(i, ssh_version);

            put_byte(bs, opcode);
            if (ssh_version == 1 && opcode >= 1 && opcode <= 127)
                put_byte(bs, val);
            else
                put_uint32(bs, val);
        }
    }

    put_byte(bs, TTYMODE_END_OF_LIST);
}

/* ----------------------------------------------------------------------
 * Routine for allocating a new channel ID, given a means of finding
 * the index field in a given channel structure.
 */

unsigned alloc_channel_id_general(tree234 *channels, size_t localid_offset)
{
    const unsigned CHANNEL_NUMBER_OFFSET = 256;
    search234_state ss;

    /*
     * First-fit allocation of channel numbers: we always pick the
     * lowest unused one.
     *
     * Every channel before that, and no channel after it, has an ID
     * exactly equal to its tree index plus CHANNEL_NUMBER_OFFSET. So
     * we can use the search234 system to identify the length of that
     * initial sequence, in a single log-time pass down the channels
     * tree.
     */
    search234_start(&ss, channels);
    while (ss.element) {
        unsigned localid = *(unsigned *)((char *)ss.element + localid_offset);
        if (localid == ss.index + CHANNEL_NUMBER_OFFSET)
            search234_step(&ss, +1);
        else
            search234_step(&ss, -1);
    }

    /*
     * Now ss.index gives exactly the number of channels in that
     * initial sequence. So adding CHANNEL_NUMBER_OFFSET to it must
     * give precisely the lowest unused channel number.
     */
    return ss.index + CHANNEL_NUMBER_OFFSET;
}

/* ----------------------------------------------------------------------
 * Functions for handling the comma-separated strings used to store
 * lists of protocol identifiers in SSH-2.
 */

void add_to_commasep_pl(strbuf *buf, ptrlen data)
{
    if (buf->len > 0)
        put_byte(buf, ',');
    put_datapl(buf, data);
}

void add_to_commasep(strbuf *buf, const char *data)
{
    add_to_commasep_pl(buf, ptrlen_from_asciz(data));
}

bool get_commasep_word(ptrlen *list, ptrlen *word)
{
    const char *comma;

    /*
     * Discard empty list elements, should there be any, because we
     * never want to return one as if it was a real string. (This
     * introduces a mild tolerance of badly formatted data in lists we
     * receive, but I think that's acceptable.)
     */
    while (list->len > 0 && *(const char *)list->ptr == ',') {
        list->ptr = (const char *)list->ptr + 1;
        list->len--;
    }

    if (!list->len)
        return false;

    comma = memchr(list->ptr, ',', list->len);
    if (!comma) {
        *word = *list;
        list->len = 0;
    } else {
        size_t wordlen = comma - (const char *)list->ptr;
        word->ptr = list->ptr;
        word->len = wordlen;
        list->ptr = (const char *)list->ptr + wordlen + 1;
        list->len -= wordlen + 1;
    }
    return true;
}

/* ----------------------------------------------------------------------
 * Functions for translating SSH packet type codes into their symbolic
 * string names.
 */

#define TRANSLATE_UNIVERSAL(y, name, value)      \
    if (type == value) return #name;
#define TRANSLATE_KEX(y, name, value, ctx) \
    if (type == value && pkt_kctx == ctx) return #name;
#define TRANSLATE_AUTH(y, name, value, ctx) \
    if (type == value && pkt_actx == ctx) return #name;

const char *ssh1_pkt_type(int type)
{
    SSH1_MESSAGE_TYPES(TRANSLATE_UNIVERSAL, y);
    return "unknown";
}
const char *ssh2_pkt_type(Pkt_KCtx pkt_kctx, Pkt_ACtx pkt_actx, int type)
{
    SSH2_MESSAGE_TYPES(TRANSLATE_UNIVERSAL, TRANSLATE_KEX, TRANSLATE_AUTH, y);
    return "unknown";
}

#undef TRANSLATE_UNIVERSAL
#undef TRANSLATE_KEX
#undef TRANSLATE_AUTH

/* ----------------------------------------------------------------------
 * Common helper function for clients and implementations of
 * PacketProtocolLayer.
 */

void ssh_ppl_replace(PacketProtocolLayer *old, PacketProtocolLayer *new)
{
    new->bpp = old->bpp;
    ssh_ppl_setup_queues(new, old->in_pq, old->out_pq);
    new->selfptr = old->selfptr;
    new->seat = old->seat;
    new->ssh = old->ssh;

    *new->selfptr = new;
    ssh_ppl_free(old);

    /* The new layer might need to be the first one that sends a
     * packet, so trigger a call to its main coroutine immediately. If
     * it doesn't need to go first, the worst that will do is return
     * straight away. */
    queue_idempotent_callback(&new->ic_process_queue);
}

void ssh_ppl_free(PacketProtocolLayer *ppl)
{
    delete_callbacks_for_context(ppl);
    ppl->vt->free(ppl);
}

static void ssh_ppl_ic_process_queue_callback(void *context)
{
    PacketProtocolLayer *ppl = (PacketProtocolLayer *)context;
    ssh_ppl_process_queue(ppl);
}

void ssh_ppl_setup_queues(PacketProtocolLayer *ppl,
                          PktInQueue *inq, PktOutQueue *outq)
{
    ppl->in_pq = inq;
    ppl->out_pq = outq;
    ppl->in_pq->pqb.ic = &ppl->ic_process_queue;
    ppl->ic_process_queue.fn = ssh_ppl_ic_process_queue_callback;
    ppl->ic_process_queue.ctx = ppl;

    /* If there's already something on the input queue, it will want
     * handling immediately. */
    if (pq_peek(ppl->in_pq))
        queue_idempotent_callback(&ppl->ic_process_queue);
}

void ssh_ppl_user_output_string_and_free(PacketProtocolLayer *ppl, char *text)
{
    /* Messages sent via this function are from the SSH layer, not
     * from the server-side process, so they always have the stderr
     * flag set. */
    seat_stderr_pl(ppl->seat, ptrlen_from_asciz(text));
    sfree(text);
}

size_t ssh_ppl_default_queued_data_size(PacketProtocolLayer *ppl)
{
    return ppl->out_pq->pqb.total_size;
}

static void ssh_ppl_prompts_callback(void *ctx)
{
    ssh_ppl_process_queue((PacketProtocolLayer *)ctx);
}

prompts_t *ssh_ppl_new_prompts(PacketProtocolLayer *ppl)
{
    prompts_t *p = new_prompts();
    p->callback = ssh_ppl_prompts_callback;
    p->callback_ctx = ppl;
    return p;
}

/* ----------------------------------------------------------------------
 * Common helper functions for clients and implementations of
 * BinaryPacketProtocol.
 */

static void ssh_bpp_input_raw_data_callback(void *context)
{
    BinaryPacketProtocol *bpp = (BinaryPacketProtocol *)context;
    Ssh *ssh = bpp->ssh;               /* in case bpp is about to get freed */
    ssh_bpp_handle_input(bpp);
    /* If we've now cleared enough backlog on the input connection, we
     * may need to unfreeze it. */
    ssh_conn_processed_data(ssh);
}

static void ssh_bpp_output_packet_callback(void *context)
{
    BinaryPacketProtocol *bpp = (BinaryPacketProtocol *)context;
    ssh_bpp_handle_output(bpp);
}

void ssh_bpp_common_setup(BinaryPacketProtocol *bpp)
{
    pq_in_init(&bpp->in_pq);
    pq_out_init(&bpp->out_pq);
    bpp->input_eof = false;
    bpp->ic_in_raw.fn = ssh_bpp_input_raw_data_callback;
    bpp->ic_in_raw.ctx = bpp;
    bpp->ic_out_pq.fn = ssh_bpp_output_packet_callback;
    bpp->ic_out_pq.ctx = bpp;
    bpp->out_pq.pqb.ic = &bpp->ic_out_pq;
}

void ssh_bpp_free(BinaryPacketProtocol *bpp)
{
    delete_callbacks_for_context(bpp);
    bpp->vt->free(bpp);
}

void ssh2_bpp_queue_disconnect(BinaryPacketProtocol *bpp,
                               const char *msg, int category)
{
    PktOut *pkt = ssh_bpp_new_pktout(bpp, SSH2_MSG_DISCONNECT);
    put_uint32(pkt, category);
    put_stringz(pkt, msg);
    put_stringz(pkt, "en");            /* language tag */
    pq_push(&bpp->out_pq, pkt);
}

#define BITMAP_UNIVERSAL(y, name, value)                        \
    | (value >= y && value < y+32                               \
       ? 1UL << (value >= y && value < y+32 ? (value-y) : 0)    \
       : 0)
#define BITMAP_CONDITIONAL(y, name, value, ctx) \
    BITMAP_UNIVERSAL(y, name, value)
#define SSH2_BITMAP_WORD(y) \
    (0 SSH2_MESSAGE_TYPES(BITMAP_UNIVERSAL, BITMAP_CONDITIONAL, \
                          BITMAP_CONDITIONAL, (32*y)))

bool ssh2_bpp_check_unimplemented(BinaryPacketProtocol *bpp, PktIn *pktin)
{
    static const unsigned valid_bitmap[] = {
        SSH2_BITMAP_WORD(0),
        SSH2_BITMAP_WORD(1),
        SSH2_BITMAP_WORD(2),
        SSH2_BITMAP_WORD(3),
        SSH2_BITMAP_WORD(4),
        SSH2_BITMAP_WORD(5),
        SSH2_BITMAP_WORD(6),
        SSH2_BITMAP_WORD(7),
    };

    if (pktin->type < 0x100 &&
        !((valid_bitmap[pktin->type >> 5] >> (pktin->type & 0x1F)) & 1)) {
        PktOut *pkt = ssh_bpp_new_pktout(bpp, SSH2_MSG_UNIMPLEMENTED);
        put_uint32(pkt, pktin->sequence);
        pq_push(&bpp->out_pq, pkt);
        return true;
    }

    return false;
}

#undef BITMAP_UNIVERSAL
#undef BITMAP_CONDITIONAL
#undef SSH2_BITMAP_WORD

/* ----------------------------------------------------------------------
 * Centralised component of SSH host key verification.
 *
 * verify_ssh_host_key is called from both the SSH-1 and SSH-2
 * transport layers, and does the initial work of checking whether the
 * host key is already known. If so, it returns success on its own
 * account; otherwise, it calls out to the Seat to give an interactive
 * prompt (the nature of which varies depending on the Seat itself).
 */

SeatPromptResult verify_ssh_host_key(
    InteractionReadySeat iseat, Conf *conf, const char *host, int port,
    ssh_key *key, const char *keytype, char *keystr, const char *keydisp,
    char **fingerprints, int ca_count,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    /*
     * First, check if the Conf includes a manual specification of the
     * expected host key. If so, that completely supersedes everything
     * else, including the normal host key cache _and_ including
     * manual overrides: we return success or failure immediately,
     * entirely based on whether the key matches the Conf.
     */
    if (conf_get_str_nthstrkey(conf, CONF_ssh_manual_hostkeys, 0)) {
        if (fingerprints) {
            for (size_t i = 0; i < SSH_N_FPTYPES; i++) {
                /*
                 * Each fingerprint string we've been given will have
                 * things like 'ssh-rsa 2048' at the front of it. Strip
                 * those off and narrow down to just the hash at the end
                 * of the string.
                 */
                const char *fingerprint = fingerprints[i];
                if (!fingerprint)
                    continue;
                const char *p = strrchr(fingerprint, ' ');
                fingerprint = p ? p+1 : fingerprint;
                if (conf_get_str_str_opt(conf, CONF_ssh_manual_hostkeys,
                                         fingerprint))
                    return SPR_OK;
            }
        }

        if (key) {
            /*
             * Construct the base64-encoded public key blob and see if
             * that's listed.
             */
            strbuf *binblob;
            char *base64blob;
            int atoms, i;
            binblob = strbuf_new();
            ssh_key_public_blob(key, BinarySink_UPCAST(binblob));
            atoms = (binblob->len + 2) / 3;
            base64blob = snewn(atoms * 4 + 1, char);
            for (i = 0; i < atoms; i++)
                base64_encode_atom(binblob->u + 3*i,
                                   binblob->len - 3*i, base64blob + 4*i);
            base64blob[atoms * 4] = '\0';
            strbuf_free(binblob);
            if (conf_get_str_str_opt(conf, CONF_ssh_manual_hostkeys,
                                     base64blob)) {
                sfree(base64blob);
                return SPR_OK;
            }
            sfree(base64blob);
        }

        return SPR_SW_ABORT("Host key not in manually configured list");
    }

    /*
     * Next, check the host key cache.
     */
    int storage_status = check_stored_host_key(host, port, keytype, keystr);
    if (storage_status == 0) /* matching key was found in the cache */
        return SPR_OK;

    /*
     * The key is either missing from the cache, or does not match.
     * Either way, fall back to an interactive prompt from the Seat.
     */
    SeatDialogText *text = seat_dialog_text_new();
    const SeatDialogPromptDescriptions *pds =
        seat_prompt_descriptions(iseat.seat);

    FingerprintType fptype_default =
        ssh2_pick_default_fingerprint(fingerprints);

    seat_dialog_text_append(
        text, SDT_TITLE, "%s 安全警告", appname);

    HelpCtx helpctx;

    if (key && ssh_key_alg(key)->is_certificate) {
        seat_dialog_text_append(
            text, SDT_SCARY_HEADING, "**警告** - 潜在安全隐患！");
        seat_dialog_text_append(
            text, SDT_PARA, "此服务器提供了经过认证的主机密钥:");
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s (端口 %d)", host, port);
        if (ca_count) {
            seat_dialog_text_append(
                text, SDT_PARA, "此服务器是由其他证书颁发机构签名认证，"
                "并未配置在 %s 可信证书颁发机构 (CA) 中。",
                appname);
            if (storage_status == 2) {
                seat_dialog_text_append(
                    text, SDT_PARA, "**此外**ALSO，此服务器密钥与 "
                    "%s 此前缓存的密钥不匹配。", appname);
                seat_dialog_text_append(
                    text, SDT_PARA, "这说明有其他证书颁发机构在此网络提供服务，"
                    "**并且*可能是该服务器管理员更新了主机密钥，"
                    "或者更可能是连接到了一台伪装成该服务器的其他计算机系统。");
            } else {
                seat_dialog_text_append(
                    text, SDT_PARA, "这说明有其他证书颁发机构在此网络提供服务，"
                    "或是连接到了一台伪装成该服务器的其他计算机系统。");
            }
        } else {
            assert(storage_status == 2);
            seat_dialog_text_append(
                text, SDT_PARA, "此服务器认证密钥与 "
                "%s 此前缓存的密钥不匹配。", appname);
            seat_dialog_text_append(
                text, SDT_PARA, "这说明可能该服务器管理员更新了主机密钥，"
                "或者更可能是连接到了一台伪装成该服务器的其他计算机系统。");
        }
        seat_dialog_text_append(
            text, SDT_PARA, "新的 %s 密钥指纹为:", keytype);
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s", fingerprints[fptype_default]);
        helpctx = HELPCTX(errors_cert_mismatch);
    } else if (storage_status == 1) {
        seat_dialog_text_append(
            text, SDT_PARA, "该服务器主机密钥未缓存:");
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s (端口 %d)", host, port);
        seat_dialog_text_append(
            text, SDT_PARA, "不能保证该服务器是能够正确访问的计算机。");
        seat_dialog_text_append(
            text, SDT_PARA, "该服务器的 %s 密钥指纹为:", keytype);
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s", fingerprints[fptype_default]);
        helpctx = HELPCTX(errors_hostkey_absent);
    } else {
        seat_dialog_text_append(
            text, SDT_SCARY_HEADING, "**警告** - 潜在安全隐患！");
        seat_dialog_text_append(
            text, SDT_PARA, "在 %s 缓存中不能匹配该服务器密钥:"
            , appname);
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s (端口 %d)", host, port);
        seat_dialog_text_append(
            text, SDT_PARA, "这说明可能该服务器管理员更新了主机密钥，"
            "或者更可能是连接到了一台伪装成该服务器的其他计算机系统。");
        seat_dialog_text_append(
            text, SDT_PARA, "新的 %s 密钥指纹为:", keytype);
        seat_dialog_text_append(
            text, SDT_DISPLAY, "%s", fingerprints[fptype_default]);
        helpctx = HELPCTX(errors_hostkey_changed);
    }

    /* The above text is printed even in batch mode. Here's where we stop if
     * we can't present interactive prompts. */
    seat_dialog_text_append(
        text, SDT_BATCH_ABORT, "连接已放弃。");

    if (storage_status == 1) {
        seat_dialog_text_append(
            text, SDT_PARA, "如果信任该主机，请%s增加密钥到"
            "%s 缓存中，并继续连接。",
            pds->hk_accept_action, appname);
        seat_dialog_text_append(
            text, SDT_PARA, "如果仅仅只希望进行本次连接，而不将密钥储存，"
            "请%s。",
            pds->hk_connect_once_action);
        seat_dialog_text_append(
            text, SDT_PARA, "如果不信任该主机，请%s放弃此连接。"
            , pds->hk_cancel_action);
        seat_dialog_text_append(
            text, SDT_PROMPT, "储存密钥到缓存？");
    } else {
        seat_dialog_text_append(
            text, SDT_PARA, "如果确信该密钥被更新并同意接受新的密钥，"
            "请%s更新 %s 缓存并继续连接。",
            pds->hk_accept_action, appname);
        if (key && ssh_key_alg(key)->is_certificate) {
            seat_dialog_text_append(
                text, SDT_PARA, "(储存此认证密钥到缓存中将不会"
                "导致其证书颁发机构信任任何其他密钥或主机。)");
        }
        seat_dialog_text_append(
            text, SDT_PARA, "如果仅仅只希望继续本次连接，而不更新系统缓存，"
            "请%s。", pds->hk_connect_once_action);
        seat_dialog_text_append(
            text, SDT_PARA, "如果希望完全放弃本次连接，"
            "请%s。%s是**唯一**可以保证的安全选择。",
            pds->hk_cancel_action, pds->hk_cancel_action_Participle);
        seat_dialog_text_append(
            text, SDT_PROMPT, "更新缓存的密钥？");
    }

    seat_dialog_text_append(text, SDT_MORE_INFO_KEY,
                            "主机公钥完整文本");
    seat_dialog_text_append(text, SDT_MORE_INFO_VALUE_BLOB, "%s", keydisp);

    if (fingerprints[SSH_FPTYPE_SHA256]) {
        seat_dialog_text_append(text, SDT_MORE_INFO_KEY, "SHA256 指纹");
        seat_dialog_text_append(text, SDT_MORE_INFO_VALUE_SHORT, "%s",
                                fingerprints[SSH_FPTYPE_SHA256]);
    }
    if (fingerprints[SSH_FPTYPE_MD5]) {
        seat_dialog_text_append(text, SDT_MORE_INFO_KEY, "MD5 指纹");
        seat_dialog_text_append(text, SDT_MORE_INFO_VALUE_SHORT, "%s",
                                fingerprints[SSH_FPTYPE_MD5]);
    }

    SeatPromptResult toret = seat_confirm_ssh_host_key(
        iseat, host, port, keytype, keystr, text, helpctx, callback, ctx);
    seat_dialog_text_free(text);
    return toret;
}

/* ----------------------------------------------------------------------
 * Common functions shared between SSH-1 layers.
 */

bool ssh1_common_get_specials(
    PacketProtocolLayer *ppl, add_special_fn_t add_special, void *ctx)
{
    /*
     * Don't bother offering IGNORE if we've decided the remote
     * won't cope with it, since we wouldn't bother sending it if
     * asked anyway.
     */
    if (!(ppl->remote_bugs & BUG_CHOKES_ON_SSH1_IGNORE)) {
        add_special(ctx, "IGNORE 消息", SS_NOP, 0);
        return true;
    }

    return false;
}

bool ssh1_common_filter_queue(PacketProtocolLayer *ppl)
{
    PktIn *pktin;
    ptrlen msg;

    while ((pktin = pq_peek(ppl->in_pq)) != NULL) {
        switch (pktin->type) {
          case SSH1_MSG_DISCONNECT:
            msg = get_string(pktin);
            ssh_remote_error(ppl->ssh,
                             "Remote side sent disconnect message:\n\"%.*s\"",
                             PTRLEN_PRINTF(msg));
            /* don't try to pop the queue, because we've been freed! */
            return true;               /* indicate that we've been freed */

          case SSH1_MSG_DEBUG:
            msg = get_string(pktin);
            ppl_logevent("Remote debug message: %.*s", PTRLEN_PRINTF(msg));
            pq_pop(ppl->in_pq);
            break;

          case SSH1_MSG_IGNORE:
            /* Do nothing, because we're ignoring it! Duhh. */
            pq_pop(ppl->in_pq);
            break;

          default:
            return false;
        }
    }

    return false;
}

void ssh1_compute_session_id(
    unsigned char *session_id, const unsigned char *cookie,
    RSAKey *hostkey, RSAKey *servkey)
{
    ssh_hash *hash = ssh_hash_new(&ssh_md5);

    for (size_t i = (mp_get_nbits(hostkey->modulus) + 7) / 8; i-- ;)
        put_byte(hash, mp_get_byte(hostkey->modulus, i));
    for (size_t i = (mp_get_nbits(servkey->modulus) + 7) / 8; i-- ;)
        put_byte(hash, mp_get_byte(servkey->modulus, i));
    put_data(hash, cookie, 8);
    ssh_hash_final(hash, session_id);
}

/* ----------------------------------------------------------------------
 * Wrapper function to handle the abort-connection modes of a
 * SeatPromptResult without a lot of verbiage at every call site.
 *
 * Can become ssh_sw_abort or ssh_user_close, depending on the kind of
 * negative SeatPromptResult.
 */
void ssh_spr_close(Ssh *ssh, SeatPromptResult spr, const char *context)
{
    if (spr.kind == SPRK_USER_ABORT) {
        ssh_user_close(ssh, "User aborted at %s", context);
    } else {
        assert(spr.kind == SPRK_SW_ABORT);
        char *err = spr_get_error_message(spr);
        ssh_sw_abort(ssh, "%s", err);
        sfree(err);
    }
}
