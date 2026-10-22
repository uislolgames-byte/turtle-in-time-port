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
    assert len(sys.argv) == 6
    tools = sys.argv[1:]
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d)
        rom = bytearray(32768)
        rom[:4] = bytes([0x18, 0xfb, 0x78, 0xdb])
        title = b'T.M.N.T. 4'
        rom[0x7fc0:0x7fc0 + len(title)] = title
        assert len(rom) == 32768
        rom[0x7ffc:0x7ffe] = bytes([0, 0x80])
        rom[0x7ffa:0x7ffc] = bytes([0x10, 0x80])
        rom[0x7fea:0x7fec] = bytes([0x20, 0x80])
        rom[0x7fdc:0x7fe0] = bytes([0x34, 0x12, 0xcb, 0xed])
        valid = p / 'synthetic.sfc'
        valid.write_bytes(rom)
        tiny = p / 'tiny.sfc'
        tiny.write_bytes(b'bad')
        for exe in tools:
            check([exe], 2, 'Usage')
            check([exe, str(p / 'missing.sfc')], 1, 'Cannot open ROM')
            check([exe, str(tiny)], 1, 'ROM too small')
        check([tools[0], str(valid)], 0, 'Reset vector: $8000')
        check([tools[0], str(valid)], 0, 'Checksum pair: valid')
        check([tools[0], str(valid)], 0, 'ROM version: 0')
        headered = p / 'headered.smc'
        headered.write_bytes(bytes(512) + rom)
        check([tools[0], str(headered)], 0, 'Checksum pair: valid')
        malformed = p / 'malformed.sfc'
        malformed.write_bytes(rom + b'X')
        check([tools[0], str(malformed)], 1, 'Unsupported ROM size')
        check([tools[1], str(valid), '3'], 0, 'CLC')
        check([tools[1], str(valid), '4'], 0, 'XCE')
        check([tools[2], str(valid), '5'], 0, 'Decoded=')
        check([tools[3], str(valid), '5'], 0, 'Total candidate byte patterns:')
        check([tools[4], str(valid)], 0, 'Emulation RESET: $8000 [ROM offset $000000]')
        check([tools[4], str(valid)], 0, 'Native NMI: $8020 [ROM offset $000020]')
        check([tools[4], str(headered)], 0, 'Emulation NMI: $8010')
        for exe in tools[1:4]:
            check([exe, str(valid), '0'], 2)
    print('All smoke tests passed')

if __name__ == '__main__':
    main()
