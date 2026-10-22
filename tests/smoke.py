"""Cross-platform smoke/regression tests using synthetic, non-copyright ROMs."""
import pathlib
import subprocess
import sys
import tempfile


def check(args, code, expected=None):
    result = subprocess.run(args, capture_output=True, text=True, timeout=15)
    output = result.stdout + result.stderr
    assert result.returncode == code, (args, result.returncode, output)
    if expected is not None:
        assert expected in output, (args, output)
    return output


def main():
    assert len(sys.argv) == 6, "Expected five tool executables from CMake"
    rominfo, disasm, flow, hwscan, vectors = sys.argv[1:]
    tools = (rominfo, disasm, flow, hwscan, vectors)
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        rom = bytearray(32768)
        # Realistic short reset sequence with 8/16-bit index width transitions.
        program = bytes([
            0x18, 0xFB,             # CLC; XCE
            0xC2, 0x30,             # REP #$30 (M=0, X=0)
            0xA2, 0xFF, 0x1D,       # LDX #$1DFF (16-bit)
            0xE2, 0x10,             # SEP #$10 (X=1)
            0xA2, 0x7F,             # LDX #$7F (8-bit)
            0x8D, 0x00, 0x21,       # STA $2100
            0xDB,                   # STP
        ])
        rom[:len(program)] = program
        title = b'T.M.N.T. 4'
        rom[0x7fc0:0x7fc0 + len(title)] = title
        rom[0x7ffc:0x7ffe] = bytes([0, 0x80])
        rom[0x7ffa:0x7ffc] = bytes([0x10, 0x80])
        rom[0x7fea:0x7fec] = bytes([0x20, 0x80])
        rom[0x7fdc:0x7fe0] = bytes([0x34, 0x12, 0xcb, 0xed])
        assert len(rom) == 32768
        valid = root / 'synthetic.sfc'
        valid.write_bytes(rom)
        tiny = root / 'tiny.sfc'
        tiny.write_bytes(b'bad')
        missing = root / 'missing.sfc'
        for exe in tools:
            check([exe], 2, 'Usage')
            check([exe, str(missing)], 1, 'Cannot open ROM')
            check([exe, str(tiny)], 1, 'ROM too small')

        info = check([rominfo, str(valid)], 0)
        assert 'ROM bytes: 32768' in info
        assert 'Reset vector: $8000' in info
        assert 'Checksum pair: valid' in info
        assert 'ROM version: 0' in info

        headered = root / 'headered.smc'
        headered.write_bytes(bytes(512) + rom)
        check([rominfo, str(headered)], 0, 'ROM bytes: 32768')
        check([vectors, str(headered)], 0, 'Emulation NMI: $8010')

        malformed = root / 'malformed.sfc'
        malformed.write_bytes(rom + b'X')
        check([rominfo, str(malformed)], 1, 'Unsupported ROM size')

        # Bad reset vectors must fail instead of silently mapping to $8000.
        invalid_reset = bytearray(rom)
        invalid_reset[0x7ffc:0x7ffe] = bytes([0, 0x20])
        invalid = root / 'invalid-reset.sfc'
        invalid.write_bytes(invalid_reset)
        check([disasm, str(invalid)], 1, 'Reset vector')
        check([flow, str(invalid)], 1, 'Reset vector')

        listing = check([disasm, str(valid), '10'], 0)
        for opcode in ('CLC', 'XCE', 'REP #$30', 'LDX #$1DFF',
                       'SEP #$10', 'LDX #$7F', 'STA #$2100', 'STP'):
            assert opcode in listing, (opcode, listing)
        check([flow, str(valid), '20'], 0, 'Decoded=')
        check([hwscan, str(valid), '5'], 0, 'Total candidate byte patterns: 1')
        check([vectors, str(valid)], 0, 'Emulation RESET: $8000 [ROM offset $000000]')
        check([vectors, str(valid)], 0, 'Native NMI: $8020 [ROM offset $000020]')
        for exe in (disasm, flow, hwscan):
            check([exe, str(valid), '0'], 2)
            check([exe, str(valid), '-1'], 2)
    print('All cross-platform smoke and regression tests passed')


if __name__ == '__main__':
    main()
