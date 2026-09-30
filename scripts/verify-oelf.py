#!/usr/bin/env python3
"""Reject overlapping load mappings and truncated allocated data after conversion."""
import struct
import sys
from pathlib import Path


def verify(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    _, phsize, phcount, shsize, shcount, _ = struct.unpack_from('<6H', data, 52)
    mappings = []
    dynamic = []
    for i in range(phcount):
        kind, flags, offset, address, _, filesz, memsz, align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phsize)
        if kind == 2:  # Orbis places the rebuilt dynamic table in dynlibdata.
            dynamic.append((address, offset, filesz))
        if kind not in (1, 0x61000010):  # PT_LOAD and PT_SCE_RELRO
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError('invalid mapped file extent')
        if align < 0x4000 or (offset - address) % 0x4000:
            raise ValueError('PS4 mapping must use 16 KiB pages')
        for other, end, *_ in mappings:
            if address < end and other < address + memsz:
                raise ValueError('overlapping or duplicate LOAD/RELRO mappings')
        mappings.append((address, address + memsz, offset, filesz))
    if not mappings:
        raise ValueError('no load mappings')
    for i in range(shcount):
        _, kind, flags, address, offset, size = struct.unpack_from(
            '<IIQQQQ', data, shoff + i * shsize)
        if kind == 6:
            if (address, offset, size) not in dynamic or offset + size > len(data):
                raise ValueError('invalid Orbis dynamic table')
            continue
        if not flags & 2 or kind == 8 or not size:  # ALLOC, excluding NOBITS
            continue
        if not any(address >= start and address + size <= start + filesz
                   and offset == fileoff + address - start
                   for start, _, fileoff, filesz in mappings):
            raise ValueError(f'allocated section {i} is truncated or outside file mappings')
    print(f'PASS OELF: non-overlapping mappings and initialized data coverage: {path}')


if __name__ == '__main__':
    try:
        verify(sys.argv[1])
    except (ValueError, struct.error) as error:
        sys.exit(f'FAIL OELF: {error}')
