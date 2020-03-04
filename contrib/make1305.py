#!/usr/bin/env python3

import sys
import string
from collections import namedtuple

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

class Multiprecision(object):
    def __init__(self, target, minval, maxval, words):
        self.target = target
        self.minval = minval
        self.maxval = maxval
        self.words = words
        assert 0 <= self.minval
        assert self.minval <= self.maxval
        assert self.target.nwords(self.maxval) == len(words)

    def getword(self, n):
        return self.words[n] if n < len(self.words) else "0"

    def __add__(self, rhs):
        newmin = self.minval + rhs.minval
        newmax = self.maxval + rhs.maxval
        nwords = self.target.nwords(newmax)
        words = []

        addfn = self.target.add
        for i in range(nwords):
            words.append(addfn(self.getword(i), rhs.getword(i)))
            addfn = self.target.adc

        return Multiprecision(self.target, newmin, newmax, words)

    def __mul__(self, rhs):
        newmin = self.minval * rhs.minval
        newmax = self.maxval * rhs.maxval
        nwords = self.target.nwords(newmax)
        words = []

        # There are basically two strategies we could take for
        # multiplying two multiprecision integers. One is to enumerate
        # the space of pairs of word indices in lexicographic order,
        # essentially computing a*b[i] for each i and adding them
        # together; the other is to enumerate in diagonal order,
        # computing everything together that belongs at a particular
        # output word index.
        #
        # For the moment, I've gone for the former.

        sprev = []
        for i, sword in enumerate(self.words):
            rprev = None
            sthis = sprev[:i]
            for j, rword in enumerate(rhs.words):
                prevwords = []
                if i+j < len(sprev):
                    prevwords.append(sprev[i+j])
                if rprev is not None:
                    prevwords.append(rprev)
                vhi, vlo = self.target.muladd(sword, rword, *prevwords)
                sthis.append(vlo)
                rprev = vhi
            sthis.append(rprev)
            sprev = sthis

        # Remove unneeded words from the top of the output, if we can
        # prove by range analysis that they'll always be zero.
        sprev = sprev[:self.target.nwords(newmax)]

        return Multiprecision(self.target, newmin, newmax, sprev)

    def extract_bits(self, start, bits=None):
        if bits is None:
            bits = (self.maxval >> start).bit_length()

        # Overly thorough range analysis: if min and max have the same
        # *quotient* by 2^bits, then the result of reducing anything
        # in the range [min,max] mod 2^bits has to fall within the
        # obvious range. But if they have different quotients, then
        # you can wrap round the modulus and so any value mod 2^bits
        # is possible.
        newmin = self.minval >> start
        newmax = self.maxval >> start
        if (newmin >> bits) != (newmax >> bits):
            newmin = 0
            newmax = (1 << bits) - 1

        nwords = self.target.nwords(newmax)
        words = []
        for i in range(nwords):
            srcpos = i * self.target.bits + start
            maxbits = min(self.target.bits, start + bits - srcpos)
            wordindex = srcpos // self.target.bits
            if srcpos % self.target.bits == 0:
                word = self.getword(srcpos // self.target.bits)
            elif (wordindex+1 >= len(self.words) or
                  srcpos % self.target.bits + maxbits < self.target.bits):
                word = self.target.new_value(
                    "(%%s) >> %d" % (srcpos % self.target.bits),
                    self.getword(srcpos // self.target.bits))
            else:
                word = self.target.new_value(
                    "((%%s) >> %d) | ((%%s) << %d)" % (
                        srcpos % self.target.bits,
                        self.target.bits - (srcpos % self.target.bits)),
                    self.getword(srcpos // self.target.bits),
                    self.getword(srcpos // self.target.bits + 1))
            if maxbits < self.target.bits and maxbits < bits:
                word = self.target.new_value(
                    "(%%s) & ((((BignumInt)1) << %d)-1)" % maxbits,
                    word)
            words.append(word)

        return Multiprecision(self.target, newmin, newmax, words)

# Each Statement has a list of variables it reads, and a list of ones
# it writes. 'forms' is a list of multiple actual C statements it
# could be generated as, depending on which of its output variables is
# actually used (e.g. no point calling BignumADC if the generated
# carry in a particular case is unused, or BignumMUL if nobody needs
# the top half). It is indexed by a bitmap whose bits correspond to
# the entries in wvars, with wvars[0] the MSB and wvars[-1] the LSB.
Statement = namedtuple("Statement", "rvars wvars forms")

class CodegenTarget(object):
    def __init__(self, bits):
        self.bits = bits
        self.valindex = 0
        self.stmts = []
        self.generators = {}
        self.bv_words = (130 + self.bits - 1) // self.bits
        self.carry_index = 0

    def nwords(self, maxval):
        return (maxval.bit_length() + self.bits - 1) // self.bits

    def stmt(self, stmt, needed=False):
        index = len(self.stmts)
        self.stmts.append([needed, stmt])
        for val in stmt.wvars:
            self.generators[val] = index

    def new_value(self, formatstr=None, *deps):
        name = "v%d" % self.valindex
        self.valindex += 1
        if formatstr is not None:
            self.stmt(Statement(
                    rvars=deps, wvars=[name],
                    forms=[None, name + " = " + formatstr % deps]))
        return name

    def bigval_input(self, name, bits):
        words = (bits + self.bits - 1) // self.bits
        # Expect not to require an entire extra word
        assert words == self.bv_words

        return Multiprecision(self, 0, (1<<bits)-1, [
                self.new_value("%s->w[%d]" % (name, i)) for i in range(words)])

    def const(self, value):
        # We only support constants small enough to both fit in a
        # BignumInt (of any size supported) _and_ be expressible in C
        # with no weird integer literal syntax like a trailing LL.
        #
        # Supporting larger constants would be possible - you could
        # break 'value' up into word-sized pieces on the Python side,
        # and generate a legal C expression for each piece by
        # splitting it further into pieces within the
        # standards-guaranteed 'unsigned long' limit of 32 bits and
        # then casting those to BignumInt before combining them with
        # shifts. But it would be a lot of effort, and since the
        # application for this code doesn't even need it, there's no
        # point in bothering.
        assert value < 2**16
        return Multiprecision(self, value, value, ["%d" % value])

    def current_carry(self):
        return "carry%d" % self.carry_index

    def add(self, a1, a2):
        ret = self.new_value()
        adcform = "BignumADC(%s, carry, %s, %s, 0)" % (ret, a1, a2)
        plainform = "%s = %s + %s" % (ret, a1, a2)
        self.carry_index += 1
        carryout = self.current_carry()
        self.stmt(Statement(
                rvars=[a1,a2], wvars=[ret,carryout],
                forms=[None, adcform, plainform, adcform]))
        return ret

    def adc(self, a1, a2):
        ret = self.new_value()
        adcform = "BignumADC(%s, carry, %s, %s, carry)" % (ret, a1, a2)
        plainform = "%s = %s + %s + carry" % (ret, a1, a2)
        carryin = self.current_carry()
        self.carry_index += 1
        carryout = self.current_carry()
        self.stmt(Statement(
                rvars=[a1,a2,carryin], wvars=[ret,carryout],
                forms=[None, adcform, plainform, adcform]))
        return ret

    def muladd(self, m1, m2, *addends):
        rlo = self.new_value()
        rhi = self.new_value()
        wideform = "BignumMUL%s(%s)" % (
            { 0:"", 1:"ADD", 2:"ADD2" }[len(addends)],
            ", ".join([rhi, rlo, m1, m2] + list(addends)))
        narrowform = " + ".join(["%s = %s * %s" % (rlo, m1, m2)] +
                                list(addends))
        self.stmt(Statement(
                rvars=[m1,m2]+list(addends), wvars=[rhi,rlo],
                forms=[None, narrowform, wideform, wideform]))
        return rhi, rlo

    def write_bigval(self, name, val):
        for i in range(self.bv_words):
            word = val.getword(i)
            self.stmt(Statement(
                    rvars=[word], wvars=[],
                    forms=["%s->w[%d] = %s" % (name, i, word)]),
                      needed=True)

    def compute_needed(self):
        used_vars = set()

        self.queue = [stmt for (needed,stmt) in self.stmts if needed]
        while len(self.queue) > 0:
            stmt = self.queue.pop(0)
            deps = []
            for var in stmt.rvars:
                if var[0] in string.digits:
                    continue # constant
                deps.append(self.generators[var])
                used_vars.add(var)
            for index in deps:
                if not self.stmts[index][0]:
                    self.stmts[index][0] = True
                    self.queue.append(self.stmts[index][1])

        forms = []
        for i, (needed, stmt) in enumerate(self.stmts):
            if needed:
                formindex = 0
                for (j, var) in enumerate(stmt.wvars):
                    formindex *= 2
                    if var in used_vars:
                        formindex += 1
                forms.append(stmt.forms[formindex])

                # Now we must check whether this form of the statement
                # also writes some variables we _don't_ actually need
                # (e.g. if you only wanted the top half from a mul, or
                # only the carry from an adc, you'd be forced to
                # generate the other output too). Easiest way to do
                # this is to look for an identical statement form
                # later in the array.
                maxindex = max(i for i in range(len(stmt.forms))
                               if stmt.forms[i] == stmt.forms[formindex])
                extra_vars = maxindex & ~formindex
                bitpos = 0
                while extra_vars != 0:
                    if extra_vars & (1 << bitpos):
                        extra_vars &= ~(1 << bitpos)
                        var = stmt.wvars[-1-bitpos]
                        used_vars.add(var)
                        # Also, write out a cast-to-void for each
                        # subsequently unused value, to prevent gcc
                        # warnings when the output code is compiled.
                        forms.append("(void)" + var)
                    bitpos += 1

        used_carry = any(v.startswith("carry") for v in used_vars)
        used_vars = [v for v in used_vars if v.startswith("v")]
        used_vars.sort(key=lambda v: int(v[1:]))

        return used_carry, used_vars, forms

    def text(self):
        used_carry, values, forms = self.compute_needed()

        ret = ""
        while len(values) > 0:
            prefix, sep, suffix = "    BignumInt ", ", ", ";"
            currline = values.pop(0)
            while (len(values) > 0 and
                   len(prefix+currline+sep+values[0]+suffix) < 79):
                currline += sep + values.pop(0)
            ret += prefix + currline + suffix + "\n"
        if used_carry:
            ret += "    BignumCarry carry;\n"
        if ret != "":
            ret += "\n"
        for stmtform in forms:
            ret += "    %s;\n" % stmtform
        return ret

def gen_add(target):
    # This is an addition _without_ reduction mod p, so that it can be
    # used both during accumulation of the polynomial and for adding
    # on the encrypted nonce at the end (which is mod 2^128, not mod
    # p).
    #
    # Because one of the inputs will have come from our
    # not-completely-reducing multiplication function, we expect up to
    # 3 extra bits of input.

    a = target.bigval_input("a", 133)
    b = target.bigval_input("b", 133)
    ret = a + b
    target.write_bigval("r", ret)
    return """\
static void bigval_add(bigval *r, const bigval *a, const bigval *b)
{
%s}
\n""" % target.text()

def gen_mul(target):
    # The inputs are not 100% reduced mod p. Specifically, we can get
    # a full 130-bit number from the pow5==0 pass, and then a 130-bit
    # number times 5 from the pow5==1 pass, plus a possible carry. The
    # total of that can be easily bounded above by 2^130 * 8, so we
    # need to assume we're multiplying two 133-bit numbers.

    a = target.bigval_input("a", 133)
    b = target.bigval_input("b", 133)
    ab = a * b
    ab0 = ab.extract_bits(0, 130)
    ab1 = ab.extract_bits(130, 130)
    ab2 = ab.extract_bits(260)
    ab1_5 = target.const(5) * ab1
    ab2_25 = target.const(25) * ab2
    ret = ab0 + ab1_5 + ab2_25
    target.write_bigval("r", ret)
    return """\
static void bigval_mul_mod_p(bigval *r, const bigval *a, const bigval *b)
{
%s}
\n""" % target.text()

def gen_final_reduce(target):
    # Given our input number n, n >> 130 is usually precisely the
    # multiple of p that needs to be subtracted from n to reduce it to
    # strictly less than p, but it might be too low by 1 (but not more
    # than 1, given the range of our input is nowhere near the square
    # of the modulus). So we add another 5, which will push a carry
    # into the 130th bit if and only if that has happened, and then
    # use that to decide whether to subtract one more copy of p.

    a = target.bigval_input("n", 133)
    q = a.extract_bits(130)
    adjusted = a.extract_bits(0, 130) + target.const(5) * q
    final_subtract = (adjusted + target.const(5)).extract_bits(130)
    adjusted2 = adjusted + target.const(5) * final_subtract
    ret = adjusted2.extract_bits(0, 130)
    target.write_bigval("n", ret)
    return """\
static void bigval_final_reduce(bigval *n)
{
%s}
\n""" % target.text()

pp_keyword = "#if"
for bits in [16, 32, 64]:
    sys.stdout.write("%s BIGNUM_INT_BITS == %d\n\n" % (pp_keyword, bits))
    pp_keyword = "#elif"
    sys.stdout.write(gen_add(CodegenTarget(bits)))
    sys.stdout.write(gen_mul(CodegenTarget(bits)))
    sys.stdout.write(gen_final_reduce(CodegenTarget(bits)))
sys.stdout.write("""#else
#error Add another bit count to contrib/make1305.py and rerun it
#endif
""")
