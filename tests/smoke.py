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
    # CMake supplies name=executable entries, avoiding positional count drift.
    paths = {}
    for entry in sys.argv[1:]:
        name, separator, path = entry.partition("=")
        assert separator and name and path, f"Invalid tool entry: {entry!r}"
        assert name not in paths, f"Duplicate tool entry: {name}"
        paths[name] = path
    required = ("rominfo", "disasm", "flow", "hwscan",
                "vectors", "entrydump", "callscan", "callgraph", "cfg")
    missing = set(required) - paths.keys()
    assert not missing, f"Missing required tools: {sorted(missing)}"
    rominfo, disasm, flow, hwscan, vectors, entrydump, callscan, callgraph, cfg = (
        paths[name] for name in required
    )
    tools = tuple(paths.values())
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
                       'SEP #$10', 'LDX #$7F', 'STA $2100', 'STP'):
            assert opcode in listing, (opcode, listing)
        # Branch targets must use signed displacements and CPU addresses.
        branch_rom = bytearray(rom)
        branch_rom[:7] = bytes([0x80, 0x02, 0xEA, 0xEA, 0xD0, 0xFA, 0xDB])
        branch = root / 'branches.sfc'
        branch.write_bytes(branch_rom)
        branch_listing = check([disasm, str(branch), '7'], 0)
        assert '$008000: BRA $008004' in branch_listing, branch_listing
        assert '$008004: BNE $008000' in branch_listing, branch_listing

        # REP must not change index width while the CPU is in emulation mode.
        emu_rom = bytearray(rom)
        emu_rom[:5] = bytes([0xC2, 0x10, 0xA2, 0x7F, 0xDB])
        emu = root / 'emulation.sfc'
        emu.write_bytes(emu_rom)
        emu_listing = check([disasm, str(emu), '3'], 0)
        assert '$008002: LDX #$7F' in emu_listing, emu_listing
        assert '$008004: STP' in emu_listing, emu_listing
        check([flow, str(valid), '20'], 0, 'Decoded=')
        check([hwscan, str(valid), '5'], 0, 'Total candidate byte patterns: 1')
        check([vectors, str(valid)], 0, 'Emulation RESET: $8000 [ROM offset $000000]')
        check([vectors, str(valid)], 0, 'Native NMI: $8020 [ROM offset $000020]')
        check([entrydump, str(valid)], 0, 'RESET $8000: 18 FB C2 30 A2 FF 1D')
        check([entrydump, str(headered)], 0, 'RESET $8000: 18 FB')
        # JSR uses the current program bank; JSL uses an explicit 24-bit bank.
        calls_rom = bytearray(rom)
        calls_rom[:10] = bytes([0x20, 0x34, 0x92, 0x22, 0x78, 0x56, 0x12,
                                0xEA, 0x60, 0xDB])
        calls_path = root / 'calls.sfc'
        calls_path.write_bytes(calls_rom)
        calls_output = check([callscan, str(calls_path)], 0)
        assert '$008000: JSR $009234' in calls_output, calls_output
        assert '$008003: JSL $125678' in calls_output, calls_output
        assert 'Calls=2' in calls_output, calls_output
        graph_output = check([callgraph, str(calls_path)], 0)
        assert '"$008000" -> "$009234" [label="JSR"]' in graph_output, graph_output
        assert '"$008003" -> "$125678" [label="JSL"]' in graph_output, graph_output
        assert 'digraph TMNT_calls {' in graph_output, graph_output
        cfg_output = check([cfg, str(branch)], 0)
        assert '"$008000" -> "$008004" [label="BRA"]' in cfg_output, cfg_output
        assert '"$008004" -> "$008000" [label="BNE"]' in cfg_output, cfg_output
        assert '"$008004" -> "$008006" [label="fallthrough"]' in cfg_output, cfg_output
        # Exercise every conditional branch: both taken and fallthrough edges.
        branch_cases = {
            0x10: "BPL", 0x30: "BMI", 0x50: "BVC", 0x70: "BVS",
            0x90: "BCC", 0xB0: "BCS", 0xD0: "BNE", 0xF0: "BEQ",
        }
        for opcode, mnemonic in branch_cases.items():
            conditional_rom = bytearray(rom)
            conditional_rom[:5] = bytes([opcode, 0x02, 0xDB, 0xEA, 0xDB])
            conditional_path = root / f"branch-{opcode:02x}.sfc"
            conditional_path.write_bytes(conditional_rom)
            output = check([cfg, str(conditional_path)], 0)
            assert f'"$008000" -> "$008004" [label="{mnemonic}"]' in output, output
            assert '"$008000" -> "$008002" [label="fallthrough"]' in output, output
        # Decode additional instructions with 8-bit accumulator/index widths.
        extended_rom = bytearray(rom)
        extended_rom[:13] = bytes([
            0xA0, 0x12,        # LDY #$12
            0x69, 0x01,        # ADC #$01
            0x8E, 0x00, 0x21,  # STX $2100
            0x5A,              # PHY
            0x7A,              # PLY
            0xC9, 0x05,        # CMP #$05
            0xEA,              # NOP
            0xDB,              # STP
        ])
        extended_path = root / 'extended-cfg.sfc'
        extended_path.write_bytes(extended_rom)
        extended_output = check([cfg, str(extended_path)], 0)
        assert '"$008000" -> "$008002" [label="next"]' in extended_output, extended_output
        assert '"$008004" -> "$008007" [label="next"]' in extended_output, extended_output
        assert '"$00800B" -> "$00800C" [label="next"]' in extended_output, extended_output
        check([cfg, str(invalid)], 1, 'Reset vector')
        check([cfg, str(valid), '0'], 2)
        check([callgraph, str(invalid)], 1, 'Reset vector')
        check([callgraph, str(valid), '0'], 2)
        check([callscan, str(invalid)], 1, 'Reset vector')
        check([callscan, str(valid), '0'], 2)
        for exe in (disasm, flow, hwscan):
            check([exe, str(valid), '0'], 2)
            check([exe, str(valid), '-1'], 2)
    print('All cross-platform smoke and regression tests passed')


if __name__ == '__main__':
    main()
