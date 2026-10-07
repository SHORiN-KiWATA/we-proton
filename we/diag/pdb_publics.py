#!/usr/bin/env python3
"""Resolve RVAs to function names using the public symbols of a PDB.

  pdb_publics.py --pdb libcef.dll.pdb --rva 0x3ce3c85 --rva 0x1260503 ...
  pdb_publics.py --pdb libcef.dll.pdb --guid

Only stdlib. Reads the MSF container, the DBI stream (for the symbol record
stream and the section headers) and S_PUB32 records.
"""
import argparse
import bisect
import struct
import sys

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"
S_PUB32 = 0x110E


class PDB:
    def __init__(self, path):
        self.f = open(path, "rb")
        sb = self.f.read(56)
        if sb[:32] != MSF_MAGIC:
            sys.exit("not an MSF 7.00 PDB")
        self.bs, _, self.nblocks, dir_size, _, dir_map = struct.unpack_from("<6I", sb, 32)
        nb = (dir_size + self.bs - 1) // self.bs
        # the block map holds the directory's block numbers (contiguous if it needs more than one block)
        raw = b"".join(self._block(dir_map + i) for i in range((nb * 4 + self.bs - 1) // self.bs))
        dir_blocks = struct.unpack_from(f"<{nb}I", raw)
        d = b"".join(self._block(b) for b in dir_blocks)[:dir_size]
        n = struct.unpack_from("<I", d)[0]
        sizes = struct.unpack_from(f"<{n}I", d, 4)
        pos = 4 + 4 * n
        self.streams = []
        for s in sizes:
            s = 0 if s == 0xFFFFFFFF else s
            cnt = (s + self.bs - 1) // self.bs
            self.streams.append((s, struct.unpack_from(f"<{cnt}I", d, pos)))
            pos += 4 * cnt

    def _block(self, i):
        self.f.seek(i * self.bs)
        return self.f.read(self.bs)

    def stream(self, i):
        size, blocks = self.streams[i]
        return b"".join(self._block(b) for b in blocks)[:size]

    def guid(self):
        info = self.stream(1)
        import uuid
        age = struct.unpack_from("<I", info, 8)[0]
        g = uuid.UUID(bytes_le=info[12:28])
        return str(g).replace("-", "").upper() + format(age, "X")

    def publics(self):
        dbi = self.stream(3)
        sym_stream = struct.unpack_from("<H", dbi, 20)[0]
        modi, secc, secm, srcinfo, tsm, mfc, dbg, ec = struct.unpack_from("<8i", dbi, 24)
        dbg_off = 64 + modi + secc + secm + srcinfo + tsm + ec
        section_hdr_stream = struct.unpack_from("<H", dbi, dbg_off + 5 * 2)[0]
        sh = self.stream(section_hdr_stream)
        sect_va = [struct.unpack_from("<I", sh, i * 40 + 12)[0] for i in range(len(sh) // 40)]
        recs = self.stream(sym_stream)
        out = []
        pos = 0
        while pos + 4 <= len(recs):
            reclen, rectype = struct.unpack_from("<HH", recs, pos)
            if rectype == S_PUB32:
                _, off, seg = struct.unpack_from("<IIH", recs, pos + 4)
                name = recs[pos + 14:recs.index(b"\0", pos + 14)].decode("utf-8", "replace")
                if 1 <= seg <= len(sect_va):
                    out.append((sect_va[seg - 1] + off, name))
            pos += reclen + 2
        out.sort()
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pdb", required=True)
    ap.add_argument("--rva", action="append", default=[])
    ap.add_argument("--guid", action="store_true")
    a = ap.parse_args()
    pdb = PDB(a.pdb)
    if a.guid:
        print(pdb.guid())
    if a.rva:
        pubs = pdb.publics()
        addrs = [p[0] for p in pubs]
        for r in a.rva:
            v = int(r, 16)
            i = bisect.bisect_right(addrs, v) - 1
            print(f"{v:#x}  " + (f"{pubs[i][1]}+{v - pubs[i][0]:#x}" if i >= 0 else "?"))


if __name__ == "__main__":
    main()
