import gdb
import re
import gdb.printing

class PuTTYMpintPrettyPrinter(gdb.printing.PrettyPrinter):
    "Pretty-print PuTTY's mp_int type."
    name = "mp_int"

    def __init__(self, val):
        super(PuTTYMpintPrettyPrinter, self).__init__(self.name)
        self.val = val

    def to_string(self):
        type_BignumInt = gdb.lookup_type("BignumInt")
        type_BignumIntPtr = type_BignumInt.pointer()
        BIGNUM_INT_BITS = 8 * type_BignumInt.sizeof
        array = self.val["w"]
        aget = lambda i: int(array[i]) & ((1 << BIGNUM_INT_BITS)-1)

        try:
            length = int(self.val["nw"])
            value = 0
            for i in range(length):
                value |= aget(i) << (BIGNUM_INT_BITS * i)
            return "mp_int({:#x})".format(value)

        except gdb.MemoryError:
            address = int(self.val)
            if address == 0:
                return "mp_int(NULL)".format(address)
            return "mp_int(invalid @ {:#x})".format(address)

class PuTTYPtrlenPrettyPrinter(gdb.printing.PrettyPrinter):
    "Pretty-print strings in PuTTY's ptrlen type."
    name = "ptrlen"

    def __init__(self, val):
        super(PuTTYPtrlenPrettyPrinter, self).__init__(self.name)
        self.val = val

    def to_string(self):
        length = int(self.val["len"])
        char_array_ptr_type = gdb.lookup_type(
            "char").const().array(length).pointer()
        line = self.val["ptr"].cast(char_array_ptr_type).dereference()
        return repr(bytes(int(line[i]) for i in range(length))).lstrip('b')

class PuTTYPrinterSelector(gdb.printing.PrettyPrinter):
    def __init__(self):
        super(PuTTYPrinterSelector, self).__init__("PuTTY")
    def __call__(self, val):
        if str(val.type) == "mp_int *":
            return PuTTYMpintPrettyPrinter(val)
        if str(val.type) == "ptrlen":
            return PuTTYPtrlenPrettyPrinter(val)
        return None

gdb.printing.register_pretty_printer(None, PuTTYPrinterSelector())

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
            raise gdb.GdbError(str(e))
        except (TypeError, AttributeError):
            raise gdb.GdbError("expression must identify an object in memory")
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

class ContainerOf(gdb.Function):
    """Implement the container_of macro from PuTTY's defs.h.

    Arguments are an object or pointer to object; a type to convert it
    to; and, optionally the name of the structure member in the
    destination type that the pointer points to. (If the member name
    is not provided, then the default is whichever member of the
    destination structure type has the same type as the input object,
    provided there's only one.)

    Due to limitations of GDB's convenience function syntax, the type
    and member names must be provided as strings.

    """

    def __init__(self):
        super(ContainerOf, self).__init__("container_of")

    def match_type(self, obj, typ):
        if obj.type == typ:
            return obj

        try:
            ref = obj.referenced_value()
            if ref.type == typ:
                return ref
        except gdb.error:
            pass

        return None

    def invoke(self, obj, dest_type_name_val, member_name_val=None):
        try:
            dest_type_name = dest_type_name_val.string()
        except gdb.error:
            raise gdb.GdbError("destination type name must be a string")

        try:
            dest_type = gdb.lookup_type(dest_type_name)
        except gdb.error:
            raise gdb.GdbError("no such type '{dt}'".format(dt=dest_type_name))

        if member_name_val is not None:
            try:
                member_name = member_name_val.string()
            except gdb.error:
                raise gdb.GdbError("member name must be a string")

            for field in dest_type.fields():
                if field.name == member_name:
                    break
            else:
                raise gdb.GdbError(
                    "type '{dt}' has no member called '{memb}'"
                    .format(dt=dest_type_name, memb=member_name))

            match_obj = self.match_type(obj, field.type)
        else:
            matches = []

            for field in dest_type.fields():
                this_match_obj = self.match_type(obj, field.type)
                if this_match_obj is not None:
                    match_obj = this_match_obj
                    matches.append(field)

            if len(matches) == 0:
                raise gdb.GdbError(
                    "type '{dt}' has no member matching type '{ot}'"
                    .format(dt=dest_type_name, ot=obj.type))

            if len(matches) > 1:
                raise gdb.GdbError(
                    "type '{dt}' has multiple members matching type '{ot}'"
                    " ({memberlist})"
                    .format(dt=dest_type_name, ot=obj.type,
                            memberlist=", ".join(f.name for f in matches)))

            field = matches[0]

        if field.bitpos % 8 != 0:
            raise gdb.GdbError(
                "offset of field '{memb}' is a fractional number of bytes"
                .format(dt=dest_type_name, memb=member_name))
        offset = field.bitpos // 8

        if match_obj.type != field.type:
            raise gdb.GdbError(
                "value to convert does not have type '{ft}'"
                .format(ft=field.type))

        try:
            addr = int(match_obj.address)
        except gdb.error:
            raise gdb.GdbError("cannot take address of value to convert")

        return gdb.Value(addr - offset).cast(dest_type.pointer())

ContainerOf()

class List234(gdb.Function):
    """List the elements currently stored in a tree234.

    Arguments are a tree234, and optionally a value type. If no value
    type is given, the result is a list of the raw void * pointers
    stored in the tree. Otherwise, each one is cast to a pointer to the
    value type and dereferenced.

    Due to limitations of GDB's convenience function syntax, the value
    type must be provided as a string.

    """

    def __init__(self):
        super(List234, self).__init__("list234")

    def add_elements(self, node, destlist):
        kids = node["kids"]
        elems = node["elems"]
        for i in range(4):
            if int(kids[i]) != 0:
                add_elements(self, kids[i].dereference(), destlist)
            if i < 3 and int(elems[i]) != 0:
                destlist.append(elems[i])

    def invoke(self, tree, value_type_name_val=None):
        if value_type_name_val is not None:
            try:
                value_type_name = value_type_name_val.string()
            except gdb.error:
                raise gdb.GdbError("value type name must be a string")

            try:
                value_type = gdb.lookup_type(value_type_name)
            except gdb.error:
                raise gdb.GdbError("no such type '{dt}'"
                                   .format(dt=value_type_name))
        else:
            value_type = None

        try:
            tree = tree.dereference()
        except gdb.error:
            pass

        if tree.type == gdb.lookup_type("tree234"):
            tree = tree["root"].dereference()

        if tree.type != gdb.lookup_type("node234"):
            raise gdb.GdbError(
                "input value is not a tree234")

        if int(tree.address) == 0:
            # If you try to return {} for the empty list, gdb gives
            # the cryptic error "bad array bounds (0, -1)"! We return
            # NULL as the best approximation to 'sorry, list is
            # empty'.
            return gdb.parse_and_eval("((void *)0)")

        elems = []
        self.add_elements(tree, elems)

        if value_type is not None:
            value_ptr_type_name = str(value_type.pointer())
            elem_fmt = lambda p: "*({}){}".format(value_ptr_type_name, int(p))
        else:
            elem_fmt = lambda p: "(void *){}".format(int(p))

        elems_str = "{" + ",".join(elem_fmt(elem) for elem in elems) + "}"
        return gdb.parse_and_eval(elems_str)

List234()
