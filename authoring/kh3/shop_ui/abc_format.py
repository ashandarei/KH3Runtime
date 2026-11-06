"""Read ABC structure for constructor hooks without rewriting any stock code.

Offsets are into the input bytes. This authoring parser only reads the local
game fixture; no game method, string pool or graphics are emitted as literals.
"""
from dataclasses import dataclass
import struct


def u30(value):
    if not 0 <= value < (1 << 30):
        raise ValueError('Invalid u30')
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    return bytes(result + bytes([value]))


def tag(kind, data=b''):
    size = len(data)
    return struct.pack('<H', (kind << 6) | min(size, 63)) + (struct.pack('<I', size) if size >= 63 else b'') + data


def tags(movie):
    if movie[:3] not in (b'GFX', b'FWS') or struct.unpack_from('<I', movie, 4)[0] != len(movie):
        raise ValueError('Expected complete uncompressed GFX/SWF')
    rect_size = (5 + 4 * (movie[8] >> 3) + 7) // 8
    position = 8 + rect_size + 4
    while position < len(movie):
        start = position
        header = struct.unpack_from('<H', movie, position)[0]
        kind, size = header >> 6, header & 63
        position += 2
        if size == 63:
            size = struct.unpack_from('<I', movie, position)[0]
            position += 4
        end = position + size
        if end > len(movie):
            raise ValueError('Truncated tag')
        yield kind, start, position, end
        position = end
    if position != len(movie):
        raise ValueError('Trailing tag data')


@dataclass
class Field:
    start: int
    end: int
    value: int


class ABC:
    def __init__(self, data):
        self.data, self.pos = data, 4
        if data[:4] != struct.pack('<HH', 16, 46):
            raise ValueError('Unsupported ABC version')
        self.pools = {}
        for name in ('integers', 'unsigned', 'doubles', 'strings', 'namespaces', 'sets', 'names'):
            count = self.field()
            values = [None]
            for _ in range(1, count.value):
                if name in ('integers', 'unsigned'):
                    value = self.number()
                elif name == 'doubles':
                    value = self.take(8)
                elif name == 'strings':
                    value = self.take(self.number()).decode('utf-8')
                elif name == 'namespaces':
                    value = self.byte(), self.number()
                elif name == 'sets':
                    value = [self.number() for _ in range(self.number())]
                else:
                    kind = self.byte()
                    lengths = {7:2, 13:2, 15:1, 16:1, 17:0, 18:0, 9:2, 14:2, 27:1, 28:1}
                    if kind == 29:
                        value = (kind, self.number(), [self.number() for _ in range(self.number())])
                    else:
                        value = (kind, *[self.number() for _ in range(lengths[kind])])
                values.append(value)
            self.pools[name] = (count, self.pos, values)
        self.method_count = self.number()
        for _ in range(self.method_count):
            params = self.number()
            for _ in range(params + 2):  # return type, parameter types, method name
                self.number()
            flags = self.byte()
            if flags & 8:
                for _ in range(self.number()):
                    self.number()
                    self.byte()
            if flags & 128:
                for _ in range(params):
                    self.number()
        for _ in range(self.number()):  # metadata
            self.number()
            count = self.number()
            for _ in range(count * 2):
                self.number()
        class_count = self.number()
        self.classes = {}
        for _ in range(class_count):
            name = self.number()
            self.number()  # superclass
            flags = self.byte()
            if flags & 8:
                self.number()
            for _ in range(self.number()):
                self.number()
            constructor = self.number()
            self.classes[self.qname(name)] = constructor
            self.traits()
        for _ in range(class_count):
            self.number()
            self.traits()
        for _ in range(self.number()):  # scripts
            self.number()
            self.traits()
        self.bodies = {}
        for _ in range(self.number()):
            method = self.number()
            stack = self.field()
            locals_, initial, maximum = self.number(), self.number(), self.number()
            length = self.field()
            start = self.pos
            code = self.take(length.value)
            exceptions = self.number()
            for _ in range(exceptions * 5):
                self.number()
            self.traits()
            self.bodies[method] = dict(stack=stack, locals=locals_, initial=initial,
                                      maximum=maximum, length=length, start=start,
                                      code=code, exceptions=exceptions)
        if self.pos != len(data):
            raise ValueError('Unparsed ABC bytes')

    def qname(self, index):
        kind, namespace, name = self.pools['names'][2][index]
        if kind not in (7, 13):
            raise ValueError('Expected QName')
        ns = self.pools['namespaces'][2][namespace]
        strings = self.pools['strings'][2]
        prefix = strings[ns[1]] if ns[1] else ''
        return (prefix + '.' if prefix else '') + strings[name]

    def traits(self):
        for _ in range(self.number()):
            self.number()
            kind = self.byte()
            trait = kind & 15
            self.number()
            self.number()
            if trait in (0, 6):
                if self.number():
                    self.byte()
            elif trait not in (1, 2, 3, 4, 5):
                raise ValueError('Invalid trait')
            if kind & 64:
                for _ in range(self.number()):
                    self.number()

    def take(self, count):
        end = self.pos + count
        if end > len(self.data):
            raise ValueError('Truncated ABC')
        result = self.data[self.pos:end]
        self.pos = end
        return result

    def byte(self):
        return self.take(1)[0]

    def number(self):
        result = 0
        for shift in range(0, 35, 7):
            byte = self.byte()
            result |= (byte & 127) << shift
            if not byte & 128:
                return result
        raise ValueError('Invalid encoded integer')

    def field(self):
        start = self.pos
        value = self.number()
        return Field(start, self.pos, value)
