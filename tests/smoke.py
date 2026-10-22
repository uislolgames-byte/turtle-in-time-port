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
                "vectors", "entrydump", "callscan", "callgraph", "cfg", "lift", "exec")
    missing = set(required) - paths.keys()
    assert not missing, f"Missing required tools: {sorted(missing)}"
    rominfo, disasm, flow, hwscan, vectors, entrydump, callscan, callgraph, cfg, lift, executor = (
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

        # Structured reset-prefix IR: 8/16-bit register widths and bounded stop.
        ir = check([lift, str(valid), '16'], 0)
        assert 'address,opcode,operand,e,m_bits,x_bits' in ir, ir
        assert '$008004,LDX_IMM,$1DFF,0,16,16' in ir, ir
        assert '$008009,LDX_IMM,$7F,0,16,8' in ir, ir
        assert '$00800E,STP,,0,16,8' in ir, ir
        assert ir.splitlines()[-1].startswith('$00800E,STP'), ir
        unknown_ir_rom = bytearray(rom)
        unknown_ir_rom[:2] = bytes([0xFF, 0xEA])
        unknown_ir_path = root / 'unknown-ir.sfc'
        unknown_ir_path.write_bytes(unknown_ir_rom)
        unknown_ir = check([lift, str(unknown_ir_path)], 0)
        assert '$008000,UNKNOWN' in unknown_ir, unknown_ir
        assert len(unknown_ir.splitlines()) == 2, unknown_ir
        check([lift, str(valid), '0'], 2)
        # Arithmetic invalidates carry; bitwise operations preserve it.
        arithmetic_rom = bytearray(rom)
        arithmetic_rom[:6] = bytes([0x18, 0x69, 0x01, 0xFB, 0xEA, 0xDB])
        arithmetic_path = root / 'lift-arithmetic.sfc'
        arithmetic_path.write_bytes(arithmetic_rom)
        arithmetic_ir = check([lift, str(arithmetic_path)], 0)
        assert '$008001,ADC_IMM,$01,1,8,8' in arithmetic_ir, arithmetic_ir
        assert '$008003,UNKNOWN' in arithmetic_ir, arithmetic_ir
        bitwise_rom = bytearray(rom)
        bitwise_rom[:6] = bytes([0x18, 0x29, 0x01, 0xFB, 0xEA, 0xDB])
        bitwise_path = root / 'lift-bitwise.sfc'
        bitwise_path.write_bytes(bitwise_rom)
        bitwise_ir = check([lift, str(bitwise_path)], 0)
        assert '$008003,XCE,,0,8,8' in bitwise_ir, bitwise_ir
        # Do not wrap the program counter across the 16-bit bank boundary.
        boundary_ir_rom = bytearray(rom)
        boundary_ir_rom[0x7fff] = 0xEA
        boundary_ir_rom[0x7ffc:0x7ffe] = bytes([0xFF, 0xFF])
        boundary_ir_path = root / 'lift-bank-boundary.sfc'
        boundary_ir_path.write_bytes(boundary_ir_rom)
        boundary_ir = check([lift, str(boundary_ir_path)], 0)
        assert '$00FFFF,NOP' in boundary_ir, boundary_ir
        assert len(boundary_ir.splitlines()) == 2, boundary_ir
        # Bounded instruction-subset execution, not full SNES emulation.
        executed = check([executor, str(valid), '20'], 0)
        assert 'reason=unsupported' in executed, executed  # STA not implemented
        assert 'x=$007F' in executed and 'm=16 xbits=8' in executed, executed
        execute_rom = bytearray(rom)
        execute_rom[:13] = bytes([
            0x18, 0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12,
            0xA2, 0x78, 0x56, 0xE2, 0x10, 0xDB,
        ])
        execute_path = root / 'execute.sfc'
        execute_path.write_bytes(execute_rom)
        execution = check([executor, str(execute_path), '20'], 0)
        assert 'reason=stp' in execution, execution
        assert 'a=$1234' in execution and 'x=$0078' in execution, execution
        assert 'm=16 xbits=8' in execution, execution
        assert 'steps=7' in execution, execution
        # STA to modeled low WRAM is permitted; hardware I/O is not.
        memory_rom = bytearray(rom)
        memory_rom[:9] = bytes([0xA9, 0xAB, 0x8D, 0x10, 0x00,
                                 0x80, 0x01, 0xFF, 0xDB])
        memory_path = root / 'execute-memory.sfc'
        memory_path.write_bytes(memory_rom)
        memory_output = check([executor, str(memory_path), '20'], 0)
        assert 'reason=stp' in memory_output, memory_output
        assert 'stores=1 last_addr=$0010 last_value=$00AB' in memory_output, memory_output
        # BRA with a negative offset loops only up to the instruction limit.
        loop_rom = bytearray(rom)
        loop_rom[:2] = bytes([0x80, 0xFE])
        loop_path = root / 'execute-loop.sfc'
        loop_path.write_bytes(loop_rom)
        loop_output = check([executor, str(loop_path), '5'], 0)
        assert 'reason=limit steps=5 pc=$008000' in loop_output, loop_output
        # A branch outside the ROM address window must stop safely.
        escape_rom = bytearray(rom)
        escape_rom[:2] = bytes([0x80, 0x80])
        escape_path = root / 'execute-escape.sfc'
        escape_path.write_bytes(escape_rom)
        escape_output = check([executor, str(escape_path), '5'], 0)
        assert 'reason=boundary' in escape_output, escape_output
        # STA/LDA round-trip and BEQ taken after loading zero.
        read_rom = bytearray(rom)
        read_rom[:17] = bytes([
            0xA9, 0x2A, 0x8D, 0x20, 0x00, 0xA9, 0x00,
            0xAD, 0x20, 0x00, 0xD0, 0x02, 0x80, 0x01,
            0xFF, 0xDB, 0xEA,
        ])
        read_path = root / 'execute-read.sfc'
        read_path.write_bytes(read_rom)
        read_output = check([executor, str(read_path), '20'], 0)
        assert 'reason=stp' in read_output, read_output
        assert 'a=$002A' in read_output and 'z=0' in read_output, read_output
        # BEQ taken on zero skips an unsupported opcode.
        beq_rom = bytearray(rom)
        beq_rom[:7] = bytes([0xA9, 0x00, 0xF0, 0x01, 0xFF, 0xDB, 0xEA])
        beq_path = root / 'execute-beq.sfc'
        beq_path.write_bytes(beq_rom)
        beq_output = check([executor, str(beq_path), '10'], 0)
        assert 'reason=stp' in beq_output and 'z=1' in beq_output, beq_output
        # BNE not taken when Z is set.
        bne_rom = bytearray(rom)
        bne_rom[:7] = bytes([0xA9, 0x00, 0xD0, 0x01, 0xDB, 0xFF, 0xEA])
        bne_path = root / 'execute-bne.sfc'
        bne_path.write_bytes(bne_rom)
        bne_output = check([executor, str(bne_path), '10'], 0)
        assert 'reason=stp' in bne_output, bne_output
        # Unmodeled hardware reads must stop instead of returning fake data.
        hardware_rom = bytearray(rom)
        hardware_rom[:4] = bytes([0xAD, 0x00, 0x21, 0xDB])
        hardware_path = root / 'execute-hardware-read.sfc'
        hardware_path.write_bytes(hardware_rom)
        hardware_output = check([executor, str(hardware_path)], 0)
        assert 'reason=unsupported steps=0' in hardware_output, hardware_output
        check([executor, str(valid), '0'], 2)
        unsupported_rom = bytearray(rom)
        unsupported_rom[0] = 0xFF
        unsupported_path = root / 'execute-unsupported.sfc'
        unsupported_path.write_bytes(unsupported_rom)
        unsupported = check([executor, str(unsupported_path)], 0)
        assert 'reason=unsupported steps=0' in unsupported, unsupported
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
        vector_output = check([vectors, str(valid)], 0, 'Emulation RESET: $8000 [ROM offset $000000]')
        assert 'Interrupt vectors (bank $00, LoROM):' in vector_output
        assert 'Emulation NMI: $8010 [ROM offset $000010]' in vector_output
        assert 'Native NMI: $8020 [ROM offset $000020]' in vector_output
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
        inventory = check([cfg, str(calls_path), '512', '--inventory'], 0)
        assert inventory.startswith('address,origin,source,kind,status,rom_offset,first_bytes,first_opcode,entry_hint,incoming_calls\n'), inventory
        assert '$009234,call,$008000,JSR,candidate,$001234,\"' in inventory, inventory
        assert '$125678,call,$008003,JSL,unmapped,,"",unmapped,unmapped,1' in inventory.splitlines(), inventory
        assert '$008020,vector,$007FEA,"Native NMI",entry_only,$000020,"' in inventory, inventory
        assert 'digraph' not in inventory, inventory
        hotspots = check([cfg, str(calls_path), '512', '--hotspots'], 0)
        assert hotspots.splitlines() == [
            'address,incoming_calls,jsr_sites,jsl_sites,status,rom_offset,first_opcode,first_bytes,second_opcode,entry_sequence',
            '$009234,1,1,0,candidate,$001234,unknown,"00 00 00 00 00 00 00 00",unknown,"?"',
            '$125678,1,0,1,unmapped,,unmapped,"",unknown,""',
        ], hotspots
        assert 'digraph' not in hotspots, hotspots
        sequence_rom = bytearray(calls_rom)
        sequence_rom[0x1234:0x123a] = bytes([0x78, 0xC2, 0x30, 0xEA, 0x60, 0xEA])
        sequence_path = root / 'hotspot-sequence.sfc'
        sequence_path.write_bytes(sequence_rom)
        sequence_output = check([cfg, str(sequence_path), '512', '--hotspots'], 0)
        assert ',"SEI REP NOP RTS"' in sequence_output, sequence_output
        # A return or jump must not be interpreted as linear fallthrough.
        boundary_rom = bytearray(calls_rom)
        boundary_rom[0x1234:0x1236] = bytes([0x60, 0xEA])
        boundary_path = root / 'hotspot-boundary.sfc'
        boundary_path.write_bytes(boundary_rom)
        boundary = check([cfg, str(boundary_path), '512', '--hotspots'], 0)
        assert '$009234,1,1,0,candidate,$001234,RTS,"60 EA' in boundary, boundary
        assert ',terminal' in boundary.splitlines()[1], boundary
        boundary_rom[0x1234:0x1238] = bytes([0x4C, 0x00, 0x80, 0xEA])
        boundary_path.write_bytes(boundary_rom)
        boundary = check([cfg, str(boundary_path), '512', '--hotspots'], 0)
        assert '$009234,1,1,0,candidate,$001234,JMP,"4C 00 80 EA' in boundary, boundary
        assert ',jump' in boundary.splitlines()[1], boundary
        # Calls must terminate prefix inspection; callee state is unknown.
        for call_bytes, call_name in (
            (bytes([0x20, 0x00, 0x80, 0xEA]), 'JSR'),
            (bytes([0x22, 0x00, 0x80, 0x00, 0xEA]), 'JSL'),
        ):
            call_boundary_rom = bytearray(calls_rom)
            call_boundary_rom[0x1234:0x1234 + len(call_bytes)] = call_bytes
            boundary_path.write_bytes(call_boundary_rom)
            call_boundary = check([cfg, str(boundary_path), '512', '--hotspots'], 0)
            assert ',"' + call_name + '"' in call_boundary, call_boundary
        # SEP establishes 8-bit immediate widths even with unknown entry E.
        width_rom = bytearray(calls_rom)
        width_rom[0x1234:0x123b] = bytes([0xE2, 0x30, 0xA9, 0x12, 0xA2, 0x34, 0x60])
        width_path = root / 'hotspot-width.sfc'
        width_path.write_bytes(width_rom)
        width_output = check([cfg, str(width_path), '512', '--hotspots'], 0)
        assert ',"SEP LDA LDX RTS"' in width_output, width_output
        # REP cannot establish 16-bit width without knowing native mode.
        width_rom[0x1234:0x123a] = bytes([0xE2, 0x20, 0xC2, 0x20, 0xA9, 0x12])
        width_path.write_bytes(width_rom)
        width_output = check([cfg, str(width_path), '512', '--hotspots'], 0)
        assert ',"SEP REP ?"' in width_output, width_output
        # SEC; XCE proves emulation mode; REP cannot widen M or X.
        emulation_rom = bytearray(calls_rom)
        emulation_rom[0x1234:0x123a] = bytes([0x38, 0xFB, 0xC2, 0x30, 0xA9, 0x42])
        emulation_path = root / 'hotspot-emulation.sfc'
        emulation_path.write_bytes(emulation_rom)
        emulation_output = check([cfg, str(emulation_path), '512', '--hotspots'], 0)
        assert ',"SEC XCE REP LDA"' in emulation_output, emulation_output
        # Native REP establishes 16-bit index width for LDX/LDY.
        index_rom = bytearray(calls_rom)
        index_rom[0x1234:0x123b] = bytes([0x18, 0xFB, 0xC2, 0x10, 0xA2, 0x34, 0x12])
        index_path = root / 'hotspot-index.sfc'
        index_path.write_bytes(index_rom)
        index_output = check([cfg, str(index_path), '512', '--hotspots'], 0)
        assert ',"CLC XCE REP LDX"' in index_output, index_output
        # ADC changes carry: XCE after arithmetic must not assume native mode.
        carry_rom = bytearray(calls_rom)
        carry_rom[0x1234:0x123a] = bytes([0xE2, 0x20, 0x18, 0x69, 0x01, 0xFB])
        carry_path = root / 'hotspot-carry.sfc'
        carry_path.write_bytes(carry_rom)
        carry_output = check([cfg, str(carry_path), '512', '--hotspots'], 0)
        assert ',"SEP CLC ADC ?"' in carry_output, carry_output
        # CLC; XCE proves native mode, so REP can establish 16-bit M.
        native_rom = bytearray(calls_rom)
        native_rom[0x1234:0x123b] = bytes([0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x34, 0x12])
        native_path = root / 'hotspot-native.sfc'
        native_path.write_bytes(native_rom)
        native_output = check([cfg, str(native_path), '512', '--hotspots'], 0)
        assert ',"CLC XCE REP LDA"' in native_output, native_output
        # XCE without a known carry must stop rather than assume native mode.
        native_rom[0x1234:0x1238] = bytes([0xFB, 0xC2, 0x20, 0xA9])
        native_path.write_bytes(native_rom)
        native_output = check([cfg, str(native_path), '512', '--hotspots'], 0)
        assert ',"?"' in native_output, native_output
        # Fixed-width store instructions can be decoded without knowing M/X.
        store_rom = bytearray(calls_rom)
        store_rom[0x1234:0x123b] = bytes([0x78, 0x8D, 0x00, 0x21, 0xE8, 0x60, 0xEA])
        store_path = root / 'hotspot-stores.sfc'
        store_path.write_bytes(store_rom)
        stores = check([cfg, str(store_path), '512', '--hotspots'], 0)
        assert ',"SEI STA INX RTS"' in stores, stores
        check([cfg, str(calls_path), '512', '--unknown'], 2)
        cfg_calls_output = check([cfg, str(calls_path)], 0)
        assert '"$008000" -> "$009234" [label="JSR",style=dashed]' in cfg_calls_output, cfg_calls_output
        assert '"$008003" -> "$125678" [label="JSL",style=dashed]' in cfg_calls_output, cfg_calls_output
        assert '"$008000" -> "$008003" [label="return-assumed"]' in cfg_calls_output, cfg_calls_output
        assert '"$008003" -> "$008007" [label="return-assumed"]' in cfg_calls_output, cfg_calls_output
        assert '"$009234" [shape=ellipse,style=dashed]' in cfg_calls_output, cfg_calls_output
        assert '"$125678" [shape=ellipse,style=dashed]' not in cfg_calls_output, cfg_calls_output
        # RTS and RTL are terminal return nodes, not fallthrough edges.
        for opcode in (0x60, 0x6B):
            return_rom = bytearray(rom)
            return_rom[:2] = bytes([opcode, 0xEA])
            return_path = root / f'return-{opcode:02x}.sfc'
            return_path.write_bytes(return_rom)
            return_output = check([cfg, str(return_path)], 0)
            assert '"$008000" [shape=doublecircle,label="$008000 return"]' in return_output, return_output
            assert '"$008000" -> "$008001"' not in return_output, return_output
        # Indirect JMP forms have unknown targets and must not fall through.
        for opcode in (0x6C, 0x7C, 0xDC):
            indirect_rom = bytearray(rom)
            indirect_rom[:5] = bytes([opcode, 0x00, 0x90, 0xEA, 0xDB])
            indirect_path = root / f'indirect-{opcode:02x}.sfc'
            indirect_path.write_bytes(indirect_rom)
            indirect_output = check([cfg, str(indirect_path)], 0)
            assert '"$008000" [shape=diamond,label="$008000 indirect jump"]' in indirect_output, indirect_output
            assert '"$008000" -> ' not in indirect_output, indirect_output
        # STP and BRK are terminal trap/stop nodes in the partial CFG.
        for opcode in (0xDB, 0x00):
            stop_rom = bytearray(rom)
            stop_rom[:3] = bytes([opcode, 0xEA, 0xEA])
            stop_path = root / f'stop-{opcode:02x}.sfc'
            stop_path.write_bytes(stop_rom)
            stop_output = check([cfg, str(stop_path)], 0)
            assert '"$008000" [shape=octagon,label="$008000 stop/trap"]' in stop_output, stop_output
            assert '"$008000" -> ' not in stop_output, stop_output
        # A long jump into $7E/$7F targets WRAM, never ROM.
        for bank in (0x7E, 0x7F):
            wram_rom = bytearray(rom)
            wram_rom[:5] = bytes([0x5C, 0x00, 0x80, bank, 0xDB])
            wram_path = root / f'wram-jump-{bank:02x}.sfc'
            wram_path.write_bytes(wram_rom)
            wram_output = check([cfg, str(wram_path)], 0)
            assert f'"$008000" -> "${bank:02X}8000"' not in wram_output, wram_output
            assert 'Unresolved=1' in wram_output, wram_output
        vector_cfg = check([cfg, str(valid)], 0)
        assert '"vector_7ffc" -> "$008000" [style=dotted,label="entry only"]' in vector_cfg
        assert '"vector_7ffa" -> "$008010" [style=dotted,label="entry only"]' in vector_cfg
        assert '"vector_7fea" -> "$008020" [style=dotted,label="entry only"]' in vector_cfg
        assert '"vector_7fea" [shape=box,label="Native NMI"]' in vector_cfg
        assert '"$008010" [peripheries=2]' in vector_cfg
        assert '"$008020" [peripheries=2]' in vector_cfg
        # Vector targets are annotated, not traversed without a known CPU state.
        assert '"$008010" -> ' not in vector_cfg, vector_cfg
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
        # XCE cannot infer CPU mode after an instruction with unknown carry.
        unknown_carry_rom = bytearray(rom)
        unknown_carry_rom[:6] = bytes([0x69, 0x01, 0xFB, 0xA9, 0x12, 0xDB])
        unknown_carry_path = root / 'unknown-carry.sfc'
        unknown_carry_path.write_bytes(unknown_carry_rom)
        unknown_carry_output = check([cfg, str(unknown_carry_path)], 0)
        assert '"$008000" -> "$008002" [label="next"]' in unknown_carry_output
        assert '"$008002" -> "$008003"' not in unknown_carry_output
        # PLP restores flags that cannot be determined without stack tracking.
        plp_rom = bytearray(rom)
        plp_rom[:4] = bytes([0x28, 0xA9, 0x12, 0xDB])
        plp_path = root / 'plp.sfc'
        plp_path.write_bytes(plp_rom)
        plp_output = check([cfg, str(plp_path)], 0)
        assert '"$008000" -> "$008001"' not in plp_output
        # Index increments must preserve carry, so XCE remains decodable.
        index_rom = bytearray(rom)
        index_rom[:6] = bytes([0x18, 0xE8, 0xFB, 0xA9, 0x12, 0xDB])
        index_path = root / 'index-carry.sfc'
        index_path.write_bytes(index_rom)
        index_output = check([cfg, str(index_path)], 0)
        assert '"$008002" -> "$008003" [label="next"]' in index_output, index_output
        assert '"$008003" -> "$008005" [label="next"]' in index_output, index_output
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
