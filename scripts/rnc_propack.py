#!/usr/bin/env python3
#
# rnc_propack.py -- RNC ProPack compression and decompression (methods 1 and 2), in Python.
#
# Copyright (c) 2025 Ben Baker. A port of Rob Northen Computing's RNC ProPack
# (https://aminet.net/package/util/pack/RNC_ProPack). Part of OpenSWOS (GPL v3, LICENSE).
#
# scripts/swos_team_file.py packs and unpacks the game's team files with it (world_import.py's
# data/TEAM.nnn).
import os
import sys

# ----------------- Types & Constants -----------------

# We just use Python ints; mask when needed.
def u8(x): return x & 0xFF
def u16(x): return x & 0xFFFF
def u32(x): return x & 0xFFFFFFFF

RNC_SIGN = 0x524E43  # "RNC"
RNC_HEADER_SIZE = 0x12
MAX_BUF_SIZE = 0x1E00000

crc_table = [
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
]

match_count_bits_table = [0x00, 0x0E, 0x08, 0x0A, 0x12, 0x13, 0x16]
match_count_bits_count_table = [0, 4, 4, 4, 5, 5, 5]
match_offset_bits_table = [0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F,
                           0x28, 0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D]
match_offset_bits_count_table = [1, 3, 4, 4, 5, 5, 5, 5,
                                 6, 6, 6, 6, 6, 6, 6, 6]


class HufTable:
    __slots__ = ("l1", "l2", "l3", "bit_depth")

    def __init__(self):
        self.l1 = 0
        self.l2 = 0xFFFF
        self.l3 = 0
        self.bit_depth = 0


class Vars:
    def __init__(self):
        # top-level config
        self.max_matches = 0x1000
        self.enc_key = 0
        self.pack_block_size = 0x3000
        self.dict_size = 0xFFFF
        self.method = 1
        self.puse_mode = ord('p')  # we still store as int/char-ish in some places

        self.input_size = 0
        self.file_size = 0

        # inner
        self.bytes_left = 0
        self.packed_size = 0
        self.processed_size = 0
        self.v7 = 0
        self.pack_block_pos = 0
        self.pack_token = 0
        self.bit_count = 0
        self.v11 = 0
        self.last_min_offset = 0
        self.v17 = 0
        self.pack_block_left_size = 0
        self.match_count = 0
        self.match_offset = 0
        self.v20 = 0
        self.v21 = 0
        self.bit_buffer = 0

        self.unpacked_size = 0
        self.rnc_data_size = 0
        self.unpacked_crc = 0
        self.unpacked_crc_real = 0
        self.packed_crc = 0
        self.leeway = 0
        self.chunks_count = 0

        self.mem1 = None       # bytearray
        self.pack_block_start = None  # index into mem1
        self.pack_block_max = None    # index into mem1
        self.pack_block_end = None    # index into mem1
        self.mem2 = None       # list of uint16
        self.mem3 = None
        self.mem4 = None
        self.mem5 = None

        self.decoded = None    # bytearray
        self.window = None     # index into decoded

        self.read_start_offset = 0
        self.write_start_offset = 0
        self.input = None      # bytearray
        self.output = None     # bytearray
        self.temp = None       # bytearray
        self.input_offset = 0
        self.output_offset = 0
        self.temp_offset = 0

        self.tmp_crc_data = bytearray(2048)
        self.raw_table = [HufTable() for _ in range(16)]
        self.pos_table = [HufTable() for _ in range(16)]
        self.len_table = [HufTable() for _ in range(16)]


# ----------------- Helper functions -----------------

def crc_block(buf, offset, size):
    crc = 0
    for i in range(size):
        b = buf[offset + i]
        crc ^= b
        crc = (crc >> 8) ^ crc_table[crc & 0xFF]
    return crc & 0xFFFF


def ror_w(x):
    x &= 0xFFFF
    if x & 1:
        x = 0x8000 | (x >> 1)
    else:
        x >>= 1
    return x & 0xFFFF


def bits_count(value):
    count = 1
    while value >> 1:
        value >>= 1
        count += 1
    return count


def inverse_bits(value, count):
    i = 0
    while count:
        i <<= 1
        if value & 1:
            i |= 1
        value >>= 1
        count -= 1
    return i


def memmove_bytes(buf, dst, src, size):
    if size <= 0:
        return
    tmp = buf[src:src + size]
    buf[dst:dst + size] = tmp


# ------------- Byte & word helpers (BE) -------------

def read_byte_from(v, buf_name):
    if buf_name == "input":
        b = v.input[v.input_offset]
        v.input_offset += 1
        return b
    elif buf_name == "output":
        b = v.output[v.output_offset]
        v.output_offset += 1
        return b
    elif buf_name == "temp":
        b = v.temp[v.temp_offset]
        v.temp_offset += 1
        return b
    else:
        raise ValueError("unknown buffer")


def write_byte_to(v, buf_name, b):
    b &= 0xFF
    if buf_name == "output":
        v.output[v.output_offset] = b
        v.output_offset += 1
    elif buf_name == "temp":
        v.temp[v.temp_offset] = b
        v.temp_offset += 1
    else:
        raise ValueError("unknown buffer")


def read_word_be_buf(buf, offset):
    b1 = buf[offset]
    b2 = buf[offset + 1]
    return ((b1 << 8) | b2) & 0xFFFF


def read_word_be_from_input(v):
    b1 = v.input[v.input_offset]
    b2 = v.input[v.input_offset + 1]
    v.input_offset += 2
    return ((b1 << 8) | b2) & 0xFFFF


def write_word_be_to_output(v, val):
    val &= 0xFFFF
    v.output[v.output_offset] = (val >> 8) & 0xFF
    v.output[v.output_offset + 1] = val & 0xFF
    v.output_offset += 2


def write_word_be_to(v, buf_name, val):
    val &= 0xFFFF
    if buf_name == "output":
        write_word_be_to_output(v, val)
    elif buf_name == "temp":
        v.temp[v.temp_offset] = (val >> 8) & 0xFF
        v.temp[v.temp_offset + 1] = val & 0xFF
        v.temp_offset += 2
    else:
        raise ValueError("unknown buffer")


def read_dword_be(v):
    if v.input_offset + 3 > v.file_size:
        print("Corrupt file.")
        sys.exit(1)
    w1 = read_word_be_from_input(v)
    w2 = read_word_be_from_input(v)
    return ((w1 << 16) | w2) & 0xFFFFFFFF


def write_dword_be_to_output(v, val):
    val &= 0xFFFFFFFF
    high = (val >> 16) & 0xFFFF
    low = val & 0xFFFF
    write_word_be_to_output(v, high)
    write_word_be_to_output(v, low)


def read_buf(dest, dest_pos, source, src_offset, size):
    dest[dest_pos:dest_pos + size] = source[src_offset:src_offset + size]
    return src_offset + size


def write_buf(dest, dest_pos, source, size):
    dest[dest_pos:dest_pos + size] = source[0:size]


# ----------------- Dict init & CRC updates -----------------

def init_dicts(v):
    dict_size = v.dict_size & 0xFFFF
    # mem2/mem3: 0x800 * 0x10 = 0x8000 entries; but array is 0x8000, masked with 0x7FFF.
    for i in range(0x800):
        base = i * 0x10
        for j in range(0x10):
            v.mem2[base + j] = dict_size
            v.mem3[base + j] = dict_size
    for i in range(dict_size):
        idx = i & 0x7FFF
        v.mem5[idx] = 0
        v.mem4[idx] = i
    v.last_min_offset = 0


def update_packed_crc(v, b):
    crc = v.packed_crc
    v.packed_crc = crc_table[(crc & 0xFF) ^ (b & 0xFF)] ^ (crc >> 8)
    v.packed_size += 1
    v.packed_crc &= 0xFFFF


def update_unpacked_crc(v, b):
    crc = v.unpacked_crc
    v.unpacked_crc = crc_table[(crc & 0xFF) ^ (b & 0xFF)] ^ (crc >> 8)
    v.unpacked_crc &= 0xFFFF
    v.processed_size += 1


def write_to_output(v, b):
    if v.packed_size >= (v.file_size - RNC_HEADER_SIZE):
        return
    v.output[v.output_offset] = b & 0xFF
    v.output_offset += 1
    update_packed_crc(v, b)


def read_from_input(v):
    b = v.input[v.input_offset]
    v.input_offset += 1
    update_unpacked_crc(v, b)
    return b


# ----------------- Bit writers (packing) -----------------

def write_bits_m2(v, value, count):
    mask = 1 << (count - 1)
    while count:
        v.pack_token <<= 1
        if value & mask:
            v.pack_token += 1
        mask >>= 1
        v.bit_count += 1

        if v.bit_count == 8:
            write_to_output(v, v.pack_token & 0xFF)
            for i in range(v.v11):
                write_to_output(v, v.tmp_crc_data[i])
            v.v11 = 0
            if (v.processed_size > v.packed_size) and (v.processed_size - v.packed_size > v.leeway):
                v.leeway = v.processed_size - v.packed_size
            v.bit_count = 0
            v.pack_token = 0
        count -= 1


def write_bits_m1(v, value, count):
    while count:
        v.pack_token >>= 1
        if value & 1:
            v.pack_token |= 0x8000
        value >>= 1
        v.bit_count += 1
        if v.bit_count == 16:
            write_to_output(v, v.pack_token & 0xFF)
            write_to_output(v, (v.pack_token >> 8) & 0xFF)
            for i in range(v.v11):
                write_to_output(v, v.tmp_crc_data[i])
            v.v11 = 0
            if (v.processed_size > v.packed_size) and (v.processed_size - v.packed_size > v.leeway):
                v.leeway = v.processed_size - v.packed_size
            v.bit_count = 0
            v.pack_token = 0
        count -= 1


def write_bits(v, bits, count):
    if v.method == 2:
        write_bits_m2(v, bits, count)
    else:
        write_bits_m1(v, bits, count)


# ----------------- Match finding (packing) -----------------

def find_matches(v):
    v.match_count = 1
    v.match_offset = 0

    match_offset = 1
    while (match_offset < (v.pack_block_end - v.pack_block_start) and
           v.mem1[v.pack_block_start + match_offset] == v.mem1[v.pack_block_start]):
        match_offset += 1

    first_word = read_word_be_buf(v.mem1, v.pack_block_start)
    offset = v.mem2[first_word & 0x7FFF]

    while True:
        if offset == v.dict_size:
            if v.match_count == 2 and v.match_offset > 0x100:
                v.match_count = 1
                v.match_offset = 0
            break

        restore = v.mem4[offset & 0x7FFF]
        min_offset = v.last_min_offset
        if min_offset <= offset:
            min_offset += v.dict_size
        min_offset -= offset

        if read_word_be_buf(v.mem1, v.pack_block_start - min_offset) == \
           read_word_be_buf(v.mem1, v.pack_block_start):
            max_count = v.mem5[offset & 0x7FFF]
            if max_count <= min_offset:
                if max_count > match_offset:
                    min_offset = min_offset - max_count + match_offset
                    max_count = match_offset
                max_size = v.pack_block_end - v.pack_block_start
                if max_count == match_offset:
                    while (max_count < max_size and
                           v.mem1[v.pack_block_start + max_count] ==
                           v.mem1[v.pack_block_start + max_count - min_offset]):
                        max_count += 1
            else:
                min_offset = 1
                max_count = match_offset

            if max_count > v.max_matches:
                max_count = v.max_matches

            if max_count >= v.match_count:
                v.match_count = max_count
                v.match_offset = min_offset

        offset = restore

    return 0


def find_and_check_matches(v):
    find_matches(v)
    if v.match_count >= 2:
        if v.pack_block_max - v.pack_block_start >= 3:
            count = v.match_count
            offset = v.match_offset
            min_offset = v.last_min_offset

            v.last_min_offset = (v.last_min_offset + 1) % v.dict_size
            v.pack_block_start += 1
            find_matches(v)
            v.pack_block_start -= 1
            v.last_min_offset = min_offset

            if count < v.match_count:
                count = 1
                offset = 0

            v.match_count = count
            v.match_offset = offset


def update_bits_table(v, data, bits):
    if bits <= 1:
        data[bits].l1 += 1
    else:
        data[bits_count(bits)].l1 += 1
    # write to temp
    write_word_be_to(v, "temp", bits)


def encode_matches(v, w):
    while True:
        restore = v.mem4[v.last_min_offset & 0x7FFF]
        v.mem4[v.last_min_offset & 0x7FFF] = v.dict_size
        if restore != v.last_min_offset:
            buffer_word = read_word_be_buf(v.mem1, v.pack_block_start - v.dict_size)
            v.mem2[buffer_word & 0x7FFF] = restore
            if v.dict_size == restore:
                v.mem3[buffer_word & 0x7FFF] = v.dict_size

        buffer_word = read_word_be_buf(v.mem1, v.pack_block_start)
        if v.mem2[buffer_word & 0x7FFF] == v.dict_size:
            v.mem2[buffer_word & 0x7FFF] = v.last_min_offset
        else:
            v.mem4[v.mem3[buffer_word & 0x7FFF] & 0x7FFF] = v.last_min_offset
        v.mem3[buffer_word & 0x7FFF] = v.last_min_offset

        count = 1
        while (count < (v.pack_block_end - v.pack_block_start) and
               v.mem1[v.pack_block_start + count] == v.mem1[v.pack_block_start]):
            count += 1
        v.mem5[v.last_min_offset & 0x7FFF] = count

        while True:
            v.last_min_offset = (v.last_min_offset + 1) % v.dict_size
            v.pack_block_start += 1
            w -= 1
            if w == 0:
                return
            count -= 1
            if count <= 1:
                break
            v.mem5[v.last_min_offset & 0x7FFF] = count
            if v.last_min_offset != v.mem4[v.last_min_offset & 0x7FFF]:
                restore = v.mem4[v.last_min_offset & 0x7FFF]
                v.mem4[v.last_min_offset & 0x7FFF] = v.last_min_offset
                buffer_word = read_word_be_buf(v.mem1, v.pack_block_start - v.dict_size)
                v.mem2[buffer_word & 0x7FFF] = restore
                if v.dict_size == restore:
                    v.mem3[buffer_word & 0x7FFF] = v.dict_size


def proc_6(v):
    v.v17 = 0
    v.pack_block_left_size = v.pack_block_size
    v.input_offset = v.read_start_offset + v.v7 + v.pack_block_pos
    v.temp_offset = 0
    data_length = 0

    while v.bytes_left or v.pack_block_pos:
        size_to_read = 0xFFFF - v.dict_size - v.pack_block_pos
        if v.bytes_left < size_to_read:
            size_to_read = v.bytes_left

        v.pack_block_start = v.dict_size
        v.input_offset = read_buf(v.mem1, v.pack_block_start + v.pack_block_pos,
                                  v.input, v.input_offset, size_to_read)
        v.bytes_left -= size_to_read
        v.pack_block_pos += size_to_read

        v.pack_block_max = v.pack_block_start + v.pack_block_pos
        v.pack_block_end = v.pack_block_start + v.pack_block_pos

        if v.pack_block_left_size < v.pack_block_pos:
            v.pack_block_max = v.pack_block_start + v.pack_block_left_size

        while (v.pack_block_start < v.pack_block_max - 1) and (v.v17 < 0xFFFE):
            find_and_check_matches(v)
            if v.match_count >= 2:
                if v.pack_block_start + v.match_count <= v.pack_block_max:
                    update_bits_table(v, v.raw_table, data_length)
                    update_bits_table(v, v.pos_table, v.match_count - 2)
                    update_bits_table(v, v.len_table, v.match_offset - 1)

                    encode_matches(v, v.match_count)
                    v.v17 += 1
                    data_length = 0
                else:
                    if v.v17 != 0:
                        break
                    v.match_count = v.pack_block_max - v.pack_block_start
            else:
                encode_matches(v, 1)
                data_length += 1

        v.pack_block_pos = v.pack_block_end - v.pack_block_start
        memmove_bytes(v.mem1, 0, v.pack_block_start - v.dict_size,
                      v.dict_size + v.pack_block_pos)

        if ((v.pack_block_max < v.pack_block_end) or
            ((v.pack_block_max == v.pack_block_end) and not v.bytes_left) or
                v.v17 == 0xFFFE):
            break

        v.pack_block_left_size -= (v.pack_block_start - v.dict_size)

    if (v.pack_block_max == v.pack_block_end and not v.bytes_left and
            v.v17 != 0xFFFE):
        data_length += v.pack_block_pos

    update_bits_table(v, v.raw_table, data_length)
    v.v17 += 1
    v.temp_offset = 0


def update_tmp_crc_data(v, b):
    if v.bit_count:
        v.tmp_crc_data[v.v11] = b & 0xFF
        v.v11 += 1
    else:
        write_to_output(v, b)


def encode_matches_count(v, count):
    while count > 0:
        if count >= 12:
            if count & 3:
                write_bits_m2(v, 0, 1)
                b = read_from_input(v)
                update_tmp_crc_data(v, v.enc_key ^ b)
                count -= 1
            else:
                write_bits_m2(v, 0x17, 5)
                if count >= 72:
                    write_bits_m2(v, 0xF, 4)
                    for _ in range(72):
                        b = read_from_input(v)
                        update_tmp_crc_data(v, v.enc_key ^ b)
                    count -= 72
                else:
                    write_bits_m2(v, (count - 12) >> 2, 4)
                    while count:
                        b = read_from_input(v)
                        update_tmp_crc_data(v, v.enc_key ^ b)
                        count -= 1
            v.enc_key = ror_w(v.enc_key)
        else:
            while count:
                write_bits_m2(v, 0, 1)
                b = read_from_input(v)
                update_tmp_crc_data(v, v.enc_key ^ b)
                v.enc_key = ror_w(v.enc_key)
                count -= 1


def clear_table(data, count):
    for i in range(count):
        data[i].l1 = 0
        data[i].l2 = 0xFFFF
        data[i].l3 = 0
        data[i].bit_depth = 0


def proc_17(v, data, count):
    d6 = 0xFFFFFFFF
    d5 = 0xFFFFFFFF
    i = 0
    while i < count:
        if data[i].l1:
            if data[i].l1 < d5:
                d6 = d5
                v.v21 = v.v20
                d5 = data[i].l1
                v.v20 = i
            elif data[i].l1 < d6:
                d6 = data[i].l1
                v.v21 = i
        i += 1
    return (d5 != 0xFFFFFFFF) and (d6 != 0xFFFFFFFF)


def proc_20(data, count):
    val = 0
    div = 0x80000000
    bits = 1
    while bits <= 16:
        i = 0
        while True:
            if i >= count:
                bits += 1
                div >>= 1
                break
            if data[i].bit_depth == bits:
                data[i].l3 = inverse_bits(val // div, bits)
                val += div
            i += 1


def proc_16(v, data, count):
    d4 = 0
    ve = 0
    for i in range(count):
        if data[i].l1:
            d4 += 1
            ve = i
    if not d4:
        return
    if d4 == 1:
        data[ve].bit_depth += 1
        return
    while proc_17(v, data, count):
        data[v.v20].l1 += data[v.v21].l1
        data[v.v21].l1 = 0
        data[v.v20].bit_depth += 1
        while data[v.v20].l2 != 0xFFFF:
            v.v20 = data[v.v20].l2
            data[v.v20].bit_depth += 1
        data[v.v20].l2 = v.v21
        data[v.v21].bit_depth += 1
        while data[v.v21].l2 != 0xFFFF:
            v.v21 = data[v.v21].l2
            data[v.v21].bit_depth += 1
    proc_20(data, count)


def proc_18(v, data, count):
    cnt = count
    while cnt and not data[cnt - 1].bit_depth:
        cnt -= 1
        count -= 1
    write_bits_m1(v, count, 5)
    for i in range(count):
        write_bits_m1(v, data[i].bit_depth, 4)


def proc_19(v, data, count):
    if count > 1:
        bits = bits_count(count)
    else:
        bits = count
    write_bits_m1(v, data[bits].l3, data[bits].bit_depth)
    if bits > 1:
        write_bits_m1(v, count - (1 << (bits - 1)), bits - 1)


def compress_data_2(v):
    src_offset = v.read_start_offset
    while v.v7 < v.unpacked_size:
        proc_6(v)
        v.input_offset = src_offset
        while v.v17:
            v.v17 -= 1
            data_length = read_word_be_buf(v.temp, v.temp_offset)
            v.temp_offset += 2
            v.v7 += data_length
            encode_matches_count(v, data_length)
            if v.v17:
                v.match_count = read_word_be_buf(v.temp, v.temp_offset)
                v.temp_offset += 2
                v.match_offset = read_word_be_buf(v.temp, v.temp_offset)
                v.temp_offset += 2
                if v.match_count:
                    if v.match_count >= 7:
                        write_bits_m2(v, 0xF, 4)
                        update_tmp_crc_data(v, (v.match_count - 6) & 0xFF)
                    else:
                        write_bits_m2(v,
                                      match_count_bits_table[v.match_count],
                                      match_count_bits_count_table[v.match_count])
                    write_bits_m2(v,
                                  match_offset_bits_table[v.match_offset >> 8],
                                  match_offset_bits_count_table[v.match_offset >> 8])
                else:
                    write_bits_m2(v, 6, 3)
                update_tmp_crc_data(v, v.match_offset & 0xFF)
                v.match_count += 2
                v.v7 += v.match_count
                while v.match_count:
                    read_from_input(v)
                    v.match_count -= 1
        write_bits_m2(v, 0xF, 4)
        update_tmp_crc_data(v, 0)
        if v.v7 >= v.unpacked_size:
            write_bits_m2(v, 0, 1)
        else:
            write_bits_m2(v, 1, 1)
        if not v.bit_count:
            for i in range(v.v11):
                write_to_output(v, v.tmp_crc_data[i])
            v.v11 = 0
        v.chunks_count += 1
        src_offset = v.input_offset

    v.pack_token <<= (8 - v.bit_count)
    if v.bit_count or v.v11:
        write_to_output(v, v.pack_token & 0xFF)


def compress_data_1(v):
    src_offset = v.read_start_offset
    raw_n = len(v.raw_table)
    pos_n = len(v.pos_table)
    len_n = len(v.len_table)

    while v.v7 < v.unpacked_size:
        clear_table(v.len_table, len_n)
        clear_table(v.pos_table, pos_n)
        clear_table(v.raw_table, raw_n)

        proc_6(v)
        v.input_offset = src_offset

        proc_16(v, v.raw_table, raw_n)
        proc_16(v, v.len_table, len_n)
        proc_16(v, v.pos_table, pos_n)

        proc_18(v, v.raw_table, raw_n)
        proc_18(v, v.len_table, len_n)
        proc_18(v, v.pos_table, pos_n)

        write_bits_m1(v, v.v17, 16)

        while v.v17 > 0:
            v.v17 -= 1
            data_length = read_word_be_buf(v.temp, v.temp_offset)
            v.temp_offset += 2
            v.v7 += data_length
            proc_19(v, v.raw_table, data_length)
            if data_length:
                while data_length:
                    b = read_from_input(v)
                    x = (v.enc_key ^ b) & 0xFF
                    if not v.bit_count:
                        write_to_output(v, x)
                    else:
                        v.tmp_crc_data[v.v11] = x
                        v.v11 += 1
                    data_length -= 1
                v.enc_key = ror_w(v.enc_key)
            if v.v17:
                v.match_count = read_word_be_buf(v.temp, v.temp_offset)
                v.temp_offset += 2
                v.match_offset = read_word_be_buf(v.temp, v.temp_offset)
                v.temp_offset += 2
                proc_19(v, v.len_table, v.match_offset)
                proc_19(v, v.pos_table, v.match_count)
                v.match_count += 2
                v.v7 += v.match_count
                while v.match_count:
                    read_from_input(v)
                    v.match_count -= 1

        if not v.bit_count:
            for i in range(v.v11):
                write_to_output(v, v.tmp_crc_data[i])
            v.v11 = 0

        v.chunks_count += 1
        src_offset = v.input_offset

    v.pack_token >>= (16 - v.bit_count)
    if v.bit_count or v.v11:
        write_to_output(v, v.pack_token & 0xFF)
    if v.bit_count > 8 or v.v11:
        write_to_output(v, (v.pack_token >> 8) & 0xFF)


def do_pack_data(v):
    v.unpacked_size = v.file_size
    v.packed_size = v.file_size
    v.bytes_left = v.file_size

    if v.file_size <= RNC_HEADER_SIZE:
        return

    v.unpacked_crc = 0
    v.packed_crc = 0
    v.packed_size = 0
    v.processed_size = 0
    v.v7 = 0
    v.pack_block_pos = 0
    v.pack_token = 0
    v.bit_count = 0
    v.v11 = 0
    v.leeway = 0
    v.chunks_count = 0

    v.mem1 = bytearray(0xFFFF)
    v.mem2 = [0] * 0x8000
    v.mem3 = [0] * 0x8000
    v.mem4 = [0] * 0x8000
    v.mem5 = [0] * 0x8000

    init_dicts(v)

    write_dword_be_to_output(v, (RNC_SIGN << 8) | (v.method & 0xFF))
    write_dword_be_to_output(v, v.unpacked_size)
    write_dword_be_to_output(v, 0)
    write_word_be_to_output(v, 0)
    write_word_be_to_output(v, 0)
    write_word_be_to_output(v, 0)

    key = v.enc_key
    write_bits(v, 0, 1)  # no lock
    write_bits(v, 1 if v.enc_key else 0, 1)

    if v.method == 1:
        compress_data_1(v)
    elif v.method == 2:
        compress_data_2(v)

    for i in range(v.v11):
        write_to_output(v, v.tmp_crc_data[i])
    v.v11 = 0
    v.enc_key = key

    if v.leeway > (v.unpacked_size - v.packed_size):
        v.leeway -= (v.unpacked_size - v.packed_size)
    else:
        v.leeway = 0

    if v.method == 2:
        v.leeway += 2

    v.packed_size = v.output_offset - v.write_start_offset
    v.output_offset = v.write_start_offset + 8
    write_dword_be_to_output(v, v.packed_size - RNC_HEADER_SIZE)
    write_word_be_to_output(v, v.unpacked_crc)
    write_word_be_to_output(v, v.packed_crc)
    v.output[v.output_offset] = v.leeway & 0xFF
    v.output_offset += 1
    v.output[v.output_offset] = v.chunks_count & 0xFF
    v.output_offset += 1

    v.output_offset = v.packed_size + v.write_start_offset
    v.input_offset = v.unpacked_size + v.read_start_offset

    v.mem1 = None
    v.mem2 = None
    v.mem3 = None
    v.mem4 = None
    v.mem5 = None


def do_pack(v):
    if v.file_size <= RNC_HEADER_SIZE:
        return 2

    v.input_offset = 0
    v.output_offset = 0

    if v.file_size >= 4:
        d = read_word_be_buf(v.input, 0)
        d2 = read_word_be_buf(v.input, 2)
        if ((d << 16) | d2) >> 8 == RNC_SIGN:
            return 3

    do_pack_data(v)
    return 0


# ----------------- Bit readers (unpacking) -----------------

def read_source_byte(v):
    # mem1 is 0xFFFF bytes, pack_block_start is index
    if v.pack_block_start == 0xFFFD:
        left_size = v.file_size - v.input_offset
        if left_size <= 0xFFFD:
            size_to_read = left_size
        else:
            size_to_read = 0xFFFD
        v.pack_block_start = 0
        v.input_offset = read_buf(v.mem1, v.pack_block_start,
                                  v.input, v.input_offset, size_to_read)
        left_size -= size_to_read
        if left_size > 2:
            left_size = 2
        v.input_offset = read_buf(v.mem1, size_to_read, v.input, v.input_offset, left_size)
        v.input_offset -= left_size
    b = v.mem1[v.pack_block_start]
    v.pack_block_start += 1
    return b


def input_bits_m2(v, count):
    bits = 0
    while count:
        if not v.bit_count:
            v.bit_buffer = read_source_byte(v)
            v.bit_count = 8
        bits <<= 1
        if v.bit_buffer & 0x80:
            bits |= 1
        v.bit_buffer = (v.bit_buffer << 1) & 0xFF
        v.bit_count -= 1
        count -= 1
    return bits


def input_bits_m1(v, count):
    bits = 0
    prev_bits = 1
    while count:
        if not v.bit_count:
            b1 = read_source_byte(v)
            b2 = read_source_byte(v)
            v.bit_buffer = ((v.mem1[v.pack_block_start + 1] << 24) |
                            (v.mem1[v.pack_block_start] << 16) |
                            (b2 << 8) | b1)
            v.bit_count = 16
        if v.bit_buffer & 1:
            bits |= prev_bits
        v.bit_buffer >>= 1
        prev_bits <<= 1
        v.bit_count -= 1
        count -= 1
    return bits


def input_bits(v, count):
    if v.method != 2:
        return input_bits_m1(v, count)
    else:
        return input_bits_m2(v, count)


def decode_match_count(v):
    v.match_count = input_bits_m2(v, 1) + 4
    if input_bits_m2(v, 1):
        v.match_count = ((v.match_count - 1) << 1) + input_bits_m2(v, 1)


def decode_match_offset(v):
    v.match_offset = 0
    if input_bits_m2(v, 1):
        v.match_offset = input_bits_m2(v, 1)
        if input_bits_m2(v, 1):
            v.match_offset = ((v.match_offset << 1) | input_bits_m2(v, 1)) | 4
            if not input_bits_m2(v, 1):
                v.match_offset = (v.match_offset << 1) | input_bits_m2(v, 1)
        elif not v.match_offset:
            v.match_offset = input_bits_m2(v, 1) + 2
    v.match_offset = ((v.match_offset << 8) | read_source_byte(v)) + 1


def write_decoded_byte(v, b):
    if v.window == 0xFFFF:
        # flush decoded[dict_size .. 0xFFFF-1] to output
        size = 0xFFFF - v.dict_size
        write_buf(v.output, v.output_offset, v.decoded[v.dict_size:v.dict_size+size], size)
        v.output_offset += size
        # move last dict_size bytes to start
        memmove_bytes(v.decoded, 0, v.window - v.dict_size, v.dict_size)
        v.window = v.dict_size
    v.decoded[v.window] = b & 0xFF
    v.window += 1
    v.unpacked_crc_real = crc_table[(v.unpacked_crc_real ^ (b & 0xFF)) & 0xFF] ^ (v.unpacked_crc_real >> 8)
    v.unpacked_crc_real &= 0xFFFF


def unpack_data_m2(v):
    while v.processed_size < v.input_size:
        while True:
            if not input_bits_m2(v, 1):
                b = (v.enc_key ^ read_source_byte(v)) & 0xFF
                write_decoded_byte(v, b)
                v.enc_key = ror_w(v.enc_key)
                v.processed_size += 1
            else:
                if input_bits_m2(v, 1):
                    if input_bits_m2(v, 1):
                        if input_bits_m2(v, 1):
                            v.match_count = read_source_byte(v) + 8
                            if v.match_count == 8:
                                input_bits_m2(v, 1)
                                break
                        else:
                            v.match_count = 3
                        decode_match_offset(v)
                    else:
                        v.match_count = 2
                        v.match_offset = read_source_byte(v) + 1
                    v.processed_size += v.match_count
                    while v.match_count:
                        write_decoded_byte(v, v.decoded[v.window - v.match_offset])
                        v.match_count -= 1
                else:
                    decode_match_count(v)
                    if v.match_count != 9:
                        decode_match_offset(v)
                        v.processed_size += v.match_count
                        while v.match_count:
                            write_decoded_byte(v, v.decoded[v.window - v.match_offset])
                            v.match_count -= 1
                    else:
                        data_length = (input_bits_m2(v, 4) << 2) + 12
                        v.processed_size += data_length
                        while data_length:
                            b = (v.enc_key ^ read_source_byte(v)) & 0xFF
                            write_decoded_byte(v, b)
                            data_length -= 1
                        v.enc_key = ror_w(v.enc_key)
    size = v.window - v.dict_size
    write_buf(v.output, v.output_offset, v.decoded[v.dict_size:v.dict_size+size], size)
    v.output_offset += size
    return 0


def make_huftable(v, data, count):
    clear_table(data, count)
    leaf_nodes = input_bits_m1(v, 5)
    if leaf_nodes:
        if leaf_nodes > 16:
            leaf_nodes = 16
        for i in range(leaf_nodes):
            data[i].bit_depth = input_bits_m1(v, 4)
        proc_20(data, leaf_nodes)


def decode_table_data(v, data):
    i = 0
    while True:
        if data[i].bit_depth and (data[i].l3 == (v.bit_buffer & ((1 << data[i].bit_depth) - 1))):
            input_bits_m1(v, data[i].bit_depth)
            if i < 2:
                return i
            return input_bits_m1(v, i - 1) | (1 << (i - 1))
        i += 1


def unpack_data_m1(v):
    raw_n = len(v.raw_table)
    len_n = len(v.len_table)
    pos_n = len(v.pos_table)
    while v.processed_size < v.input_size:
        make_huftable(v, v.raw_table, raw_n)
        make_huftable(v, v.len_table, len_n)
        make_huftable(v, v.pos_table, pos_n)
        subchunks = input_bits_m1(v, 16)
        while subchunks:
            subchunks -= 1
            data_length = decode_table_data(v, v.raw_table)
            v.processed_size += data_length
            if data_length:
                while data_length:
                    b = (v.enc_key ^ read_source_byte(v)) & 0xFF
                    write_decoded_byte(v, b)
                    data_length -= 1
                v.enc_key = ror_w(v.enc_key)

                v.bit_buffer = (((v.mem1[v.pack_block_start + 2] << 16) |
                                 (v.mem1[v.pack_block_start + 1] << 8) |
                                 v.mem1[v.pack_block_start]) << v.bit_count) | \
                               (v.bit_buffer & ((1 << v.bit_count) - 1))

            if subchunks:
                v.match_offset = decode_table_data(v, v.len_table) + 1
                v.match_count = decode_table_data(v, v.pos_table) + 2
                v.processed_size += v.match_count
                while v.match_count:
                    write_decoded_byte(v, v.decoded[v.window - v.match_offset])
                    v.match_count -= 1

    size = v.window - v.dict_size
    write_buf(v.output, v.output_offset, v.decoded[v.dict_size:v.dict_size+size], size)
    v.output_offset += size
    return 0


# ----------------- High-level unpack -----------------

def do_unpack_data(v):
    start_pos = v.input_offset
    sign = read_dword_be(v)
    if (sign >> 8) != RNC_SIGN:
        return 6
    v.method = sign & 3
    v.input_size = read_dword_be(v)
    v.packed_size = read_dword_be(v)
    if v.file_size < v.packed_size:
        return 7

    v.unpacked_crc = read_word_be_from_input(v)
    v.packed_crc = read_word_be_from_input(v)

    _ = v.input[v.input_offset]; v.input_offset += 1  # leeway
    _ = v.input[v.input_offset]; v.input_offset += 1  # chunks

    if v.input_size > MAX_BUF_SIZE:
        print(f"Error: Declared unpacked size in header ({v.input_size}) is too large.")
        return 8

    if v.packed_size > v.file_size and v.packed_size > MAX_BUF_SIZE:
        print(f"Error: Declared packed size in header ({v.packed_size}) is too large.")
        return 8

    if crc_block(v.input, v.input_offset, v.packed_size) != v.packed_crc:
        return 4

    v.mem1 = bytearray(0xFFFF)
    v.decoded = bytearray(0xFFFF)
    v.pack_block_start = 0xFFFD
    # dict_size comes from main logic; use as-is
    v.window = v.dict_size

    v.unpacked_crc_real = 0
    v.bit_count = 0
    v.bit_buffer = 0
    v.processed_size = 0

    specified_key = v.enc_key
    error_code = 0

    if input_bits(v, 1) and v.puse_mode == ord('p'):
        error_code = 9

    if not error_code:
        if input_bits(v, 1) and not v.enc_key:
            error_code = 10

    if not error_code:
        if v.method == 1:
            error_code = unpack_data_m1(v)
        elif v.method == 2:
            error_code = unpack_data_m2(v)
        else:
            error_code = 6

    v.enc_key = specified_key
    v.mem1 = None
    v.decoded = None

    v.input_offset = start_pos + v.packed_size + RNC_HEADER_SIZE

    if error_code:
        return error_code

    if v.unpacked_crc != v.unpacked_crc_real:
        return 5

    return 0


def do_unpack(v):
    v.packed_size = v.file_size
    if v.file_size < RNC_HEADER_SIZE:
        return 6
    return do_unpack_data(v)


def do_search(v, input_size, save):
    error_code = 11
    has_rncs = 0
    current_offset = 0

    while current_offset + RNC_HEADER_SIZE <= input_size:
        v.read_start_offset = current_offset
        v.file_size = input_size - current_offset
        v.input_offset = 0
        v.output_offset = 0

        input_backup = v.input
        v.input = input_backup[current_offset:]

        error_code = do_unpack(v)
        v.input = input_backup

        if not error_code:
            print(f"RNC archive found: 0x{current_offset:06X} ({v.packed_size + RNC_HEADER_SIZE:6d}/{v.output_offset:6d} bytes)")
            advance = v.packed_size + RNC_HEADER_SIZE
            if advance == 0 or (current_offset + advance < current_offset) or (v.packed_size > input_size):
                current_offset += 1
            else:
                current_offset += advance
            has_rncs = 1

            if save:
                try:
                    os.makedirs("extracted", exist_ok=True)
                except OSError:
                    return 12

                out_name = os.path.join("extracted", f"data_{v.read_start_offset:06X}.bin")
                try:
                    with open(out_name, "wb") as out:
                        out.write(v.output[:v.output_offset])
                except OSError:
                    return 12
        else:
            if error_code == 4:
                print(f"Position 0x{current_offset:06X}: Packed CRC is wrong!")
            elif error_code == 5:
                print(f"Position 0x{current_offset:06X}: Unpacked CRC is wrong!")
            elif error_code == 9:
                print(f"Position 0x{current_offset:06X}: File already packed!")
            elif error_code == 10:
                print(f"Position 0x{current_offset:06X}: Decryption key required!")
            current_offset += 1

    return 0 if has_rncs else (11 if error_code == 6 else error_code)


# ----------------- CLI helpers -----------------

def print_usage():
    print("Unpack        : <u> <infile.bin> [outfile.bin] [-i=hex_offset_to_read_from] [-k=hex_key_if_protected]")
    print("Search        : <s> <infile.bin>")
    print("Seach&Extract : <e> <infile.bin>")
    print("Pack          : <p> <infile.bin> [outfile.bin] <-m=1|2> [-k=hex_key_to_protect]")


def parse_args(argv, v):
    argc = len(argv)
    if argc < 2:
        return 1

    mode_char = argv[1][0]
    if mode_char in "puse":
        v.puse_mode = ord(mode_char)
    else:
        return 1

    i = 3
    while i < argc:
        arg = argv[i]
        if arg and (arg[0] == '-' or arg[0] == '/'):
            which = arg[1]
            # argument is like -k=XXXX or -k XXXX
            if len(arg) > 3 and arg[2] == '=':
                arg_ptr = arg[3:]
            else:
                i += 1
                if i >= argc:
                    return 3
                arg_ptr = argv[i]
            if which == 'k':
                v.enc_key = int(arg_ptr, 16)
                if not v.enc_key:
                    return 3
            elif which == 'd':
                v.dict_size = int(arg_ptr, 16)
                if v.dict_size < 0x400:
                    v.dict_size = 0x400
            elif which == 'i':
                v.read_start_offset = int(arg_ptr, 16)
            elif which == 'o':
                v.write_start_offset = int(arg_ptr, 16)
            elif which == 'm':
                v.method = int(arg_ptr, 10)
                if not v.method or v.method > 2:
                    return 3
        i += 1
    return 0


def main(argv):
    print("-= RNC ProPackED v1.8 [by Lab 313] (01/26/2021) =-")
    print("-----------------------------")

    if len(argv) <= 2:
        print("Compression type: Huffman + LZ77")
        print("De/Compressor: Dr.MefistO")
        print("Coding: Dr. MefistO")
        print("Original: Rob Northen Computing")
        print("Info: De(re)compiled source of the famous RNC ProPack compression tool\n")
        print_usage()
        print("-----------------------------\n")

    v = Vars()
    if parse_args(argv, v):
        print("Wrong command line specified!")
        return 1

    # match original main's dict/method handling
    if v.method == 1:
        if v.dict_size > 0x8000:
            v.dict_size = 0x8000
        v.max_matches = 0x1000
    elif v.method == 2:
        if v.dict_size > 0x1000:
            v.dict_size = 0x1000
        v.max_matches = 0xFF

    if len(argv) < 3:
        print("No input file!")
        return 1

    infile = argv[2]
    try:
        with open(infile, "rb") as f:
            f.seek(0, os.SEEK_END)
            actual_file_length = f.tell()
            if v.read_start_offset >= actual_file_length:
                v.file_size = 0
            else:
                effective_size = actual_file_length - v.read_start_offset
                if effective_size > 0xFFFFFFFF:
                    print("Error: Calculated file size to process is too large.")
                    return 1
                v.file_size = int(effective_size)
            if v.file_size > MAX_BUF_SIZE:
                print(f"Error: File size {v.file_size} exceeds processable limit {MAX_BUF_SIZE}.")
                return 1
            f.seek(v.read_start_offset, os.SEEK_SET)
            v.input = bytearray(f.read(v.file_size))
    except OSError:
        print("Cannot open input file!")
        return -1

    v.output = bytearray(MAX_BUF_SIZE)
    v.temp = bytearray(MAX_BUF_SIZE)

    error_code = 0
    mode = chr(v.puse_mode)
    if mode == 'p':
        error_code = do_pack(v)
    elif mode == 'u':
        error_code = do_unpack(v)
    elif mode in ('s', 'e'):
        error_code = do_search(v, v.file_size, mode == 'e')

    if not error_code and mode not in ('s', 'e'):
        if len(argv) <= 3 or (argv[3][0] in "-/"):
            out_name = f"{infile}.{v.read_start_offset:06X}.bin"
        else:
            out_name = argv[3]

        try:
            with open(out_name, "wb") as out:
                out.write(v.output[:v.output_offset])
        except OSError:
            print("Cannot create output file!")
            return -1

        print(f"File successfully {'packed' if mode == 'p' else 'unpacked'}!")
        if mode == 'u':
            original = v.packed_size + RNC_HEADER_SIZE
        else:
            original = v.file_size
        print(f"Original/new size: {original}/{v.output_offset} bytes")
    else:
        if error_code == 0:
            pass
        elif error_code == 4:
            print("Corrupted input data.")
        elif error_code == 5:
            print("CRC check failed.")
        elif error_code in (6, 7):
            print("Wrong RNC header.")
        elif error_code == 10:
            print("Decryption key required.")
        elif error_code == 11:
            print("No RNC archives were found.")
        else:
            print(f"Cannot process file. Error code: {error_code:x}")

    return error_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
