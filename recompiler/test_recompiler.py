"""Independent byte interpreter versus compiled static code, including every prefix."""
import random
import subprocess
import sys
import unittest
from fixture import PROGRAMS
from generate import generate

RUNNERS = dict(arg.split('=', 1) for arg in sys.argv[1:])
sys.argv[1:] = []


def interpret(data, initial, limit):
    a, x, y, p = initial
    pc, steps, halted, ram = 0x8000, 0, False, {}
    while steps < limit:
        op = data[pc - 0x8000]
        pc += 1
        value = None
        if op in (0xA9, 0xA2, 0xA0):
            value = data[pc - 0x8000]
            pc += 1
            if op == 0xA9:
                a = (a // 256) * 256 + value
            elif op == 0xA2:
                x = value
            else:
                y = value
        elif op in (0xC9, 0x29, 0x09, 0x49):
            operand = data[pc - 0x8000]
            pc += 1
            low = a % 256
            if op == 0xC9:
                value = (low - operand) % 256
                p = (p | 1) if low >= operand else (p & ~1)
            else:
                value = (low & operand) if op == 0x29 else (low | operand) if op == 0x09 else (low ^ operand)
                a = a - low + value
        elif op == 0x8D:
            address = data[pc - 0x8000] + 256 * data[pc - 0x7fff]
            ram[address] = a % 256
            pc += 2
        elif op in (0xE8, 0xCA):
            x = (x + (1 if op == 0xE8 else -1)) % 256
            value = x
        elif op == 0x18:
            p = p & ~1
        elif op == 0x38:
            p = p | 1
        elif op in (0x80, 0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0):
            displacement = int.from_bytes(data[pc - 0x8000:pc - 0x8000 + 1], 'little', signed=True)
            pc += 1
            # Deliberately independent of the generator's branch table.
            taken = (op == 0x80 or (op == 0x10 and p < 128)
                     or (op == 0x30 and p >= 128) or (op == 0x50 and not p & 64)
                     or (op == 0x70 and bool(p & 64)) or (op == 0x90 and not p & 1)
                     or (op == 0xB0 and bool(p & 1)) or (op == 0xD0 and not p & 2)
                     or (op == 0xF0 and bool(p & 2)))
            if taken:
                pc += displacement
        elif op == 0xDB:
            halted = True
        elif op != 0xEA:
            raise AssertionError('invalid fixture')
        if value is not None:
            p = (p & ~130) + (2 if value == 0 else 0) + (128 if value >= 128 else 0)
        steps += 1
        if halted:
            break
    return [a, x, y, p, pc & 65535, steps, int(halted)], {k: v for k, v in ram.items() if v}


class RecompilerTests(unittest.TestCase):
    def test_rejections(self):
        for data in (b'', b'\xa9', b'\x8d\x01', b'\x00\xdb', b'\xfb\xdb',
                     b'\xc2\x20\xdb', b'\xea', b'\xdb\xea', b'\xea' * 32768 + b'\xdb',
                     bytes.fromhex('80'), bytes.fromhex('80fd db'),
                     bytes.fromhex('8001 a901 db'), bytes.fromhex('d07f db'),
                     bytes.fromhex('8001 db'), b'\xc9', b'\x29', b'\x09', b'\x49'):
            with self.subTest(data=data[:4]), self.assertRaises(ValueError):
                generate(data)

    def test_reference_branch_examples(self):
        self.assertEqual(interpret(bytes.fromhex('80fe db'), [0, 0, 0, 0x34], 3)[0],
                         [0, 0, 0, 0x34, 0x8000, 3, 0])
        self.assertEqual(interpret(bytes.fromhex('a202 ca d0fd db'), [0, 0, 0, 0x34], 10)[0],
                         [0, 0, 0, 0x36, 0x8006, 6, 1])

    @unittest.skipUnless(RUNNERS, 'compiled runners required')
    def test_equivalence(self):
        rng = random.Random(65816)
        # Each prefix exposes intermediate state and memory, not only final results.
        self.assertEqual(set(RUNNERS), set(PROGRAMS))
        for name, fixture in PROGRAMS.items():
            for limit in range(300):
                initial = [rng.randrange(65536), rng.randrange(256), rng.randrange(256),
                           rng.randrange(256) | 0x30]
                expected = interpret(fixture(), initial, limit)
                output = subprocess.check_output([RUNNERS[name], *map(str, initial), str(limit), '32768'], text=True, timeout=5)
                lines = output.splitlines()
                actual = list(map(int, lines[0].split()))
                ram = dict(tuple(map(int, row.split(':'))) for row in lines[1:])
                with self.subTest(program=name, limit=limit):
                    self.assertEqual((actual, ram), expected)

    @unittest.skipUnless(RUNNERS, 'compiled runners required')
    def test_all_branch_flag_combinations(self):
        for bits in range(16):
            p = 0x30 | ((bits & 8) << 4) | ((bits & 4) << 4) | (bits & 3)
            output = subprocess.check_output(
                [RUNNERS['branches'], '4660', '0', '0', str(p), '300', '32768'],
                text=True, timeout=5)
            ram = dict(tuple(map(int, row.split(':'))) for row in output.splitlines()[1:])
            # Complementary conditions must write exactly one address per flag pair.
            expected = {0 if p & 128 else 1: 0x34,
                        2 if p & 64 else 3: 0x34,
                        4 if p & 1 else 5: 0x34,
                        6 if p & 2 else 7: 0x34}
            with self.subTest(p=p):
                self.assertEqual(ram, expected)

    @unittest.skipUnless(RUNNERS, 'compiled runners required')
    def test_cmp_exhaustive_and_logic_flags(self):
        for low in range(256):
            for initial_p in (0x34, 0xff):
                # The first instruction is CMP #$80; A (including B) is unchanged.
                output = subprocess.check_output(
                    [RUNNERS['logic'], str(0xab00 + low), '17', '23', str(initial_p), '1', '32768'],
                    text=True, timeout=5)
                p = initial_p & ~0x83
                if low >= 128:
                    p |= 1
                if low == 128:
                    p |= 2
                if low < 128:
                    p |= 128
                with self.subTest(a=low, p=initial_p):
                    self.assertEqual(list(map(int, output.split())),
                                     [0xab00 + low, 17, 23, p, 0x8002, 1, 0])
        # Explicit intermediate logical results; all flags except N/Z stay set.
        for limit, a, p in ((2, 0xab00, 0x7e), (3, 0xab80, 0xfc), (4, 0xab7f, 0x7c)):
            output = subprocess.check_output(
                [RUNNERS['logic'], str(0xab00), '17', '23', '255', str(limit), '32768'],
                text=True, timeout=5)
            self.assertEqual(list(map(int, output.split())),
                             [a, 17, 23, p, 0x8000 + 2 * limit, limit, 0])

    @unittest.skipUnless(RUNNERS, 'compiled runners required')
    def test_entry_contract(self):
        for p, pc in ((0, 32768), (0x34, 32769)):
            output = subprocess.check_output([RUNNERS['synthetic'], '0', '0', '0', str(p), '10', str(pc)], text=True, timeout=5)
            self.assertEqual(list(map(int, output.splitlines()[0].split()))[-2:], [0, 0])


if __name__ == '__main__':
    unittest.main()
