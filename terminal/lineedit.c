/*
 * Implementation of local line editing. Used during username and
 * password input at login time, and also by ldisc during the main
 * session (if the session's virtual terminal is in that mode).
 *
 * Because we're tied into a real GUI terminal (and not a completely
 * standalone line-discipline module that deals purely with byte
 * streams), we can support a slightly richer input interface than
 * plain bytes.
 *
 * In particular, the 'dedicated' flag sent along with every byte is
 * used to distinguish control codes input via Ctrl+letter from the
 * same code input by a dedicated key like Return or Backspace. This
 * allows us to interpret the Ctrl+letter key combination as inputting
 * a literal control character to go into the line buffer, and the
 * dedicated-key version as performing an editing function.
 */

#include "putty.h"
#include "terminal.h"

typedef struct BufChar BufChar;

struct TermLineEditor {
    Terminal *term;
    BufChar *head, *tail;
    unsigned flags;
    bool quote_next_char;
    TermLineEditorCallbackReceiver *receiver;
};

struct BufChar {
    BufChar *prev, *next;

    /* The bytes of the character, to be sent on the wire */
    char wire[6];
    uint8_t nwire;

    /* Whether this character is considered complete */
    bool complete;

    /* Width of the character when it was displayed, in terminal cells */
    uint8_t width;

    /* Whether this character counts as whitespace, for ^W purposes */
    bool space;
};

TermLineEditor *lineedit_new(Terminal *term, unsigned flags,
                             TermLineEditorCallbackReceiver *receiver)
{
    TermLineEditor *le = snew(TermLineEditor);
    le->term = term;
    le->head = le->tail = NULL;
    le->flags = flags;
    le->quote_next_char = false;
    le->receiver = receiver;
    return le;
}

static void bufchar_free(BufChar *bc)
{
    smemclr(bc, sizeof(*bc));
    sfree(bc);
}

static void lineedit_free_buffer(TermLineEditor *le)
{
    while (le->head) {
        BufChar *bc = le->head;
        le->head = bc->next;
        bufchar_free(bc);
    }
    le->tail = NULL;
}

void lineedit_free(TermLineEditor *le)
{
    lineedit_free_buffer(le);
    sfree(le);
}

void lineedit_modify_flags(TermLineEditor *le, unsigned clr, unsigned flip)
{
    le->flags &= ~clr;
    le->flags ^= flip;
}

static void lineedit_term_write(TermLineEditor *le, ptrlen data)
{
    le->receiver->vt->to_terminal(le->receiver, data);
}

static void lineedit_term_newline(TermLineEditor *le)
{
    lineedit_term_write(le, PTRLEN_LITERAL("\x0D\x0A"));
}

static inline void lineedit_send_data(TermLineEditor *le, ptrlen data)
{
    le->receiver->vt->to_backend(le->receiver, data);
}

static inline void lineedit_special(TermLineEditor *le,
                                    SessionSpecialCode code, int arg)
{
    le->receiver->vt->special(le->receiver, code, arg);
}

static inline void lineedit_send_newline(TermLineEditor *le)
{
    le->receiver->vt->newline(le->receiver);
}

static void lineedit_delete_char(TermLineEditor *le)
{
    if (le->tail) {
        BufChar *bc = le->tail;
        le->tail = bc->prev;
        if (!le->tail)
            le->head = NULL;
        else
            le->tail->next = NULL;

        for (unsigned i = 0; i < bc->width; i++)
            lineedit_term_write(le, PTRLEN_LITERAL("\x08 \x08"));

        bufchar_free(bc);
    }
}

static void lineedit_delete_word(TermLineEditor *le)
{
    /*
     * Deleting a word stops at the _start_ of a word, i.e. at any
     * boundary with a space on the left and a non-space on the right.
     */
    if (!le->tail)
        return;

    while (true) {
        bool deleted_char_is_space = le->tail->space;
        lineedit_delete_char(le);
        if (!le->tail)
            break;         /* we've cleared the whole line */
        if (le->tail->space && !deleted_char_is_space)
            break;         /* we've just reached a word boundary */
    }
}

static void lineedit_delete_line(TermLineEditor *le)
{
    while (le->tail)
        lineedit_delete_char(le);
    lineedit_special(le, SS_EL, 0);
}

void lineedit_send_line(TermLineEditor *le)
{
    for (BufChar *bc = le->head; bc; bc = bc->next)
        lineedit_send_data(le, make_ptrlen(bc->wire, bc->nwire));
    lineedit_free_buffer(le);
    le->quote_next_char = false;
}

static void lineedit_complete_line(TermLineEditor *le)
{
    lineedit_term_newline(le); 
    lineedit_send_line(le);
    lineedit_send_newline(le);
}

/*
 * Send data to the terminal to display a BufChar. As a side effect,
 * update bc->width to indicate how many character cells we think were
 * taken up by what we just wrote. No other change to bc is made.
 */
static void lineedit_display_bufchar(TermLineEditor *le, BufChar *bc,
                                     unsigned chr)
{
    char buf[6];
    buffer_sink bs[1];
    buffer_sink_init(bs, buf, lenof(buf));

    /* Handle single-byte character set translation. */
    if (!in_utf(le->term) && DIRECT_CHAR(chr)) {
        /*
         * If we're not in UTF-8, i.e. we're in a single-byte
         * character set, then first we must feed the input byte
         * through term_translate, which will tell us whether it's a
         * control character or not. (That varies with the charset:
         * e.g. ISO 8859-1 and Win1252 disagree on a lot of
         * 0x80-0x9F).
         *
         * In principle, we could pass NULL as our term_utf8_decode
         * pointer, on the grounds that since the terminal isn't in
         * UTF-8 mode term_translate shouldn't access it. But that
         * seems needlessly reckless; we'll make up an empty one.
         */
        term_utf8_decode dummy_utf8 = { .state = 0, .chr = 0, .size = 0 };
        chr = term_translate(
            le->term, &dummy_utf8, (unsigned char)chr);

        /*
         * After that, chr will be either a control-character value
         * (00-1F, 7F, 80-9F), or a byte value ORed with one of the
         * CSET_FOO character set indicators. The latter indicates
         * that it's a printing character in this charset, in which
         * case it takes up one character cell.
         */
        if (chr & CSET_MASK) {
            put_byte(bs, chr);
            bc->width = 1;
            goto got_char;
        }
    }

    /*
     * If we got here without taking the 'goto' above, then we're now
     * holding an actual Unicode character.
     */
    assert(!IS_SURROGATE(chr)); /* and it should be an _actual_ one */

    /*
     * Deal with symbolic representations of control characters.
     */

    if (chr < 0x20 || chr == 0x7F) {
        /*
         * Represent C0 controls as '^C' or similar, and 7F as ^?.
         */
        put_byte(bs, '^');
        put_byte(bs, chr ^ 0x40);
        bc->width = 2;
        goto got_char;
    }

    if (chr >= 0x80 && chr < 0xA0) {
        /*
         * Represent C1 controls as <9B> or similar.
         */
        put_fmt(bs, "<%02X>", chr);
        bc->width = 4;
        goto got_char;
    }

    /*
     * And if we get _here_, we're holding a printing (or at least not
     * _control_, even if zero-width) Unicode character, which _must_
     * mean that the terminal is currently in UTF-8 mode (since if it
     * were not then printing characters would have gone through the
     * term_translate case above). So we can just write the UTF-8 for
     * the character - but we must also pay attention to its width in
     * character cells, which might be 0, 1 or 2.
     */
    assert(in_utf(le->term));
    put_utf8_char(bs, chr);
    bc->width = term_char_width(le->term, chr);

  got_char:
    lineedit_term_write(le, make_ptrlen(buf, bs->out - buf));
}

/* Called when we've just added a byte to a UTF-8 character and want
 * to see if it's complete */
static void lineedit_check_utf8_complete(TermLineEditor *le, BufChar *bc)
{
    BinarySource src[1];
    BinarySource_BARE_INIT(src, bc->wire, bc->nwire);
    DecodeUTF8Failure err;
    unsigned chr = decode_utf8(src, &err);
    if (err == DUTF8_E_OUT_OF_DATA)
        return;                        /* not complete yet */

    /* Any other error code is regarded as complete, and we just
     * display the character as the U+FFFD that decode_utf8 will have
     * returned anyway */
    bc->complete = true;
    bc->space = (chr == ' ');
    lineedit_display_bufchar(le, bc, chr);
}

static void lineedit_input_printing_char(TermLineEditor *le, char ch);

static void lineedit_redraw_line(TermLineEditor *le)
{
    /* FIXME: I'm not 100% sure this is the behaviour I really want in
     * this situation, but it's at least very simple to implement */
    BufChar *prevhead = le->head;
    le->head = le->tail = NULL;
    while (prevhead) {
        BufChar *bc = prevhead;
        prevhead = prevhead->next;

        for (unsigned i = 0; i < bc->nwire; i++)
            lineedit_input_printing_char(le, bc->wire[i]);
        bufchar_free(bc);
    }
}

#define CTRL(c) ((char) (0x40 ^ (unsigned char)c))

void lineedit_input(TermLineEditor *le, char ch, bool dedicated)
{
    if (le->quote_next_char) {
        /*
         * If the previous keypress was ^V, 'quoting' the next
         * character to be treated literally, then skip all the
         * editing-control processing, and clear that flag.
         */
        le->quote_next_char = false;
    } else {
        /*
         * Input events that are only valid with the 'dedicated' flag.
         * These are limited to the control codes that _have_
         * dedicated keys.
         *
         * Any case we actually handle here ends with a 'return'
         * statement, so that if we fall out of the end of this switch
         * at all, it's because the byte hasn't been handled here and
         * will fall into the next switch dealing with ordinary input.
         */
        if (dedicated) {
            switch (ch) {
                /*
                 * The Backspace key.
                 *
                 * Since our terminal can be configured to send either
                 * ^H or 7F (aka ^?) via the backspace key, we accept
                 * both.
                 *
                 * (We could query the Terminal's configuration here
                 * and accept only the one of those codes that the
                 * terminal is currently set to. But it's pointless,
                 * because whichever one the terminal isn't set to,
                 * the front end won't be sending it with
                 * dedicated=true anyway.)
                 */
              case CTRL('H'):
              case 0x7F:
                lineedit_delete_char(le);
                return;

                /*
                 * The Return key.
                 */
              case CTRL('M'):
                lineedit_complete_line(le);
                return;
            }
        }

        /*
         * Editing and special functions in response to ordinary keys
         * or Ctrl+key combinations. Many editing functions have to be
         * supported in this mode, like ^W and ^U, because there are
         * no dedicated keys that generate the same control codes
         * anyway.
         *
         * Again, we return if the key was handled. The final
         * processing of ordinary data to go into the input buffer
         * happens if we break from this switch.
         */
        switch (ch) {
          case CTRL('W'):
            lineedit_delete_word(le);
            return;

          case CTRL('U'):
            lineedit_delete_line(le);
            return;

          case CTRL('['):
            if (!(le->flags & LE_ESC_ERASES))
                break;             /* treat as normal input */
            lineedit_delete_line(le);
            return;

          case CTRL('R'):
            lineedit_term_write(le, PTRLEN_LITERAL("^R"));
            lineedit_term_newline(le);
            lineedit_redraw_line(le);
            return;

          case CTRL('V'):
            le->quote_next_char = true;
            return;

          case CTRL('C'):
            lineedit_delete_line(le);
            if (!(le->flags & LE_INTERRUPT))
                break;                 /* treat as normal input */
            lineedit_special(le, SS_IP, 0);
            return;

          case CTRL('Z'):
            lineedit_delete_line(le);
            if (!(le->flags & LE_SUSPEND))
                break;                 /* treat as normal input */
            lineedit_special(le, SS_SUSP, 0);
            return;

          case CTRL('\\'):
            lineedit_delete_line(le);
            if (!(le->flags & LE_ABORT))
                break;                 /* treat as normal input */
            lineedit_special(le, SS_ABORT, 0);
            return;

          case CTRL('D'):
            if (le->flags & LE_EOF_ALWAYS) {
                /* Our client wants to treat ^D / EOF as a special
                 * character in their own way. Just send an EOF
                 * special. */
                lineedit_special(le, SS_EOF, 0);
                return;
            }

            /*
             * Otherwise, ^D has the same behaviour as in Unix tty
             * line editing: if the edit buffer is non-empty then it's
             * sent immediately without a newline, and if it is empty
             * then an EOF is sent.
             */
            if (le->head) {
                lineedit_send_line(le);
                return;
            }

            lineedit_special(le, SS_EOF, 0);
            return;

          case CTRL('J'):
            if (le->flags & LE_CRLF_NEWLINE) {
                /*
                 * If the previous character in the buffer is a
                 * literal Ctrl-M, and now the user sends Ctrl-J, then
                 * we amalgamate both into a newline event.
                 */
                if (le->tail && le->tail->nwire == 1 && 
                    le->tail->wire[0] == CTRL('M')) {
                    lineedit_delete_char(le); /* erase ^J from buffer */
                    lineedit_complete_line(le);
                    return;
                }
            }
        }
    }

    /*
     * If we get to here, we've exhausted the options for treating our
     * character as an editing or special function of any kind. Treat
     * it as a printing character, or part of one.
     */
    lineedit_input_printing_char(le, ch);
}

static void lineedit_input_printing_char(TermLineEditor *le, char ch)
{
    /*
     * Append ch to the line buffer, either as a new BufChar or by
     * adding it to a previous one containing an as yet incomplete
     * UTF-8 encoding.
     */
    if (le->tail && !le->tail->complete) {
        BufChar *bc = le->tail;

        /*
         * If we're in UTF-8 mode, and ch is a UTF-8 continuation
         * byte, then we can append it to bc, which we've just checked
         * is missing at least one of those.
         */
        if (in_utf(le->term) && (unsigned char)ch - 0x80U < 0x40) {
            assert(bc->nwire < lenof(bc->wire));
            bc->wire[bc->nwire++] = ch;
            lineedit_check_utf8_complete(le, bc);
            return;
        }

        /*
         * Otherwise, the previous incomplete character can't be
         * extended. Mark it as complete, and if possible, display it
         * as a replacement character indicating that something weird
         * happened.
         */
        bc->complete = true;
        if (in_utf(le->term))
            lineedit_display_bufchar(le, bc, 0xFFFD);

        /*
         * But we still haven't processed the byte we're holding. Fall
         * through to the next step, where we make a fresh BufChar for
         * it.
         */
    }

    /*
     * Make a fresh BufChar.
     */
    BufChar *bc = snew(BufChar);
    bc->prev = le->tail;
    le->tail = bc;
    if (bc->prev)
        bc->prev->next = bc;
    else
        le->head = bc;
    bc->next = NULL;
    bc->complete = false;
    bc->space = false;

    bc->nwire = 1;
    bc->wire[0] = ch;
    if (in_utf(le->term)) {
        lineedit_check_utf8_complete(le, bc);
    } else {
        bc->complete = true;    /* always, in a single-byte charset */
        bc->space = (bc->wire[0] == ' ');
        lineedit_display_bufchar(le, bc, CSET_ASCII | (unsigned char)ch);
    }
}
