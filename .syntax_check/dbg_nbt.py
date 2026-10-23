import gzip, struct

d = gzip.open('test_proj.litematic', 'rb').read()


class R:
    def __init__(self, d):
        self.d = d
        self.p = 0

    def u1(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u2(self):
        v = struct.unpack_from('>H', self.d, self.p)[0]
        self.p += 2
        return v

    def i4(self):
        v = struct.unpack_from('>i', self.d, self.p)[0]
        self.p += 4
        return v

    def i8(self):
        v = struct.unpack_from('>q', self.d, self.p)[0]
        self.p += 8
        return v

    def st(self):
        n = self.u2()
        v = self.d[self.p:self.p + n].decode('utf-8')
        self.p += n
        return v


NAMES = {0: 'End', 1: 'Byte', 2: 'Short', 3: 'Int', 4: 'Long', 5: 'Float', 6: 'Double',
         7: 'ByteArray', 8: 'String', 9: 'List', 10: 'Compound', 11: 'IntArray', 12: 'LongArray'}


def payload(r, t, ind):
    pad = ' ' * ind
    if t == 1:
        v = struct.unpack_from('>b', r.d, r.p)[0]; r.p += 1; print(f'{pad}= {v}')
    elif t == 2:
        v = struct.unpack_from('>h', r.d, r.p)[0]; r.p += 2; print(f'{pad}= {v}')
    elif t == 3:
        print(f'{pad}= {r.i4()}')
    elif t == 4:
        print(f'{pad}= {r.i8()}')
    elif t == 5:
        v = struct.unpack_from('>f', r.d, r.p)[0]; r.p += 4; print(f'{pad}= {v}')
    elif t == 6:
        v = struct.unpack_from('>d', r.d, r.p)[0]; r.p += 8; print(f'{pad}= {v}')
    elif t == 7:
        n = r.i4(); r.p += n; print(f'{pad}[bytearray {n}]')
    elif t == 8:
        print(f'{pad}= {r.st()!r}')
    elif t == 11:
        n = r.i4(); r.p += 4 * n; print(f'{pad}[intarray {n}]')
    elif t == 12:
        n = r.i4(); r.p += 8 * n; print(f'{pad}[longarray {n}]')
    elif t == 9:
        et = r.u1(); n = r.i4(); print(f'{pad}[list type={NAMES[et]} n={n}]')
        for i in range(n):
            print(f'{pad}  [{i}]')
            payload(r, et, ind + 4)
    elif t == 10:
        print(f'{pad}{{')
        while True:
            if r.d[r.p] == 0:
                r.p += 1
                break
            tag(r, ind + 2)
        print(f'{pad}}}')
    else:
        raise SystemExit(f'bad tag {t} at {r.p}')


def tag(r, ind=0):
    t = r.u1()
    if t == 0:
        return
    name = r.st()
    print(' ' * ind + f'{NAMES[t]} {name!r}', end='')
    payload(r, t, ind + 2)


r = R(d)
tag(r)
print('consumed', r.p, 'of', len(d))
