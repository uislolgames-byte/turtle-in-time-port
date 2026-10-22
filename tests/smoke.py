import pathlib
import subprocess
import sys
import tempfile

def check(args, code, expected=None):
    p = subprocess.run(args, capture_output=True, text=True, timeout=15)
    output = p.stdout + p.stderr
    assert p.returncode == code, (args, p.returncode, output)
    if expected:
        assert expected in output, (args, output)

def main():
    assert len(sys.argv) == 5
    tools = sys.argv[1:]
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d)
        rom = bytearray(32768)
        rom[:4] = bytes([0x18, 0xfb, 0x78, 0xdb])
        rom[0x7fc0:0x7fc9] = b'T.M.N.T. 4'
        rom[0x7ffc:0x7ffe] = bytes([0, 0x80])
        valid = p / 'synthetic.sfc'
        valid.write_bytes(rom)
        tiny = p / 'tiny.sfc'
        tiny.write_bytes(b'bad')
        for exe in tools:
            check([exe], 2, 'Usage')
            check([exe, str(p / 'missing.sfc')], 1, 'Cannot open ROM')
            check([exe, str(tiny)], 1, 'ROM too small')
        check([tools[0], str(valid)], 0, 'Reset vector: $8000')
        check([tools[1], str(valid), '3'], 0, 'CLC')
        check([tools[1], str(valid), '4'], 0, 'XCE')
        check([tools[2], str(valid), '5'], 0, 'Decoded=')
        check([tools[3], str(valid), '5'], 0, 'Total candidate byte patterns:')
        for exe in tools[1:]:
            check([exe, str(valid), '0'], 2)
    print('All smoke tests passed')

if __name__ == '__main__':
    main()
