"""Independent byte interpreter versus compiled static code, including every prefix."""
import random
import subprocess
import sys
import unittest
from fixture import program
from generate import generate

RUNNER = sys.argv.pop(1) if len(sys.argv) > 1 else None


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
                     b'\xc2\x20\xdb', b'\xea', b'\xdb\xea', b'\xea' * 32768 + b'\xdb'):
            with self.subTest(data=data[:4]), self.assertRaises(ValueError):
                generate(data)

    def test_deterministic(self):
        self.assertEqual(generate(program()), generate(program()))
        self.assertNotIn('switch', generate(program()))

    @unittest.skipUnless(RUNNER, 'compiled runner required')
    def test_equivalence(self):
        rng = random.Random(65816)
        # Each prefix exposes intermediate state and memory, not only final results.
        for limit in range(140):
            initial = [rng.randrange(65536), rng.randrange(256), rng.randrange(256),
                       rng.randrange(256) | 0x30]
            expected = interpret(program(), initial, limit)
            output = subprocess.check_output([RUNNER, *map(str, initial), str(limit), '32768'], text=True)
            lines = output.splitlines()
            actual = list(map(int, lines[0].split()))
            ram = dict(tuple(map(int, row.split(':'))) for row in lines[1:])
            with self.subTest(limit=limit):
                self.assertEqual((actual, ram), expected)

    @unittest.skipUnless(RUNNER, 'compiled runner required')
    def test_entry_contract(self):
        for p, pc in ((0, 32768), (0x34, 32769)):
            output = subprocess.check_output([RUNNER, '0', '0', '0', str(p), '10', str(pc)], text=True)
            self.assertEqual(list(map(int, output.splitlines()[0].split()))[-2:], [0, 0])


if __name__ == '__main__':
    unittest.main()
