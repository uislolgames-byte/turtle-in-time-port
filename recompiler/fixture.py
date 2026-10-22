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
        op = rng.choice([0xA9, 0xA2, 0xA0, 0x8D, 0xE8, 0xCA, 0x18, 0x38, 0xEA])
        data.append(op)
        if op in (0xA9, 0xA2, 0xA0):
            data.append(rng.randrange(256))
        elif op == 0x8D:
            data.extend(rng.randrange(65536).to_bytes(2, 'little'))
    data.append(0xDB)
    return bytes(data)


if __name__ == '__main__':
    Path(sys.argv[1]).write_bytes(program())
    Path(sys.argv[2]).write_text(generate(program()), encoding='utf-8')
