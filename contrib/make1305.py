#!/usr/bin/env python

import sys

class Output(object):
    def __init__(self, bignum_int_bits):
        self.bignum_int_bits = bignum_int_bits
        self.text = ""
        self.vars = []
    def stmt(self, statement):
        self.text += "    %s;\n" % statement
    def register_var(self, var):
        self.vars.append(var)
    def finalise(self):
        for var in self.vars:
            assert var.maxval == 0, "Variable not clear: %s" % var.name
        return self.text

class Variable(object):
    def __init__(self, out, name):
        self.out = out
        self.maxval = 0
        self.name = name
        self.placeval = None
        self.out.stmt("BignumDblInt %s" % (self.name))
        self.out.register_var(self)
    def clear(self, placeval):
        self.maxval = 0
        self.placeval = placeval
        self.out.stmt("%s = 0" % (self.name))
    def set_word(self, name, limit=None):
        if limit is not None:
            self.maxval = limit-1
        else:
            self.maxval = (1 << self.out.bignum_int_bits) - 1
        assert self.maxval < (1 << 2*self.out.bignum_int_bits)
        self.out.stmt("%s = %s" % (self.name, name))
    def add_word(self, name, limit=None):
        if limit is not None:
            self.maxval += limit-1
        else:
            self.maxval += (1 << self.out.bignum_int_bits) - 1
        assert self.maxval < (1 << 2*self.out.bignum_int_bits)
        self.out.stmt("%s += %s" % (self.name, name))
    def add_input_word(self, fmt, wordpos, limit=None):
        assert self.placeval == wordpos * self.out.bignum_int_bits
        self.add_word(fmt % wordpos, limit)
    def set_to_product(self, a, b, placeval):
        self.maxval = ((1 << self.out.bignum_int_bits) - 1) ** 2
        assert self.maxval < (1 << 2*self.out.bignum_int_bits)        
        self.out.stmt("%s = (BignumDblInt)(%s) * (%s)" % (self.name, a, b))
        self.placeval = placeval
    def add_bottom_half(self, srcvar):
        self.add_word("%s & BIGNUM_INT_MASK" % (srcvar.name))
    def add_top_half(self, srcvar):
        self.add_word("%s >> %d" % (srcvar.name, self.out.bignum_int_bits))
    def unload_into(self, topvar, botvar):
        assert botvar.placeval == self.placeval
        botvar.add_bottom_half(self)
        assert topvar.placeval == self.placeval + self.out.bignum_int_bits
        topvar.add_top_half(self)
        self.maxval = 0
    def output_word(self, bitpos, bits, destfmt, destwordpos):
        assert bitpos == 0
        assert self.placeval == destwordpos * self.out.bignum_int_bits
        dest = destfmt % destwordpos
        if bits == self.out.bignum_int_bits:
            self.out.stmt("%s = %s" % (dest, self.name))
        else:
            self.out.stmt("%s = %s & (((BignumInt)1 << %d)-1)" %
                          (dest, self.name, bits))
    def transfer_to_next_acc(self, bitpos, bits, pow5, destvar):
        destbitpos = self.placeval + bitpos - 130 * pow5 - destvar.placeval
        #print "transfer", "*%d" % 5**pow5, self.name, self.placeval, bitpos, destvar.name, destvar.placeval, destbitpos, bits
        assert 0 <= bitpos < bitpos+bits <= self.out.bignum_int_bits
        assert 0 <= destbitpos < destbitpos+bits <= self.out.bignum_int_bits
        expr = self.name
        if bitpos > 0:
            expr = "(%s >> %d)" % (expr, bitpos)
        expr = "(%s & (((BignumInt)1 << %d)-1))" % (expr, bits)
        self.out.stmt("%s += %s * ((BignumDblInt)%d << %d)" %
                      (destvar.name, expr, 5**pow5, destbitpos))
        destvar.maxval += (((1 << bits)-1) << destbitpos) * (5**pow5)
    def shift_down_from(self, top):
        if top is not None:
            self.out.stmt("%s = %s + (%s >> %d)" %
                          (self.name, top.name, self.name,
                           self.out.bignum_int_bits))
            topmaxval = top.maxval
        else:
            self.out.stmt("%s >>= %d" % (self.name, self.out.bignum_int_bits))
            topmaxval = 0
        self.maxval = topmaxval + self.maxval >> self.out.bignum_int_bits
        assert self.maxval < (1 << 2*self.out.bignum_int_bits)
        if top is not None:
            assert self.placeval + self.out.bignum_int_bits == top.placeval
            top.clear(top.placeval + self.out.bignum_int_bits)
        self.placeval += self.out.bignum_int_bits

def gen_add(bignum_int_bits):
    out = Output(bignum_int_bits)

    inbits = 130
    inwords = (inbits + bignum_int_bits - 1) / bignum_int_bits

    # This is an addition _without_ reduction mod p, so that it can be
    # used both during accumulation of the polynomial and for adding
    # on the encrypted nonce at the end (which is mod 2^128, not mod
    # p).
    #
    # Because one of the inputs will have come from our
    # not-completely-reducing multiplication function, we expect up to
    # 3 extra bits of input.
    acclo = Variable(out, "acclo")

    acclo.clear(0)

    for wordpos in range(inwords):
        limit = min(1 << bignum_int_bits, 1 << (130 - wordpos*bignum_int_bits))
        acclo.add_input_word("a->w[%d]", wordpos, limit)
        acclo.add_input_word("b->w[%d]", wordpos, limit)
        acclo.output_word(0, bignum_int_bits, "r->w[%d]", wordpos)
        acclo.shift_down_from(None)

    return out.finalise()

def gen_mul_1305(bignum_int_bits):
    out = Output(bignum_int_bits)

    inbits = 130
    inwords = (inbits + bignum_int_bits - 1) / bignum_int_bits

    # The inputs are not 100% reduced mod p. Specifically, we can get
    # a full 130-bit number from the pow5==0 pass, and then a 130-bit
    # number times 5 from the pow5==1 pass, plus a possible carry. The
    # total of that can be easily bounded above by 2^130 * 8, so we
    # need to assume we're multiplying two 133-bit numbers.
    outbits = (inbits + 3) * 2
    outwords = (outbits + bignum_int_bits - 1) / bignum_int_bits + 1

    tmp = Variable(out, "tmp")
    acclo = Variable(out, "acclo")
    acchi = Variable(out, "acchi")
    acc2lo = Variable(out, "acc2lo")

    pow5, bits_at_pow5 = 0, inbits

    acclo.clear(0)
    acchi.clear(bignum_int_bits)
    bits_needed_in_acc2 = bignum_int_bits

    for outwordpos in range(outwords):
        for a in range(inwords):
            b = outwordpos - a
            if 0 <= b < inwords:
                tmp.set_to_product("a->w[%d]" % a, "b->w[%d]" % b,
                                   outwordpos * bignum_int_bits)
                tmp.unload_into(acchi, acclo)

        bits_in_word = bignum_int_bits
        bitpos = 0
        #print "begin output"
        while bits_in_word > 0:
            chunk = min(bits_in_word, bits_at_pow5)
            if pow5 > 0:
                chunk = min(chunk, bits_needed_in_acc2)
            if pow5 == 0:
                acclo.output_word(bitpos, chunk, "r->w[%d]", outwordpos)
            else:
                acclo.transfer_to_next_acc(bitpos, chunk, pow5, acc2lo)
                bits_needed_in_acc2 -= chunk
                if bits_needed_in_acc2 == 0:
                    assert acc2lo.placeval % bignum_int_bits == 0
                    other_outwordpos = acc2lo.placeval / bignum_int_bits
                    acc2lo.add_input_word("r->w[%d]", other_outwordpos)
                    acc2lo.output_word(bitpos, bignum_int_bits, "r->w[%d]",
                                       other_outwordpos)
                    acc2lo.shift_down_from(None)
                    bits_needed_in_acc2 = bignum_int_bits
            bits_in_word -= chunk
            bits_at_pow5 -= chunk
            bitpos += chunk
            if bits_at_pow5 == 0:
                if pow5 > 0:
                    assert acc2lo.placeval % bignum_int_bits == 0
                    other_outwordpos = acc2lo.placeval / bignum_int_bits
                    acc2lo.add_input_word("r->w[%d]", other_outwordpos)
                    acc2lo.output_word(0, bignum_int_bits, "r->w[%d]",
                                       other_outwordpos)
                pow5 += 1
                bits_at_pow5 = inbits
                acc2lo.clear(0)
                bits_needed_in_acc2 = bignum_int_bits
        acclo.shift_down_from(acchi)

    while acc2lo.maxval > 0:
        other_outwordpos = acc2lo.placeval / bignum_int_bits
        bitsleft = inbits - other_outwordpos * bignum_int_bits
        limit = 1<<bitsleft if bitsleft < bignum_int_bits else None
        acc2lo.add_input_word("r->w[%d]", other_outwordpos, limit=limit)
        acc2lo.output_word(0, bignum_int_bits, "r->w[%d]", other_outwordpos)
        acc2lo.shift_down_from(None)

    return out.finalise()

def gen_final_reduce_1305(bignum_int_bits):
    out = Output(bignum_int_bits)

    inbits = 130
    inwords = (inbits + bignum_int_bits - 1) / bignum_int_bits

    # We take our input number n, and compute k = 5 + 5*(n >> 130).
    # Then k >> 130 is precisely the multiple of p that needs to be
    # subtracted from n to reduce it to strictly less than p.

    acclo = Variable(out, "acclo")

    acclo.clear(0)
    # Hopefully all the bits we're shifting down fit in the same word.
    assert 130 / bignum_int_bits == (130 + 3 - 1) / bignum_int_bits
    acclo.add_word("5 * ((n->w[%d] >> %d) + 1)" %
                   (130 / bignum_int_bits, 130 % bignum_int_bits),
                   limit = 5 * (7 + 1))
    for wordpos in range(inwords):
        acclo.add_input_word("n->w[%d]", wordpos)
        # Notionally, we could call acclo.output_word here to store
        # our adjusted value k. But we don't need to, because all we
        # actually want is the very top word of it.
        if wordpos == 130 / bignum_int_bits:
            break
        acclo.shift_down_from(None)

    # Now we can find the right multiple of p to subtract. We actually
    # subtract it by adding 5 times it, and then finally discarding
    # the top bits of the output.

    # Hopefully all the bits we're shifting down fit in the same word.
    assert 130 / bignum_int_bits == (130 + 3 - 1) / bignum_int_bits
    acclo.set_word("5 * (acclo >> %d)" % (130 % bignum_int_bits),
                   limit = 5 * (7 + 1))
    acclo.placeval = 0
    for wordpos in range(inwords):
        acclo.add_input_word("n->w[%d]", wordpos)
        acclo.output_word(0, bignum_int_bits, "n->w[%d]", wordpos)
        acclo.shift_down_from(None)

    out.stmt("n->w[%d] &= (1 << %d) - 1" %
             (130 / bignum_int_bits, 130 % bignum_int_bits))

    # Here we don't call out.finalise(), because that will complain
    # that there are bits of output we never dealt with. This is true,
    # but all the bits in question are above 2^130, so they're bits
    # we're discarding anyway.
    return out.text # not out.finalise()

ops = { "mul" : gen_mul_1305,
        "add" : gen_add,
        "final_reduce" : gen_final_reduce_1305 }

args = sys.argv[1:]
if len(args) != 2 or args[0] not in ops:
    sys.stderr.write("usage: make1305.py (%s) <bits>\n" % (" | ".join(sorted(ops))))
    sys.exit(1)

sys.stdout.write("    /* ./contrib/make1305.py %s %s */\n" % tuple(args))
s = ops[args[0]](int(args[1]))
sys.stdout.write(s)
