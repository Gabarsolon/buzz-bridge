"""Read-only, section-name-independent PS3 ELF queries. No game bytes are saved."""
import hashlib
import struct


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:6] != b'\x7fELF\x02\x02' or len(data) < 64:
            raise ValueError('expected decrypted big-endian ELF64')
        if struct.unpack_from('>HH', data, 16) != (2,21):
            raise ValueError('expected PowerPC64 executable')
        off = struct.unpack_from('>Q', data, 32)[0]
        size, count = struct.unpack_from('>HH', data, 54)
        if size != 56 or not 1 <= count <= 32 or off + size * count > min(len(data),0x10000):
            raise ValueError('invalid program headers')
        self.ph = [struct.unpack_from('>II6Q', data, off + i * size) for i in range(count)]
        self.loads = [p for p in self.ph if p[0] == 1 and p[5]]
        mapped=[p for p in self.ph if p[0]==1 and p[6]]
        if not 1<=len(mapped)<=8:
            raise ValueError('unsupported load-segment count')
        for i,p in enumerate(mapped):
            if p[2] + p[5] > len(data) or p[5] > min(p[6],0x4000000) or p[6]>0x8000000 or p[3]<0x10000 or p[3] + p[6] > 0x40000000:
                raise ValueError('invalid load segment')
            if any(p[3]<q[3]+q[6] and q[3]<p[3]+p[6] for q in mapped[:i]):
                raise ValueError('overlapping load segments')
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.entry = struct.unpack_from('>Q', data, 24)[0]
        self.toc = self.u32(self.entry + 4)

    def read(self, address, size):
        for p in self.loads:
            if p[3] <= address and address + size <= p[3] + p[5]:
                return self.data[p[2]+address-p[3]:p[2]+address-p[3]+size]
        raise ValueError(f'unmapped file address {address:08x}+{size:x}')

    def u32(self, address):
        return int.from_bytes(self.read(address, 4), 'big')

    def string(self, address, limit=128):
        out = bytearray()
        for i in range(limit):
            b = self.read(address+i, 1)[0]
            if not b:
                return out.decode('ascii', errors='replace')
            out.append(b)
        raise ValueError('unterminated string')

    def words(self, executable=False):
        for p in self.loads:
            if executable and not p[1] & 1:
                continue
            for off in range(0, p[5]-3, 4):
                yield p[3]+off, struct.unpack_from('>I', self.data, p[2]+off)[0]

    def imports(self):
        params = [p for p in self.ph if p[0] == 0x60000002]
        if len(params) != 1:
            raise ValueError('need exactly one process PRX parameter')
        a = params[0][3]
        if self.u32(a+4) != 0x1b434cec:
            raise ValueError('invalid process PRX parameter magic')
        begin, end = self.u32(a+24), self.u32(a+28)
        if begin >= end or (end-begin) % 44 or end-begin > 65536:
            raise ValueError('invalid import interval')
        result=[]
        for at in range(begin,end,44):
            h=self.read(at,44)
            if h[0] != 44:
                raise ValueError('unsupported import header shape')
            count=int.from_bytes(h[6:8],'big')
            name,ids,slots=struct.unpack_from('>III',h,16)
            if (ids|slots)&3:
                raise ValueError('unaligned import table')
            lib=self.string(name)
            if lib in ('cellUsbd','sys_io') and h[8:12]!=bytes(4):
                raise ValueError('unsupported variable/TLS imports')
            if count > 512:
                raise ValueError('oversized import table')
            for i in range(count):
                result.append(dict(library=lib,fnid=self.u32(ids+4*i),slot=slots+4*i,
                                   stub=self.u32(slots+4*i),header=at))
        return begin,end,result

    def branches(self):
        for a,w in self.words(True):
            if w >> 26 == 18 and not w & 2:
                disp=w & 0x3fffffc
                if disp & 0x2000000: disp -= 0x4000000
                yield a,a+disp,bool(w&1)


if __name__ == '__main__':
    import pathlib,sys,json
    for path in sys.argv[1:]:
        e=Elf(pathlib.Path(path).read_bytes())
        b,z,im=e.imports()
        print(path,e.sha256,hex(e.toc),hex(b),hex(z))
        print(json.dumps([x for x in im if x['library']=='cellUsbd'],indent=2))
