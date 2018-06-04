import gdb
import re
import gdb.printing

class PuTTYBignumPrettyPrinter(gdb.printing.PrettyPrinter):
    "Pretty-print PuTTY's Bignum type."
    name = "Bignum"

    def __init__(self, val):
        super(PuTTYBignumPrettyPrinter, self).__init__(self.name)
        self.val = val

    def to_string(self):
        type_BignumInt = gdb.lookup_type("BignumInt")
        type_BignumIntPtr = type_BignumInt.pointer()
        BIGNUM_INT_BITS = 8 * type_BignumInt.sizeof
        array = self.val.cast(type_BignumIntPtr)
        aget = lambda i: int(array[i]) & ((1 << BIGNUM_INT_BITS)-1)

        try:
            length = aget(0)
            value = 0
            for i in range(length):
                value |= aget(i+1) << (BIGNUM_INT_BITS * i)
            return "Bignum({:#x})".format(value)

        except gdb.MemoryError:
            address = int(array)
            if address == 0:
                return "Bignum(NULL)".format(address)
            return "Bignum(invalid @ {:#x})".format(address)

rcpp = gdb.printing.RegexpCollectionPrettyPrinter("PuTTY")
rcpp.add_printer(PuTTYBignumPrettyPrinter.name, "^Bignum$",
                 PuTTYBignumPrettyPrinter)

gdb.printing.register_pretty_printer(None, rcpp)
