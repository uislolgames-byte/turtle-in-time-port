"""Static translation of a bounded, flat synthetic 65C816 program (no SNES ROM mapping)."""
import argparse
from pathlib import Path

# opcode: (status bit, branch when set)
BRANCHES = {0x10: (0x80, False), 0x30: (0x80, True),
            0x50: (0x40, False), 0x70: (0x40, True),
            0x90: (1, False), 0xB0: (1, True),
            0xD0: (2, False), 0xF0: (2, True)}


def generate(data: bytes) -> str:
    if not data or len(data) > 0x8000:
        raise ValueError("program must contain 1..32768 bytes")
    instructions = {}
    offset = 0
    while offset < len(data):
        op = data[offset]
        size = 2 if op in (0xA9, 0xA2, 0xA0, 0x80) or op in BRANCHES else 3 if op == 0x8D else 1
        if offset + size > len(data):
            raise ValueError(f"truncated instruction at {offset:#x}")
        operand = int.from_bytes(data[offset + 1:offset + size], 'little')
        statements = {
            0xEA: '', 0x18: 's.p &= 0xfe;', 0x38: 's.p |= 1;',
            0xA9: f's.a = (s.a & 0xff00) | {operand}; nz(s, s.a & 255);',
            0xA2: f's.x = {operand}; nz(s, s.x);',
            0xA0: f's.y = {operand}; nz(s, s.y);',
            0xE8: 's.x = static_cast<uint8_t>(s.x + 1); nz(s, s.x);',
            0xCA: 's.x = static_cast<uint8_t>(s.x - 1); nz(s, s.x);',
            0x8D: f's.ram[{operand}] = static_cast<uint8_t>(s.a);',
            0xDB: '',
        }
        if op not in statements and op != 0x80 and op not in BRANCHES:
            raise ValueError(f"unsupported opcode {op:#x} at {offset:#x}")
        instructions[offset] = (op, size, operand, statements.get(op, ''))
        offset += size
        if op == 0xDB:
            if offset != len(data):
                raise ValueError('bytes after STP are not accepted')
    if op != 0xDB:
        raise ValueError('program must terminate with STP')

    # Validate all code, including unreachable bytes, before emitting any C++.
    targets = {}
    for offset, (op, size, operand, _) in instructions.items():
        if op == 0x80 or op in BRANCHES:
            target = offset + size + (operand if operand < 128 else operand - 256)
            if target not in instructions:
                raise ValueError(f'branch target outside program or inside operand at {offset:#x}')
            targets[offset] = target

    lines = ['#include "runtime.hpp"',
             'bool run_generated(State& s, unsigned limit) {',
             '  if (s.pc != 0x8000 || (s.p & 0x30) != 0x30) return false;']
    labels = set(targets.values())
    for offset, (op, size, _, statement) in instructions.items():
        if offset in labels:
            lines.append(f'L_{offset}:')
        lines += ['  if (s.steps >= limit) return false;', f'  {statement}',
                  f'  s.pc = {(0x8000 + offset + size) & 0xffff}; ++s.steps;']
        if op in BRANCHES:
            mask, when_set = BRANCHES[op]
            condition = f'(s.p & {mask}) {"!=" if when_set else "=="} 0'
        elif op == 0x80:
            condition = 'true'
        else:
            condition = None
        if condition:
            target = targets[offset]
            lines.append(f'  if ({condition}) {{ s.pc = {0x8000 + target}; goto L_{target}; }}')
        if op == 0xDB:
            lines.append('  return true;')
    return '\n'.join(lines + ['}', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        source = generate(args.input.read_bytes())
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    args.output.write_text(source, encoding='utf-8')
