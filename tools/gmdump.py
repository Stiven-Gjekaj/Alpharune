#!/usr/bin/env python3
# gmdump: show the parts of a GameMaker data file (data.win or game.ios).
# Usage: tools/gmdump.py FILE chunks
#        tools/gmdump.py FILE gen8
#        tools/gmdump.py FILE code NAME     (disassemble one code entry)
#        tools/gmdump.py FILE codes         (list the code entries)
import struct, sys

class Data:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.chunks = {}
        p = 8
        while p < len(self.d):
            name = self.d[p:p + 4].decode()
            size = self.u32(p + 4)
            self.chunks[name] = (p + 8, size)
            p += 8 + size

    def u32(self, o): return struct.unpack_from('<I', self.d, o)[0]
    def i32(self, o): return struct.unpack_from('<i', self.d, o)[0]
    def u16(self, o): return struct.unpack_from('<H', self.d, o)[0]
    def s(self, ptr):
        if ptr == 0: return None
        n = self.u32(ptr - 4)
        return self.d[ptr:ptr + n].decode('utf8', 'replace')

    def plist(self, name):
        o, size = self.chunks[name]
        if size == 0: return []
        return [self.u32(o + 4 + 4 * i) for i in range(self.u32(o))]

    def gen8(self):
        o, _ = self.chunks['GEN8']
        d = {}
        d['debug_disabled'] = self.d[o]
        d['bytecode'] = self.d[o + 1]
        d['filename'] = self.s(self.u32(o + 4))
        d['config'] = self.s(self.u32(o + 8))
        d['last_obj'] = self.u32(o + 12)
        d['last_tile'] = self.u32(o + 16)
        d['game_id'] = self.u32(o + 20)
        q = o + 24 + 16
        d['name'] = self.s(self.u32(q)); q += 4
        d['version'] = struct.unpack_from('<4I', self.d, q); q += 16
        d['window'] = struct.unpack_from('<2I', self.d, q); q += 8
        d['info'] = hex(self.u32(q)); q += 4
        q += 4 + 16
        d['timestamp'] = struct.unpack_from('<Q', self.d, q)[0]; q += 8
        d['display_name'] = self.s(self.u32(q)); q += 4
        q += 8 + 8
        d['steam_app'] = self.i32(q); q += 4
        q += 4  # debugger port
        n = self.u32(q); q += 4
        d['room_order'] = [self.u32(q + 4 * i) for i in range(n)]; q += 4 * n
        q += 8 * 5  # random uids, GMS2
        d['fps'] = struct.unpack_from('<f', self.d, q)[0]
        return d

    def vari(self):
        o, size = self.chunks['VARI']
        out = []
        p = o + 12
        while p + 20 <= o + size:
            out.append(dict(name=self.s(self.u32(p)), inst=self.i32(p + 4), id=self.i32(p + 8),
                            occ=self.u32(p + 12), first=self.u32(p + 16)))
            p += 20
        return out

    def func(self):
        o, size = self.chunks['FUNC']
        n = self.u32(o)
        return [dict(name=self.s(self.u32(o + 4 + 12 * i)), occ=self.u32(o + 8 + 12 * i),
                     first=self.u32(o + 12 + 12 * i)) for i in range(n)]

    def codes(self):
        out = []
        for e in self.plist('CODE'):
            name = self.s(self.u32(e))
            length = self.u32(e + 4)
            locs = self.u16(e + 8)
            args = self.u16(e + 10)
            rel = self.i32(e + 12)
            off = self.u32(e + 16)
            out.append(dict(name=name, len=length, locals=locs, args=args, start=e + 12 + rel, offset=off, entry=e))
        return out

    def refmap(self):
        # Follow the chain of each variable and function to map each
        # instruction address to its name.
        m = {}
        for kind, table in (('var', self.vari()), ('func', self.func())):
            for v in table:
                # A variable points to its instruction. A function points to
                # the operand after its instruction.
                a = v['first'] - (4 if kind == 'func' else 0)
                for _ in range(v['occ']):
                    m[a] = (kind, v['name'], v)
                    nxt = self.u32(a + 4) & 0x07FFFFFF
                    a += nxt
        return m

OPS = {0x07: 'conv', 0x08: 'mul', 0x09: 'div', 0x0A: 'rem', 0x0B: 'mod', 0x0C: 'add', 0x0D: 'sub',
       0x0E: 'and', 0x0F: 'or', 0x10: 'xor', 0x11: 'neg', 0x12: 'not', 0x13: 'shl', 0x14: 'shr',
       0x15: 'cmp', 0x45: 'pop', 0x84: 'pushi', 0x86: 'dup', 0x9C: 'ret', 0x9D: 'exit', 0x9E: 'popz',
       0xB6: 'b', 0xB7: 'bt', 0xB8: 'bf', 0xBA: 'pushenv', 0xBB: 'popenv', 0xC0: 'push', 0xC1: 'pushloc',
       0xC2: 'pushglb', 0xC3: 'pushbltn', 0xD9: 'call', 0x99: 'callv', 0xFF: 'break'}
T = 'd f i l b v s inst del u ui ? ? ? ? e'.split()
CMP = {1: 'lt', 2: 'le', 3: 'eq', 4: 'neq', 5: 'ge', 6: 'gt'}
VT = {0x00: 'array', 0x80: 'stacktop', 0xA0: 'normal', 0xE0: 'instance', 0x10: 'pushaf', 0x90: 'popaf'}

def disasm(dd, c, refs):
    p = c['start'] + c['offset']
    end = c['start'] + c['len']
    while p < end:
        w = dd.u32(p)
        op = w >> 24
        t1 = (w >> 16) & 0xF
        t2 = (w >> 20) & 0xF
        name = OPS.get(op, '?%02x' % op)
        text = name
        size = 4
        if op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
            off = w & 0x7FFFFF
            if off & 0x400000: off -= 0x800000
            text += ' %+d -> %x' % (off * 4, p + off * 4 - c['start'])
        elif op == 0x15:
            text += '.%s.%s %s' % (T[t1], T[t2], CMP.get((w >> 8) & 0xFF, '?'))
        elif op in (0xC0, 0xC1, 0xC2, 0xC3, 0x84):
            text += '.' + T[t1]
            if t1 == 0xF: text += ' %d' % struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0]
            elif t1 == 5:
                inst = struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0]
                r = dd.u32(p + 4)
                ref = refs.get(p, ('?', '?'))
                text += ' %s.%s [%s]' % (inst, ref[1], VT.get((r >> 24) & 0xF8, hex(r >> 24)))
                size = 8
            elif t1 == 6:
                text += ' "%s"' % dd.s(dd.u32(dd.plist('STRG')[dd.u32(p + 4)] if False else 0) or 0) if False else ' str#%d %r' % (dd.u32(p + 4), strg(dd, dd.u32(p + 4)))
                size = 8
            elif t1 == 2:
                v = dd.i32(p + 4)
                ref = refs.get(p)
                text += ' %d' % v + (' <%s %s>' % (ref[0], ref[1]) if ref else '')
                size = 8
            elif t1 in (0, 3):
                v = struct.unpack_from('<d' if t1 == 0 else '<q', dd.d, p + 4)[0]
                text += ' %r' % v
                size = 12
            elif t1 == 4:
                text += ' %d' % dd.i32(p + 4); size = 8
        elif op == 0x45:
            inst = struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0]
            if t1 == 0xF:
                text += '.swap %d' % (w & 0xFFFF)
            else:
                r = dd.u32(p + 4)
                ref = refs.get(p, ('?', '?'))
                text += '.%s.%s %s.%s [%s]' % (T[t1], T[t2], inst, ref[1], VT.get((r >> 24) & 0xF8, hex(r >> 24)))
                size = 8
        elif op == 0xD9:
            ref = refs.get(p, ('?', '?'))
            text += ' %s argc=%d' % (ref[1], w & 0xFFFF)
            size = 8
        elif op == 0x99:
            text += ' argc=%d' % (w & 0xFFFF)
        elif op == 0x86:
            text += '.%s %d' % (T[t1], w & 0xFF) + (' swap%d' % ((w >> 8) & 0xFF) if (w >> 8) & 0xFF else '')
        elif op == 0xFF:
            v = struct.unpack('<h', struct.pack('<H', w & 0xFFFF))[0]
            text += '.%s %d' % (T[t1], v)
            if v == -11: size = 8; text += ' %d' % dd.i32(p + 4)
        elif op in (0x07,):
            text += '.%s.%s' % (T[t1], T[t2])
        elif op in (0x9C, 0x9D, 0x9E, 0x11, 0x12):
            text += '.%s' % T[t1]
        else:
            text += '.%s.%s' % (T[t1], T[t2])
        print('%6x  %08x  %s' % (p - c['start'], w, text))
        p += size

_strg = None
def strg(dd, i):
    global _strg
    if _strg is None: _strg = dd.plist('STRG')
    return dd.s(_strg[i] + 4)

if __name__ == '__main__':
    dd = Data(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == 'chunks':
        for k, (o, s) in dd.chunks.items(): print(k, hex(o), s)
    elif cmd == 'gen8':
        for k, v in dd.gen8().items(): print(k, v)
    elif cmd == 'codes':
        for c in dd.codes(): print(c['name'], c['len'], c['locals'], c['args'], c['offset'])
    elif cmd == 'code':
        refs = dd.refmap()
        for c in dd.codes():
            if c['name'] == sys.argv[3]:
                print(c); disasm(dd, c, refs)
    elif cmd == 'all':
        # The disassembly of each code entry, with its name before it.
        refs = dd.refmap()
        for c in dd.codes():
            print('== ' + c['name'])
            disasm(dd, c, refs)
