"""Create only original synthetic instruction bytes; never reads a game ROM."""
import random
import sys
from pathlib import Path
from generate import generate


def program():
    data = bytearray()
    # Width, hidden accumulator byte, N/Z transitions, wraparound and RAM edges.
    data.extend(bytes.fromhex('a900 8d0000 a980 8dffff a2ff e8 ca a000 18 38 ea'))
    rng = random.Random(816)
    for _ in range(120):
        op = rng.choice([0xA9, 0xA2, 0xA0, 0x8D, 0xE8, 0xCA, 0x18, 0x38, 0xEA, 0xC9, 0x29, 0x09, 0x49])
        data.append(op)
        if op in (0xA9, 0xA2, 0xA0, 0xC9, 0x29, 0x09, 0x49):
            data.append(rng.randrange(256))
        elif op == 0x8D:
            data.extend(rng.randrange(65536).to_bytes(2, 'little'))
    data.append(0xDB)
    return bytes(data)


def branches():
    data = bytearray()
    for index, op in enumerate((0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0)):
        # If taken, skip a distinct observable RAM write.
        data.extend((op, 3, 0x8D, index, 0))
    data.extend(bytes.fromhex('8003 8d0001 a203 ca d0fd 8000 db'))
    return bytes(data)


def boundaries():
    # BNE -128 returns to DEX; BRA +127 skips the entire NOP region.
    return bytes.fromhex('a202 ca') + b'\xea' * 125 + bytes.fromhex('d080 807f') + b'\xea' * 127 + b'\xdb'


PROGRAMS = {'synthetic': program, 'branches': branches,
            'loop': lambda: bytes.fromhex('80fe db'), 'boundaries': boundaries,
            'logic': lambda: bytes.fromhex('c980 294f 0980 49ff db')}


if __name__ == '__main__':
    data = PROGRAMS[sys.argv[3] if len(sys.argv) > 3 else 'synthetic']()
    Path(sys.argv[1]).write_bytes(data)
    Path(sys.argv[2]).write_text(generate(data), encoding='utf-8')
