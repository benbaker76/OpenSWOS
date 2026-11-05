#!/usr/bin/env python3
"""swos_team_file — read/write the SWOS bin/data team/player data files.

Covers all three record-based data files (all RNC-ProPack compressed):

  * TEAM.<country>  RNC v1: [u16 BE count] + count * 684-byte TeamFileEx records
  * *.TMD           RNC v2: bare TeamFileEx[] (no count; N = _size / 684)
                    EUROCUP(16) / EUROCWC(32) / UEFACUP(32) — the European cups
  * POOLPLYR.DAT    RNC v2: bare PoolPlayer[] (no count; 58 B each, 900 records)

A TeamFileEx is a 76-byte TeamFile header + 16 * 38-byte PlayerFile records; a
PoolPlayer is teamId + playerOrdinal + teamName[17] + an embedded PlayerFile
(see src/game_types.h).  A {country, ordinal} TeamId resolves to a team by
loading TEAM.<country> and finding the record whose teamOrdinal == ordinal.

This module is the parsing/encoding LIBRARY; `decode_data.py` is the CLI util
built on top of it.  The parse produces plain JSON-serialisable dicts and
`encode_*` is the faithful inverse, so every file round-trips
(binary -> dict -> binary) — the foundation for the data->JSON migration
(see docs/DATA_JSON.md).

RNC method (v1/v2) is auto-detected from the header.  Compression and decompression
are scripts/rnc_propack.py (beside this file; RNC_PROPACK_DIR picks another copy).
"""
import os
import sys

# ---- on-disk layout constants (src/game_types.h) ----
TEAM_REC_SIZE = 684          # sizeof(TeamFileEx)
PLAYER_REC_SIZE = 38         # sizeof(PlayerFile)
PLAYERS_PER_TEAM = 16
TEAM_NAME_OFF, TEAM_NAME_LEN = 5, 17
COACH_NAME_OFF, COACH_NAME_LEN = 36, 23
PLAYER_NAME_OFF, PLAYER_NAME_LEN = 3, 23   # 22-char name + folded NUL terminator (byte 25)

# positionAndFace high 3 bits -> position label (game_types.h)
POSITIONS = {0: "GK", 1: "RB", 2: "LB", 3: "CD", 4: "RW", 5: "LW", 6: "MF", 7: "ST"}
_POS_REV = {v: k for k, v in POSITIONS.items()}

_RNC_DIRS = [
    os.environ.get("RNC_PROPACK_DIR"),
    os.path.dirname(os.path.abspath(__file__)),
]


# ======================================================================
# RNC ProPack (v1) decompression
# ======================================================================
def _import_rnc():
    for d in _RNC_DIRS:
        if d and os.path.isfile(os.path.join(d, "rnc_propack.py")):
            if d not in sys.path:
                sys.path.insert(0, d)
            import rnc_propack
            return rnc_propack
    raise ImportError("rnc_propack.py not found. Set RNC_PROPACK_DIR.")


def _apply_method_clamps(v, method):
    """Mirror rnc_propack.main()'s per-method dict/match setup."""
    v.method = method
    if method == 2:                 # RNC\x02 (TMD, POOLPLYR.DAT)
        v.dict_size = 0x1000
        v.max_matches = 0xFF
    else:                           # RNC\x01 (TEAM.NNN)
        v.dict_size = 0x8000
        v.max_matches = 0x1000


def rnc_unpack(packed: bytes) -> bytes:
    """Decompress an RNC ProPack blob.  Method (v1/v2) is taken from the header
    byte (packed[3] & 3) so both TEAM.NNN (v1) and TMD/POOLPLYR (v2) work."""
    rnc = _import_rnc()
    v = rnc.Vars()
    v.input = bytearray(packed)
    v.file_size = len(packed)
    v.read_start_offset = v.input_offset = v.output_offset = 0
    _apply_method_clamps(v, packed[3] & 3 if len(packed) > 3 else 1)
    v.output = bytearray(rnc.MAX_BUF_SIZE)
    v.temp = bytearray(rnc.MAX_BUF_SIZE)
    err = rnc.do_unpack(v)
    if err:
        raise ValueError("RNC unpack failed (code %d)" % err)
    return bytes(v.output[: v.output_offset])


def rnc_pack(raw: bytes, method: int = 1) -> bytes:
    """Re-compress with RNC ProPack (method 1 or 2) — for writing files back."""
    rnc = _import_rnc()
    v = rnc.Vars()
    v.input = bytearray(raw)
    v.file_size = len(raw)
    v.read_start_offset = v.input_offset = v.output_offset = 0
    _apply_method_clamps(v, method)
    v.puse_mode = ord("p")
    v.output = bytearray(rnc.MAX_BUF_SIZE)
    v.temp = bytearray(rnc.MAX_BUF_SIZE)
    err = rnc.do_pack(v)
    if err:
        raise ValueError("RNC pack failed (code %d)" % err)
    return bytes(v.output[: v.output_offset])


# ======================================================================
# helpers
# ======================================================================
def _decode_str(raw: bytes) -> str:
    """Faithful name decode: bytes _up to the first NUL, latin1.  No stripping/
    filtering so the value round-trips exactly (names may keep trailing spaces;
    a name that fills the whole field has no NUL and is terminated by the next
    struct field, e.g. PlayerFile.player_name[23] -> position_and_face)."""
    return raw.split(b"\x00")[0].decode("latin1")


def _put_str(buf: bytearray, off: int, field_len: int, text: str):
    # write the full field; a shorter name leaves trailing 0s (its NUL terminator),
    # a field-filling name uses all bytes (next struct field terminates it)
    enc = text.encode("latin1", "replace")[:field_len]
    buf[off : off + len(enc)] = enc


def _s8(v: int) -> int:
    return v - 256 if v >= 128 else v


def _s16(v: int) -> int:
    return v - 65536 if v >= 0x8000 else v


# ======================================================================
# PlayerFile  <->  dict
# ======================================================================
# Keys/structure/signedness mirror save_format_json.c (emit_player_file_body /
# s_player_fields) so the .CAR and bin/data JSON share one representation and one
# C loader. The three skill nibble-pairs are SPLIT into editable halves
# (velocity/heading, tackling/control, speed/finishing); position_and_face and
# cards_injuries stay packed. Mirrors save_format_json.c s_player_fields.
def parse_player(b: bytes) -> dict:
    return {
        "nationality": _s8(b[0]),
        "cached_match_value": _s8(b[1]),   # was field_1
        "shirt_number": _s8(b[2]),
        # player_name now 23 B (3..25): the 22-char name + its NUL terminator
        # (the former separate field_19 byte, folded into the name buffer).
        "player_name": _decode_str(b[PLAYER_NAME_OFF : PLAYER_NAME_OFF + PLAYER_NAME_LEN]),
        # positionAndFace byte: position (bits 5-7), face (bits 3-4), flags (bits 0-2).
        "position":  (b[26] >> 5) & 0x07,
        "face":      (b[26] >> 3) & 0x03,
        "flags":     b[26] & 0x07,
        # cardsInjuries byte: cards (bits 0-1), injury (bits 2-4), status_bucket (bits 5-7).
        "cards":         b[27] & 0x03,
        "injury":        (b[27] >> 2) & 0x07,
        "status_bucket": (b[27] >> 5) & 0x07,
        "passing": _s8(b[28]),
        # Skill bytes 29-31 are nibble pairs (hi=first label, lo=second); split for editing.
        "velocity":  (b[29] >> 4) & 0x0F,
        "heading":   b[29] & 0x0F,
        "tackling":  (b[30] >> 4) & 0x0F,
        "control":   b[30] & 0x0F,
        "speed":     (b[31] >> 4) & 0x0F,
        "finishing": b[31] & 0x0F,
        "price": _s8(b[32]),
        "goal_stats": list(b[33:37]),
        "form": _s8(b[37]),                # was some_flag
    }


def _pack_nib(p: dict, hi_key: str, lo_key: str, packed_key: str) -> int:
    """Combine two split nibble fields into one byte (hi<<4 | lo). Falls back to a
    pre-split packed key so old dicts still encode."""
    if hi_key in p or lo_key in p:
        return ((int(p.get(hi_key, 0)) & 0x0F) << 4) | (int(p.get(lo_key, 0)) & 0x0F)
    return int(p.get(packed_key, 0)) & 0xFF


def _pack_bits(p: dict, fields, packed_key: str) -> int:
    """Combine named bit-run fields [(key, shift, value_mask), ...] into one byte. Falls
    back to a pre-split packed key so old dicts still encode."""
    if any(k in p for k, _, _ in fields):
        v = 0
        for key, shift, vmask in fields:
            v |= (int(p.get(key, 0)) & vmask) << shift
        return v & 0xFF
    return int(p.get(packed_key, 0)) & 0xFF


def encode_player(p: dict) -> bytearray:
    b = bytearray(PLAYER_REC_SIZE)
    b[0] = p["nationality"] & 0xFF
    b[1] = p["cached_match_value"] & 0xFF
    b[2] = p["shirt_number"] & 0xFF
    # 23-byte name field covers byte 25 (the NUL terminator); a <=22-char name
    # leaves it 0, so no separate field_19 write is needed.
    _put_str(b, PLAYER_NAME_OFF, PLAYER_NAME_LEN, p["player_name"])
    b[26] = _pack_bits(p, [("position", 5, 0x07), ("face", 3, 0x03), ("flags", 0, 0x07)],
                       "position_and_face")
    b[27] = _pack_bits(p, [("cards", 0, 0x03), ("injury", 2, 0x07), ("status_bucket", 5, 0x07)],
                       "cards_injuries")
    b[28] = p["passing"] & 0xFF
    b[29] = _pack_nib(p, "velocity", "heading",   "velocity_heading")
    b[30] = _pack_nib(p, "tackling", "control",   "tackling_control")
    b[31] = _pack_nib(p, "speed",    "finishing", "speed_finishing")
    b[32] = p["price"] & 0xFF
    gs = (list(p["goal_stats"]) + [0, 0, 0, 0])[:4]
    b[33:37] = bytes(x & 0xFF for x in gs)
    b[37] = p["form"] & 0xFF
    return b


# ======================================================================
# TeamFileEx  <->  dict
# ======================================================================
# Mirrors save_format_json.c emit_team_file_ex_body / s_team_file_fields:
# { "team": { header... }, "players": [16] }, kit_colors as one 10-byte array.
def parse_team(b: bytes) -> dict:
    return {
        "team": {
            "country_number": b[0],
            "team_ordinal": b[1],
            "global_team_number": _s16((b[2] << 8) | b[3]),   # int16 big-endian on disk
            "team_controls": b[4],
            "team_name": _decode_str(b[TEAM_NAME_OFF : TEAM_NAME_OFF + TEAM_NAME_LEN]),
            "writes_zero_here": b[22],
            "and_with_0xfe": b[23],
            "tactics": b[24],
            "league": b[25],
            # Kit colors as 10 named fields (primary + secondary; bytes 26-35).
            "primary_shirt_type":      b[26],
            "primary_stripes_color":   b[27],
            "primary_basic_color":     b[28],
            "primary_shorts_color":    b[29],
            "primary_socks_color":     b[30],
            "secondary_shirt_type":    b[31],
            "secondary_stripes_color": b[32],
            "secondary_basic_color":   b[33],
            "secondary_shorts_color":  b[34],
            "secondary_socks_color":   b[35],
            "coach_name": _decode_str(b[COACH_NAME_OFF : COACH_NAME_OFF + COACH_NAME_LEN]),
            "player_values_dirty": b[59],   # was some_flag
            "player_numbers": list(b[60:76]),
        },
        "players": [
            parse_player(b[76 + i * PLAYER_REC_SIZE : 76 + (i + 1) * PLAYER_REC_SIZE])
            for i in range(PLAYERS_PER_TEAM)
        ],
    }


def encode_team(t: dict) -> bytearray:
    b = bytearray(TEAM_REC_SIZE)
    h = t["team"]
    b[0] = h["country_number"] & 0xFF
    b[1] = h["team_ordinal"] & 0xFF
    gtn = h["global_team_number"] & 0xFFFF
    b[2], b[3] = (gtn >> 8) & 0xFF, gtn & 0xFF
    b[4] = h["team_controls"] & 0xFF
    name = h["team_name"]
    if len(name.encode("latin1", "replace")) > TEAM_NAME_LEN - 1:
        # 16 letters and a NUL: the byte after the field is the team's goal count
        # (writes_zero_here), so a 17-letter name is out of the format (none of 96/97's is)
        sys.stderr.write("swos_team_file: team name %r is over %d letters, cut\n" % (name, TEAM_NAME_LEN - 1))
        name = name.encode("latin1", "replace")[:TEAM_NAME_LEN - 1].decode("latin1")
    _put_str(b, TEAM_NAME_OFF, TEAM_NAME_LEN, name)
    b[22] = h["writes_zero_here"] & 0xFF
    b[23] = h["and_with_0xfe"] & 0xFF
    b[24] = h["tactics"] & 0xFF
    b[25] = h["league"] & 0xFF
    if "kit_colors" in h:   # back-compat: pre-split flat 10-byte array
        kit = (list(h["kit_colors"]) + [0] * 10)[:10]
    else:
        kit = [h.get(k, 0) for k in (
            "primary_shirt_type", "primary_stripes_color", "primary_basic_color",
            "primary_shorts_color", "primary_socks_color",
            "secondary_shirt_type", "secondary_stripes_color", "secondary_basic_color",
            "secondary_shorts_color", "secondary_socks_color")]
    b[26:36] = bytes(int(x) & 0xFF for x in kit)
    _put_str(b, COACH_NAME_OFF, COACH_NAME_LEN, h["coach_name"])
    b[59] = h["player_values_dirty"] & 0xFF
    pn = (list(h["player_numbers"]) + [0] * 16)[:16]
    b[60:76] = bytes(x & 0xFF for x in pn)
    for i, p in enumerate(t["players"][:16]):
        b[76 + i * PLAYER_REC_SIZE : 76 + (i + 1) * PLAYER_REC_SIZE] = encode_player(p)
    return b


# ======================================================================
# blob (count + records)  <->  list[dict]
# ======================================================================
def parse_blob(blob: bytes) -> list:
    count = (blob[0] << 8) | blob[1]
    teams = []
    for i in range(count):
        base = 2 + i * TEAM_REC_SIZE
        rec = blob[base : base + TEAM_REC_SIZE]
        if len(rec) < TEAM_REC_SIZE:
            break
        teams.append(parse_team(rec))
    return teams


def encode_blob(teams: list) -> bytes:
    out = bytearray(2)
    out[0], out[1] = (len(teams) >> 8) & 0xFF, len(teams) & 0xFF
    for t in teams:
        out += encode_team(t)
    return bytes(out)


# ======================================================================
# TMD (EUROCUP/EUROCWC/UEFACUP)  —  bare TeamFileEx[] array, no count header
# (loaded into _tmdFileBuffer = TeamFileEx[80]; count = filesize / 684)
# ======================================================================
def parse_tmd_blob(blob: bytes) -> list:
    n = len(blob) // TEAM_REC_SIZE
    return [parse_team(blob[i * TEAM_REC_SIZE : (i + 1) * TEAM_REC_SIZE]) for i in range(n)]


def encode_tmd_blob(teams: list) -> bytes:
    out = bytearray()
    for t in teams:
        out += encode_team(t)
    return bytes(out)


# ======================================================================
# POOLPLYR.DAT  —  PoolPlayer[] array, 58 B each, no count header
# PoolPlayer = teamId(BE u16) + playerOrdinal + teamName[17] + PlayerFile (game_types.h)
# ======================================================================
POOLPLAYER_REC_SIZE = 58
POOL_TEAMNAME_OFF, POOL_TEAMNAME_LEN = 3, 17


# Mirrors save_format_json.c emit_market_player_body naming (team_id / player_ordinal
# / player), with PoolPlayer's team_name[17] where MarketPlayer has padding.
def parse_pool_player(b: bytes) -> dict:
    return {
        "team_id": _s16((b[0] << 8) | b[1]),   # uint16 big-endian on disk
        "player_ordinal": _s8(b[2]),
        "team_name": _decode_str(b[POOL_TEAMNAME_OFF : POOL_TEAMNAME_OFF + POOL_TEAMNAME_LEN]),
        "player": parse_player(b[20:58]),
    }


def encode_pool_player(pp: dict) -> bytearray:
    b = bytearray(POOLPLAYER_REC_SIZE)
    tid = pp["team_id"] & 0xFFFF
    b[0], b[1] = (tid >> 8) & 0xFF, tid & 0xFF
    b[2] = pp["player_ordinal"] & 0xFF
    _put_str(b, POOL_TEAMNAME_OFF, POOL_TEAMNAME_LEN, pp["team_name"])
    b[20:58] = encode_player(pp["player"])
    return b


def parse_poolplyr_blob(blob: bytes) -> list:
    n = len(blob) // POOLPLAYER_REC_SIZE
    return [parse_pool_player(blob[i * POOLPLAYER_REC_SIZE : (i + 1) * POOLPLAYER_REC_SIZE])
            for i in range(n)]


def encode_poolplyr_blob(players: list) -> bytes:
    out = bytearray()
    for pp in players:
        out += encode_pool_player(pp)
    return bytes(out)


# ======================================================================
# file-level API
# ======================================================================
def _data_dir() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(os.path.dirname(here), "bin", "data")


def team_file_path(country) -> str:
    return os.path.join(_data_dir(), "TEAM.%03d" % int(country))


def _read_blob(target) -> bytes:
    """Accept a country number (int/str of digits) or a path; return the decompressed blob."""
    if isinstance(target, int) or (isinstance(target, str) and target.isdigit()):
        path = team_file_path(int(target))
    else:
        path = target
    raw = open(path, "rb").read()
    return rnc_unpack(raw) if raw[:3] == b"RNC" else raw


def load(target) -> list:
    """Load a TEAM file (country number or path) -> list of team dicts."""
    return parse_blob(_read_blob(target))


def file_type(target) -> str:
    """Classify a bin/data target: 'team' (TEAM.NNN or a country number),
    'tmd' (*.TMD), or 'pool' (POOLPLYR.DAT).  None if unrecognised."""
    if isinstance(target, int) or (isinstance(target, str) and target.isdigit()):
        return "team"
    name = os.path.basename(str(target)).upper()
    if name.startswith("TEAM."):
        return "team"
    if name.endswith(".TMD"):
        return "tmd"
    if name.endswith("POOLPLYR.DAT"):
        return "pool"
    return None


def file_to_dict(target) -> dict:
    """Whole-file JSON-ready dict, dispatched by file type."""
    ft = file_type(target)
    blob = _read_blob(target)
    if ft == "team":
        teams = parse_blob(blob)
        country = teams[0]["team"]["country_number"] if teams else None
        return {"type": "team", "country": country,
                "country_name": country_name(country) if country is not None else None,
                "count": len(teams), "teams": teams}
    if ft == "tmd":
        teams = parse_tmd_blob(blob)
        return {"type": "tmd", "file": os.path.basename(str(target)),
                "count": len(teams), "teams": teams}
    if ft == "pool":
        players = parse_poolplyr_blob(blob)
        return {"type": "pool", "file": os.path.basename(str(target)),
                "count": len(players), "players": players}
    raise ValueError("unknown data file type: %s" % target)


def dict_to_blob(d) -> bytes:
    """Inverse of file_to_dict: a JSON-ready dict -> the DECOMPRESSED binary blob."""
    t = d["type"]
    if t == "team":
        return encode_blob(d["teams"])
    if t == "tmd":
        return encode_tmd_blob(d["teams"])
    if t == "pool":
        return encode_poolplyr_blob(d["players"])
    raise ValueError("unknown data dict type: %r" % t)


def dict_rnc_method(d) -> int:
    """TEAM.NNN is RNC v1; *.TMD and POOLPLYR.DAT are RNC v2."""
    return 1 if d["type"] == "team" else 2


def dict_filename(d) -> str:
    """The on-disk filename a dict regenerates (TEAM.NNN from the country number;
    tmd/pool keep their original filename)."""
    if d["type"] == "team":
        return "TEAM.%03d" % int(d["country"])
    return d["file"]


def reencode(target) -> bytes:
    """Decompressed-blob round-trip: parse then re-encode (no RNC), by file type."""
    ft = file_type(target)
    blob = _read_blob(target)
    if ft == "team":
        return encode_blob(parse_blob(blob))
    if ft == "tmd":
        return encode_tmd_blob(parse_tmd_blob(blob))
    if ft == "pool":
        return encode_poolplyr_blob(parse_poolplyr_blob(blob))
    raise ValueError("unknown data file type: %s" % target)


_names_cache = {}


def team_names(country) -> dict:
    """{teamOrdinal: name} for a country number (cached)."""
    country = int(country)
    if country not in _names_cache:
        _names_cache[country] = {t["team"]["team_ordinal"]: t["team"]["team_name"]
                                 for t in load(country)}
    return _names_cache[country]


def resolve(country, ordinal) -> str:
    """Resolve a {country, ordinal} TeamId to a name ('' if unknown)."""
    return team_names(country).get(int(ordinal), "")


# ---- country number -> name (parsed from game_types.h CN_* defines) ----
_country_cache = None


def _load_country_map() -> dict:
    import re
    global _country_cache
    if _country_cache is None:
        _country_cache = {}
        repo = os.path.dirname(os.path.dirname(_data_dir()))   # .../<repo>/bin/data -> <repo>
        hdr = os.path.join(repo, "include", "game_types.h")
        try:
            for line in open(hdr):
                m = re.match(r"\s*#define\s+CN_([A-Z_]+)\s+(\d+)\b", line)
                if m:
                    _country_cache[int(m.group(2))] = m.group(1)
        except OSError:
            pass
    return _country_cache


def country_name(num) -> str:
    """Human country name (e.g. 'SCOTLAND') or 'COUNTRY_N' fallback."""
    return _load_country_map().get(int(num), "COUNTRY_%d" % int(num))


def country_define(num) -> str:
    """The CN_* macro for a country number (e.g. 'CN_SCOTLAND')."""
    nm = _load_country_map().get(int(num))
    return "CN_" + nm if nm else None
