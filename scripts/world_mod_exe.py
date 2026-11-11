"""world_mod_exe.py -- read an installed SWOS 96/97 Mod's patched DOS exe (ENGLISH.EXE ...): its objects and
its pointers (docs/WORLD_2026_27.md 6d). The base the World 2026-27 decoders (scripts/world_mod_*.py) stand on.

The exe is DOS/4GW's Linear Executable (LE): objects of pages, and internal fixups -- every 32-bit pointer the
program holds, in code or data, is one (source object + offset -> target object + offset). The Mod's tools
(tools/patch.py and the modules it drives) add their tables to the objects and point the game at them through
such pointers, so a table is found by what points to it, never by an address: the same decoder reads 4.0.5's exe
and 4.1.1's. Standard library only.
"""
import struct


class Exe:
    def __init__(self, path_or_bytes):
        d = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, "rb").read()
        self.data = d = bytes(d)
        h = struct.unpack_from("<I", d, 0x3C)[0]
        if d[h:h + 2] != b"LE":
            raise ValueError("not an LE executable")
        u = lambda off: struct.unpack_from("<I", d, h + off)[0]
        self.npages, self.page_size, self.last_page = u(0x14), u(0x28), u(0x2C)
        obj_tab, nobj, page_map = u(0x40), u(0x44), u(0x48)
        self.fix_page_tab, self.fix_rec_tab, data_pages = h + u(0x68), h + u(0x6C), u(0x80)
        self.objs = []                          # (vsize, base, flags, first page (1-based), pages)
        for i in range(nobj):
            vs, base, fl, pi, npg, _ = struct.unpack_from("<6I", d, h + obj_tab + i * 24)
            self.objs.append((vs, base, fl, pi, npg))
        self.page_file = []
        for i in range(self.npages):
            b = d[h + page_map + i * 4:h + page_map + i * 4 + 4]
            self.page_file.append(data_pages + (((b[0] << 16) | (b[1] << 8) | b[2]) - 1) * self.page_size)
        self.obj = {}                           # object number -> its bytes (initialised pages, zero-extended)
        for n in range(1, nobj + 1):
            vs, _, _, pi, npg = self.objs[n - 1]
            out = bytearray()
            for p in range(npg):
                gp = pi - 1 + p
                size = self.last_page if gp == self.npages - 1 else self.page_size
                out += d[self.page_file[gp]:self.page_file[gp] + size]
            out += bytes(max(0, vs - len(out)))
            self.obj[n] = bytes(out)
        self.ptr = {}                           # (obj, offset) -> (target obj, target offset): 32-bit offsets
        self.refs = {}                          # (target obj, target offset) -> [(obj, offset)]
        for so, soff, typ, to, toff in self._fixups():
            if typ == 7 and toff is not None:
                self.ptr[(so, soff)] = (to, toff)
                self.refs.setdefault((to, toff), []).append((so, soff))

    def _fixups(self):
        d = self.data
        page_obj = []
        for n, (_, _, _, pi, npg) in enumerate(self.objs, 1):
            for p in range(npg):
                page_obj.append((n, p * self.page_size))
        for gp in range(self.npages):
            start, end = struct.unpack_from("<II", d, self.fix_page_tab + gp * 4)
            p, stop = self.fix_rec_tab + start, self.fix_rec_tab + end
            sobj, pbase = page_obj[gp]
            while p < stop:
                src, flg = d[p], d[p + 1]
                p += 2
                if src & 0x20:
                    cnt, srcoffs = d[p], None
                    p += 1
                else:
                    srcoffs = struct.unpack_from("<h", d, p)[0]
                    p += 2
                if flg & 0x40:
                    tobj = struct.unpack_from("<H", d, p)[0]
                    p += 2
                else:
                    tobj = d[p]
                    p += 1
                toff = None
                if src & 0xF != 2:
                    if flg & 0x10:
                        toff = struct.unpack_from("<I", d, p)[0]
                        p += 4
                    else:
                        toff = struct.unpack_from("<H", d, p)[0]
                        p += 2
                if srcoffs is None:
                    for _ in range(cnt):
                        yield sobj, pbase + struct.unpack_from("<h", d, p)[0], src & 0xF, tobj, toff
                        p += 2
                else:
                    yield sobj, pbase + srcoffs, src & 0xF, tobj, toff

    # ---- reading -------------------------------------------------------------------------------------------------
    def bytes(self, obj, off, n):
        return self.obj[obj][off:off + n]

    def u8(self, obj, off):
        return self.obj[obj][off]

    def u16(self, obj, off):
        return struct.unpack_from("<H", self.obj[obj], off)[0]

    def u32(self, obj, off):
        return struct.unpack_from("<I", self.obj[obj], off)[0]

    def cstr(self, obj, off, limit=256):
        b = self.obj[obj][off:off + limit]
        return b[:b.index(b"\0")] if b"\0" in b else b

    def target(self, obj, off):
        """The pointer at (obj, off), or None (not a fixup: no pointer, or a null one)."""
        return self.ptr.get((obj, off))

    def find_all(self, obj, needle, start=0):
        o, out = self.obj[obj], []
        i = o.find(needle, start)
        while i >= 0:
            out.append(i)
            i = o.find(needle, i + 1)
        return out


# ---- the world's clubs, as the decoders name them --------------------------------------------------------------

class World:
    """An installed Mod's World 2026-27 for the decoders: its exe, its DATA, and its clubs by every handle the
    compiled tables use -- a team word (file | ordinal << 8, or (file, ordinal)), a global team number (the record's
    own: file base + ordinal) -- each resolved to the club's pack key (the key of Davide's clubs.csv: the
    registry's, scripts/world_club_ids_2627.csv; a club the registry does not know: its name, lower case, only
    letters and digits)."""

    def __init__(self, game_or_data, exe_name="ENGLISH.EXE"):
        import glob
        import os
        import re
        import world_from_mod as wfm
        self.data = wfm.data_dir(game_or_data)
        game = os.path.dirname(self.data) if os.path.basename(self.data).upper() == "DATA" else self.data
        self.exe = Exe(os.path.join(game, exe_name))
        self.countries = {int(r["file"]): r for r in wfm.read_registry(wfm.COUNTRIES)}   # file -> code, name, json
        self.code_of = {f: r["code"] for f, r in self.countries.items()}
        reg = wfm.read_registry(wfm.CLUB_IDS)
        by_name = {(r["code"], r["file"] == "pool", r["name"]): r["key"] for r in reg}
        by_slot = {(r["json"], r["ordinal"]): r for r in reg}
        self.records = {}                       # file -> [684-byte records]
        self.key = {}                           # (file, ordinal) -> key
        self.name = {}                          # (file, ordinal) -> name
        self.where = {}                         # key -> (file, ordinal)   (per country: keys are per country)
        self.glob = {}                          # global number -> (file, ordinal)
        for f in sorted(int(m.group(1)) for x in os.listdir(self.data)
                        for m in [re.fullmatch(r"TMW5\.(\d{3})", x, re.I)] if m):
            recs = wfm.records(os.path.join(self.data, "TMW5.%03d" % f))
            self.records[f] = recs
            code = self.code_of.get(f)
            for i, r in enumerate(recs):
                nm = wfm.text(r[5:22])
                self.name[(f, i)] = nm
                g = struct.unpack_from(">H", r, 2)[0]
                if code:
                    self.glob.setdefault(g, (f, i))
                    k = by_name.get((code, False, nm))
                    if k is None:
                        row = by_slot.get((self.countries[f]["json"], str(i)))
                        k = row["key"] if row and row["name"] not in {wfm.text(x[5:22]) for x in recs} else None
                    k = k or re.sub(r"[^a-z0-9]", "", nm.lower())
                    self.key[(f, i)] = k
                    self.where[(code, k)] = (f, i)
        self.pools = {}                         # code -> [(name, key)] of RS5<file>.DAT, the off-file pool
        for x in os.listdir(self.data):
            m = re.fullmatch(r"RS5(\d{3})\.DAT", x, re.I)
            if m and int(m.group(1)) in self.code_of:
                code = self.code_of[int(m.group(1))]
                recs = wfm.records(os.path.join(self.data, x))
                self.pools[code] = [(wfm.text(r[5:22]), by_name.get((code, True, wfm.text(r[5:22])))
                                     or re.sub(r"[^a-z0-9]", "", wfm.text(r[5:22]).lower())) for r in recs]

    def word(self, w):
        """A team word (file | ordinal << 8) -> (code, key), or None."""
        f, o = w & 0xFF, w >> 8
        return (self.code_of.get(f), self.key[(f, o)]) if (f, o) in self.key else None

    def global_club(self, g):
        """A global team number -> (code, key), or None."""
        fo = self.glob.get(g)
        return (self.code_of[fo[0]], self.key[fo]) if fo else None
