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

class MemDumpCommand(gdb.Command):
    """Print a hex+ASCII dump of object EXP.

EXP must be an expression whose value resides in memory. The
contents of the memory it occupies are printed in a standard hex
dump format, with each line showing an offset relative to the
address of EXP, then the hex byte values of the memory at that
offset, and then a translation into ASCII of the same bytes (with
values outside the printable ASCII range translated as '.').

To dump a number of bytes from a particular address, it's useful
to use the gdb expression extensions {TYPE} and @LENGTH. For
example, if 'ptr' and 'len' are variables giving an address and a
length in bytes, then the command

    memdump {char} ptr @ len

will dump the range of memory described by those two variables."""

    def __init__(self):
        super(MemDumpCommand, self).__init__(
            "memdump", gdb.COMMAND_DATA, gdb.COMPLETE_EXPRESSION)

    def invoke(self, cmdline, from_tty):
        expr = gdb.parse_and_eval(cmdline)
        try:
            start, size = int(expr.address), expr.type.sizeof
        except gdb.error as e:
            sys.stderr.write(str(e))
            return
        except (TypeError, AttributeError):
            sys.stderr.write("expression must identify an object in memory")
            return

        width = 16
        line_ptr_type = gdb.lookup_type(
            "unsigned char").const().array(width).pointer()

        dumpaddr = 0
        while size > 0:
            line = gdb.Value(start).cast(line_ptr_type).dereference()
            thislinelen = min(size, width)
            start += thislinelen
            size -= thislinelen

            dumpline = [None, " "] + ["   "] * width + ["  "] + [""] * width

            dumpline[0] = "{:08x}".format(dumpaddr)
            dumpaddr += thislinelen

            for i in range(thislinelen):
                ch = int(line[i]) & 0xFF
                dumpline[2+i] = " {:02x}".format(ch)
                dumpline[3+width+i] = chr(ch) if 0x20 <= ch < 0x7F else "."

            sys.stdout.write("".join(dumpline) + "\n")

MemDumpCommand()
