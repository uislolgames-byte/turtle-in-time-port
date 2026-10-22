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

        # Callgraph records direct jump targets, but does not claim they are decoded.
        graph_rom = bytearray(rom)
        graph_rom[:6] = bytes([0x4C, 0x10, 0x80, 0xEA, 0xEA, 0xDB])
        graph_path = root / 'callgraph-jmp.sfc'
        graph_path.write_bytes(graph_rom)
        graph_output = check([callgraph, str(graph_path)], 0)
        assert '"$008000" -> "$008010" [label="JMP"]' in graph_output, graph_output
        assert 'Entries=1 Edges=1' in graph_output, graph_output
        graph_long_rom = bytearray(rom)
        graph_long_rom[:5] = bytes([0x5C, 0x10, 0x80, 0x01, 0xDB])
        graph_long_path = root / 'callgraph-jml.sfc'
        graph_long_path.write_bytes(graph_long_rom)
        graph_long_output = check([callgraph, str(graph_long_path)], 0)
        assert '"$008000" -> "$018010" [label="JML"]' in graph_long_output, graph_long_output
        # Invalid targets are shown as candidates, not decoded as proven code.
        graph_invalid_rom = bytearray(rom)
        graph_invalid_rom[:3] = bytes([0x4C, 0x00, 0x20])
        graph_invalid_path = root / 'callgraph-jmp-invalid.sfc'
        graph_invalid_path.write_bytes(graph_invalid_rom)
        graph_invalid_output = check([callgraph, str(graph_invalid_path)], 0)
        assert '"$008000" -> "$002000" [label="JMP"]' in graph_invalid_output, graph_invalid_output
        # Callgraph can skip known arithmetic/index instructions before a call.
        graph_prefix_rom = bytearray(rom)
        graph_prefix_rom[:11] = bytes([
            0xA0, 0x02, 0xE8, 0x69, 0x01, 0xA9, 0x33,
            0x20, 0x10, 0x80, 0xDB,
        ])
        graph_prefix_path = root / 'callgraph-prefix.sfc'
        graph_prefix_path.write_bytes(graph_prefix_rom)
        graph_prefix_output = check([callgraph, str(graph_prefix_path)], 0)
        assert '"$008007" -> "$008010" [label="JSR"]' in graph_prefix_output, graph_prefix_output
        # A conditional branch ends the conservative linear path.
        graph_branch_rom = bytearray(rom)
        graph_branch_rom[:6] = bytes([0x90, 0x01, 0x20, 0x10, 0x80, 0xDB])
        graph_branch_path = root / 'callgraph-branch-stop.sfc'
        graph_branch_path.write_bytes(graph_branch_rom)
        graph_branch_output = check([callgraph, str(graph_branch_path)], 0)
        assert 'label="JSR"' not in graph_branch_output, graph_branch_output
        # Relative branches are recorded as candidates without exploring unknown paths.
        branch_target_rom = bytearray(rom)
        branch_target_rom[:2] = bytes([0x90, 0x02])
        branch_target_path = root / 'callgraph-bcc-target.sfc'
        branch_target_path.write_bytes(branch_target_rom)
        branch_target_output = check([callgraph, str(branch_target_path)], 0)
        assert '"$008000" -> "$008004" [label="BCC"]' in branch_target_output, branch_target_output
        assert 'Decoded=1' in branch_target_output, branch_target_output
        # Carry is unknown at RESET and after arithmetic. XCE must stop
        # before an unproven CPU mode can produce spurious call edges.
        uncertain_prefixes = [bytes([0xFB])]
        for arithmetic in (0x69, 0xE9, 0xC9):
            uncertain_prefixes.append(bytes([0x18, arithmetic, 0x01, 0xFB]))
        for index, prefix in enumerate(uncertain_prefixes):
            uncertain_rom = bytearray(rom)
            program = prefix + bytes([0x20, 0x10, 0x80, 0xDB])
            uncertain_rom[:len(program)] = program
            uncertain_path = root / f'callgraph-unknown-carry-{index}.sfc'
            uncertain_path.write_bytes(uncertain_rom)
            output = check([callgraph, str(uncertain_path)], 0)
            assert 'label="JSR"' not in output, output
            expected_decoded = 0 if index == 0 else 2
            assert f'Decoded={expected_decoded} ' in output, output

        # Instructions that explicitly set C restore knowledge after ADC.
        # REP/SEP affect C even in emulation mode. Width-only masks preserve C.
        known_carry_cases = (
            (bytes([0x18]), False),
            (bytes([0x38]), True),
            (bytes([0xC2, 0x01]), False),
            (bytes([0xE2, 0x01]), True),
            (bytes([0x18, 0xE2, 0x30]), False),
            (bytes([0x38, 0xC2, 0x30]), True),
        )
        for index, (setter, emulation) in enumerate(known_carry_cases):
            known_rom = bytearray(rom)
            prefix = bytes([0x69, 0x01]) + setter + bytes([0xFB, 0xC2, 0x30])
            # REP widens immediates only after XCE proves native mode.
            loads = bytes([0xA9, 0x34] + ([] if emulation else [0x12]))
            loads += bytes([0xA2, 0x56] + ([] if emulation else [0x78]))
            source = 0x8000 + len(prefix) + len(loads)
            program = prefix + loads + bytes([0x20, 0x10, 0x80, 0xDB])
            known_rom[:len(program)] = program
            known_path = root / f'callgraph-known-carry-{index}.sfc'
            known_path.write_bytes(known_rom)
            output = check([callgraph, str(known_path)], 0)
            assert f'"${source:06X}" -> "$008010" [label="JSR"]' in output, output

        # Logical operations and index increments preserve a known carry.
        preserving_rom = bytearray(rom)
        preserving_program = bytes([
            0x18, 0x29, 0xFF, 0x09, 0x00, 0x49, 0x00, 0xE8,
            0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12, 0x20, 0x10, 0x80, 0xDB,
        ])
        preserving_rom[:len(preserving_program)] = preserving_program
        preserving_path = root / 'callgraph-preserved-carry.sfc'
        preserving_path.write_bytes(preserving_rom)
        output = check([callgraph, str(preserving_path)], 0)
        assert '"$00800E" -> "$008010" [label="JSR"]' in output, output
        long_branch_rom = bytearray(rom)
        long_branch_rom[:3] = bytes([0x82, 0x10, 0x00])
        long_branch_path = root / 'callgraph-brl-target.sfc'
        long_branch_path.write_bytes(long_branch_rom)
        long_branch_output = check([callgraph, str(long_branch_path)], 0)
        assert '"$008000" -> "$008013" [label="BRL"]' in long_branch_output, long_branch_output
        backwards_branch_rom = bytearray(rom)
        backwards_branch_rom[:2] = bytes([0x80, 0xFE])
        backwards_branch_path = root / 'callgraph-bra-back.sfc'
        backwards_branch_path.write_bytes(backwards_branch_rom)
        backwards_branch_output = check([callgraph, str(backwards_branch_path)], 0)
        assert '"$008000" -> "$008000" [label="BRA"]' in backwards_branch_output, backwards_branch_output
        # Unconditional BRA/BRL preserve known CPU width state and may be
        # traversed safely, unlike conditional branches with unknown flags.
        for name, branch, target in (
            ('bra', bytes([0x80, 0x02]), 0x8004),
            ('brl', bytes([0x82, 0x02, 0x00]), 0x8005),
        ):
            follow_rom = bytearray(rom)
            follow_rom[:16] = bytes([0xDB] * 16)
            follow_rom[:len(branch)] = branch
            follow_rom[target - 0x8000:target - 0x8000 + 4] = bytes([0x20, 0x10, 0x80, 0xDB])
            follow_path = root / f'callgraph-follow-{name}.sfc'
            follow_path.write_bytes(follow_rom)
            output = check([callgraph, str(follow_path)], 0)
            assert f'"${target:06X}" -> "$008010" [label="JSR"]' in output, output
            assert 'Decoded=3' in output, output
        # Self-loops must terminate through visited-address tracking.
        loop_follow = check([callgraph, str(backwards_branch_path)], 0)
        assert 'Decoded=1' in loop_follow, loop_follow
        # Out-of-ROM branches are reported but never traversed.
        invalid_follow_rom = bytearray(rom)
        invalid_follow_rom[:2] = bytes([0x80, 0x80])
        invalid_follow_path = root / 'callgraph-follow-invalid.sfc'
        invalid_follow_path.write_bytes(invalid_follow_rom)
        invalid_follow_output = check([callgraph, str(invalid_follow_path)], 0)
        assert 'Decoded=1' in invalid_follow_output, invalid_follow_output
        # JMP absolute preserves PBR and CPU width state. Follow the target,
        # never the bytes immediately after the jump.
        jump_rom = bytearray(rom)
        jump_rom[:10] = bytes([
            0x18, 0xFB, 0xC2, 0x30, 0x4C, 0x10, 0x80,
            0x20, 0xEF, 0xBE,  # unreachable fake JSR
        ])
        jump_rom[0x10:0x17] = bytes([
            0xA9, 0x34, 0x12, 0x20, 0x20, 0x80, 0xDB,
        ])
        jump_path = root / 'callgraph-follow-jmp.sfc'
        jump_path.write_bytes(jump_rom)
        output = check([callgraph, str(jump_path)], 0)
        assert '"$008004" -> "$008010" [label="JMP"]' in output, output
        assert '"$008013" -> "$008020" [label="JSR"]' in output, output
        assert '$00BEEF' not in output, output
        assert 'Decoded=7 ' in output, output
        limited = check([callgraph, str(jump_path), '4'], 0)
        assert 'label="JSR"' not in limited, limited
        assert 'Decoded=4 ' in limited, limited
        # Invalid targets remain candidates; self-loops terminate via visited.
        for target in (0x2000, 0x8000):
            bounded_rom = bytearray(rom)
            bounded_rom[:6] = bytes([
                0x4C, target & 0xFF, target >> 8, 0x20, 0xEF, 0xBE,
            ])
            bounded_path = root / f'callgraph-jmp-bounded-{target:04x}.sfc'
            bounded_path.write_bytes(bounded_rom)
            output = check([callgraph, str(bounded_path)], 0)
            assert f'"$008000" -> "${target:06X}" [label="JMP"]' in output, output
            assert 'label="JSR"' not in output, output
            assert 'Decoded=1 ' in output, output
        # JML follows a valid target across program banks, without decoding
        # unreachable bytes after the jump or inventing callee CPU state.
        long_jump_rom = bytearray(rom) + bytearray(32768)
        long_jump_rom[:7] = bytes([0x18, 0xFB, 0xC2, 0x30, 0x5C, 0x10, 0x80])
        long_jump_rom[7] = 0x01
        long_jump_rom[8:11] = bytes([0x20, 0xEF, 0xBE])
        long_jump_rom[0x8010:0x8018] = bytes([
            0xA9, 0x34, 0x12, 0x20, 0x20, 0x80, 0xDB, 0xEA,
        ])
        long_jump_path = root / 'callgraph-follow-jml-bank.sfc'
        long_jump_path.write_bytes(long_jump_rom)
        long_jump_output = check([callgraph, str(long_jump_path)], 0)
        assert '"$008004" -> "$018010" [label="JML"]' in long_jump_output, long_jump_output
        assert '"$018013" -> "$018020" [label="JSR"]' in long_jump_output, long_jump_output
        assert '$00BEEF' not in long_jump_output, long_jump_output
        assert 'Decoded=7 ' in long_jump_output, long_jump_output
        # A JML target outside mapped ROM is only reported, never traversed.
        bad_long_jump_rom = bytearray(rom)
        bad_long_jump_rom[:5] = bytes([0x5C, 0x10, 0x80, 0x01, 0xDB])
        bad_long_jump_path = root / 'callgraph-follow-jml-unmapped.sfc'
        bad_long_jump_path.write_bytes(bad_long_jump_rom)
        bad_long_jump_output = check([callgraph, str(bad_long_jump_path)], 0)
        assert '"$008000" -> "$018010" [label="JML"]' in bad_long_jump_output, bad_long_jump_output
        assert 'Decoded=1 ' in bad_long_jump_output, bad_long_jump_output
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
        read_rom[:16] = bytes([
            0xA9, 0x2A, 0x8D, 0x20, 0x00, 0xA9, 0x00,
            0xAD, 0x20, 0x00, 0xD0, 0x01, 0xFF, 0xDB,
            0xEA, 0xEA,
        ])
        assert len(read_rom) == len(rom), "Synthetic ROM size must not change"
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
        # ADC carry and zero wrap in 8-bit mode.
        adc_rom = bytearray(rom)
        adc_program = bytes([0xA9, 0xFF, 0x18, 0x69, 0x01, 0xDB])
        adc_rom[:len(adc_program)] = adc_program
        adc_path = root / 'execute-adc.sfc'
        adc_path.write_bytes(adc_rom)
        adc_output = check([executor, str(adc_path), '20'], 0)
        assert 'reason=stp' in adc_output and 'a=$0000' in adc_output, adc_output
        assert 'c=1 z=1' in adc_output, adc_output
        # SBC borrows with carry clear and sets carry when no borrow occurs.
        sbc_rom = bytearray(rom)
        sbc_program = bytes([0xA9, 0x05, 0x38, 0xE9, 0x03, 0xDB])
        sbc_rom[:len(sbc_program)] = sbc_program
        sbc_path = root / 'execute-sbc.sfc'
        sbc_path.write_bytes(sbc_rom)
        sbc_output = check([executor, str(sbc_path), '20'], 0)
        assert 'a=$0002' in sbc_output and 'c=1 z=0' in sbc_output, sbc_output
        # CMP sets flags but does not overwrite A.
        cmp_rom = bytearray(rom)
        cmp_program = bytes([0xA9, 0x07, 0xC9, 0x07, 0xF0, 0x01, 0xFF, 0xDB])
        cmp_rom[:len(cmp_program)] = cmp_program
        cmp_path = root / 'execute-cmp.sfc'
        cmp_path.write_bytes(cmp_rom)
        cmp_output = check([executor, str(cmp_path), '20'], 0)
        assert 'reason=stp' in cmp_output and 'a=$0007' in cmp_output, cmp_output
        assert 'c=1 z=1' in cmp_output, cmp_output
        # 16-bit ADC carries across the full accumulator.
        adc16_rom = bytearray(rom)
        adc16_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0xFF, 0xFF,
            0x18, 0x69, 0x01, 0x00, 0xDB,
        ])
        adc16_rom[:len(adc16_program)] = adc16_program
        adc16_path = root / 'execute-adc16.sfc'
        adc16_path.write_bytes(adc16_rom)
        adc16_output = check([executor, str(adc16_path), '20'], 0)
        assert 'a=$0000' in adc16_output and 'c=1 z=1' in adc16_output, adc16_output
        # Signed overflow after 0x7F + 1, with BMI and BVS taken.
        flags_rom = bytearray(rom)
        flags_program = bytes([
            0xA9, 0x7F, 0x18, 0x69, 0x01,
            0x30, 0x01, 0xFF, 0x70, 0x01, 0xFF, 0xDB,
        ])
        flags_rom[:len(flags_program)] = flags_program
        flags_path = root / 'execute-flags.sfc'
        flags_path.write_bytes(flags_rom)
        flags_output = check([executor, str(flags_path), '20'], 0)
        assert 'reason=stp' in flags_output and 'a=$0080' in flags_output, flags_output
        assert 'n=1 v=1' in flags_output, flags_output
        # CLV clears overflow and BVC follows the cleared flag.
        clv_rom = bytearray(rom)
        clv_program = bytes([
            0xA9, 0x7F, 0x18, 0x69, 0x01,
            0xB8, 0x50, 0x01, 0xFF, 0xDB,
        ])
        clv_rom[:len(clv_program)] = clv_program
        clv_path = root / 'execute-clv.sfc'
        clv_path.write_bytes(clv_rom)
        clv_output = check([executor, str(clv_path), '20'], 0)
        assert 'reason=stp' in clv_output and 'n=1 v=0' in clv_output, clv_output
        # BPL branches after a nonnegative load.
        bpl_rom = bytearray(rom)
        bpl_program = bytes([0xA9, 0x01, 0x10, 0x01, 0xFF, 0xDB])
        bpl_rom[:len(bpl_program)] = bpl_program
        bpl_path = root / 'execute-bpl.sfc'
        bpl_path.write_bytes(bpl_rom)
        bpl_output = check([executor, str(bpl_path), '20'], 0)
        assert 'reason=stp' in bpl_output and 'n=0' in bpl_output, bpl_output
        # CMP updates N without changing accumulator.
        cmpneg_rom = bytearray(rom)
        cmpneg_program = bytes([0xA9, 0x00, 0xC9, 0x01, 0xDB])
        cmpneg_rom[:len(cmpneg_program)] = cmpneg_program
        cmpneg_path = root / 'execute-cmp-negative.sfc'
        cmpneg_path.write_bytes(cmpneg_rom)
        cmpneg_output = check([executor, str(cmpneg_path), '20'], 0)
        assert 'a=$0000' in cmpneg_output and 'c=0 z=0 n=1' in cmpneg_output, cmpneg_output
        # Logical operations preserve carry and overflow, and update N/Z.
        logic_rom = bytearray(rom)
        logic_program = bytes([
            0x38, 0xE2, 0x40, 0xA9, 0xF0, 0x29, 0x0F,
            0x09, 0x80, 0x49, 0x80, 0xDB,
        ])
        logic_rom[:len(logic_program)] = logic_program
        logic_path = root / 'execute-logic.sfc'
        logic_path.write_bytes(logic_rom)
        logic_output = check([executor, str(logic_path), '20'], 0)
        assert 'reason=stp' in logic_output and 'a=$0000' in logic_output, logic_output
        assert 'c=1 z=1 n=0 v=1' in logic_output, logic_output
        # 16-bit EOR must consume a two-byte operand.
        logic16_rom = bytearray(rom)
        logic16_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x00, 0x80,
            0x49, 0x00, 0x80, 0xDB,
        ])
        logic16_rom[:len(logic16_program)] = logic16_program
        logic16_path = root / 'execute-logic16.sfc'
        logic16_path.write_bytes(logic16_rom)
        logic16_output = check([executor, str(logic16_path), '20'], 0)
        assert 'reason=stp' in logic16_output and 'a=$0000' in logic16_output, logic16_output
        assert 'm=16' in logic16_output and 'z=1 n=0' in logic16_output, logic16_output
        # INX wraps in 8-bit index mode and preserves carry.
        inx_rom = bytearray(rom)
        inx_program = bytes([0x38, 0xA2, 0xFF, 0xE8, 0xDB])
        inx_rom[:len(inx_program)] = inx_program
        inx_path = root / 'execute-inx.sfc'
        inx_path.write_bytes(inx_rom)
        inx_output = check([executor, str(inx_path), '20'], 0)
        assert 'reason=stp' in inx_output and 'x=$0000' in inx_output, inx_output
        assert 'c=1 z=1 n=0' in inx_output, inx_output
        # DEY wraps zero to 0xFF and sets Negative in 8-bit mode.
        dey_rom = bytearray(rom)
        dey_program = bytes([0xA0, 0x00, 0x88, 0xDB])
        dey_rom[:len(dey_program)] = dey_program
        dey_path = root / 'execute-dey.sfc'
        dey_path.write_bytes(dey_rom)
        dey_output = check([executor, str(dey_path), '20'], 0)
        assert 'y=$00FF' in dey_output and 'z=0 n=1' in dey_output, dey_output
        # INC A in 8-bit mode retains the high accumulator byte.
        inc_rom = bytearray(rom)
        inc_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x34, 0x12,
            0xE2, 0x20, 0x1A, 0xDB,
        ])
        inc_rom[:len(inc_program)] = inc_program
        inc_path = root / 'execute-inca.sfc'
        inc_path.write_bytes(inc_rom)
        inc_output = check([executor, str(inc_path), '20'], 0)
        assert 'a=$1235' in inc_output and 'm=8' in inc_output, inc_output
        # 16-bit DEX wraps from zero to 0xFFFF.
        dex16_rom = bytearray(rom)
        dex16_program = bytes([
            0x18, 0xFB, 0xC2, 0x10, 0xA2, 0x00, 0x00, 0xCA, 0xDB,
        ])
        dex16_rom[:len(dex16_program)] = dex16_program
        dex16_path = root / 'execute-dex16.sfc'
        dex16_path.write_bytes(dex16_rom)
        dex16_output = check([executor, str(dex16_path), '20'], 0)
        assert 'x=$FFFF' in dex16_output and 'xbits=16' in dex16_output, dex16_output
        assert 'z=0 n=1' in dex16_output, dex16_output
        # TAX truncates a 16-bit accumulator to an 8-bit X register.
        tax_rom = bytearray(rom)
        tax_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x80, 0x12,
            0xAA, 0xDB,
        ])
        tax_rom[:len(tax_program)] = tax_program
        tax_path = root / 'execute-tax.sfc'
        tax_path.write_bytes(tax_rom)
        tax_output = check([executor, str(tax_path), '20'], 0)
        assert 'reason=stp' in tax_output and 'a=$1280' in tax_output, tax_output
        assert 'x=$0080' in tax_output and 'n=1' in tax_output, tax_output
        # TXA into 8-bit A retains the hidden high accumulator byte.
        txa_rom = bytearray(rom)
        txa_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x34, 0x12,
            0xE2, 0x20, 0xA2, 0xFE, 0x8A, 0xDB,
        ])
        txa_rom[:len(txa_program)] = txa_program
        txa_path = root / 'execute-txa.sfc'
        txa_path.write_bytes(txa_rom)
        txa_output = check([executor, str(txa_path), '20'], 0)
        assert 'a=$12FE' in txa_output and 'n=1' in txa_output, txa_output
        # TAY and TYX in 16-bit index mode preserve both bytes.
        tyx_rom = bytearray(rom)
        tyx_program = bytes([
            0x18, 0xFB, 0xC2, 0x30, 0xA9, 0xEF, 0xBE,
            0xA8, 0xBB, 0xDB,
        ])
        tyx_rom[:len(tyx_program)] = tyx_program
        tyx_path = root / 'execute-tyx.sfc'
        tyx_path.write_bytes(tyx_rom)
        tyx_output = check([executor, str(tyx_path), '20'], 0)
        assert 'x=$BEEF' in tyx_output and 'y=$BEEF' in tyx_output, tyx_output
        assert 'xbits=16' in tyx_output and 'n=1' in tyx_output, tyx_output
        # TXY and TYA round-trip in emulation mode.
        txy_rom = bytearray(rom)
        txy_program = bytes([0xA2, 0x42, 0x9B, 0x98, 0xDB])
        txy_rom[:len(txy_program)] = txy_program
        txy_path = root / 'execute-txy.sfc'
        txy_path.write_bytes(txy_rom)
        txy_output = check([executor, str(txy_path), '20'], 0)
        assert 'a=$0042' in txy_output and 'y=$0042' in txy_output, txy_output
        # Direct Page STA/LDA round trip with a one-byte address.
        dp_rom = bytearray(rom)
        dp_program = bytes([
            0xA9, 0xA5, 0x85, 0x42, 0xA9, 0x00,
            0xA5, 0x42, 0xDB,
        ])
        dp_rom[:len(dp_program)] = dp_program
        dp_path = root / 'execute-direct-page.sfc'
        dp_path.write_bytes(dp_rom)
        dp_output = check([executor, str(dp_path), '20'], 0)
        assert 'reason=stp' in dp_output and 'a=$00A5' in dp_output, dp_output
        assert 'stores=1 last_addr=$0042 last_value=$00A5' in dp_output, dp_output
        # Direct Page 16-bit store/load uses adjacent low WRAM bytes.
        dp16_rom = bytearray(rom)
        dp16_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0xCD, 0xAB,
            0x85, 0xFE, 0xA9, 0x00, 0x00, 0xA5, 0xFE, 0xDB,
        ])
        dp16_rom[:len(dp16_program)] = dp16_program
        dp16_path = root / 'execute-direct-page16.sfc'
        dp16_path.write_bytes(dp16_rom)
        dp16_output = check([executor, str(dp16_path), '20'], 0)
        assert 'reason=stp' in dp16_output and 'a=$ABCD' in dp16_output, dp16_output
        assert 'm=16' in dp16_output and 'stores=1' in dp16_output, dp16_output
        # TCD moves the full 16-bit accumulator to D; Direct Page offsets
        # are relative to D even when the accumulator later becomes 8-bit.
        d_rom = bytearray(rom)
        d_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x00, 0x01,
            0x5B, 0xE2, 0x20, 0xA9, 0x5A, 0x85, 0x10,
            0xA9, 0x00, 0xA5, 0x10, 0xDB,
        ])
        d_rom[:len(d_program)] = d_program
        d_path = root / 'execute-d-register.sfc'
        d_path.write_bytes(d_rom)
        d_output = check([executor, str(d_path), '30'], 0)
        assert 'reason=stp' in d_output and 'd=$0100' in d_output, d_output
        assert 'a=$015A' in d_output and 'last_addr=$0110' in d_output, d_output
        # TDC copies D to the full accumulator, regardless of M width.
        tdc_rom = bytearray(rom)
        tdc_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x34, 0x12,
            0x5B, 0xE2, 0x20, 0xA9, 0x00, 0x7B, 0xDB,
        ])
        tdc_rom[:len(tdc_program)] = tdc_program
        tdc_path = root / 'execute-tdc.sfc'
        tdc_path.write_bytes(tdc_rom)
        tdc_output = check([executor, str(tdc_path), '30'], 0)
        assert 'a=$1234' in tdc_output and 'd=$1234' in tdc_output, tdc_output
        assert 'm=8' in tdc_output and 'n=0' in tdc_output, tdc_output
        # Direct Page outside modeled WRAM is rejected, not silently aliased.
        dp_oob_rom = bytearray(rom)
        dp_oob_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x00, 0x21,
            0x5B, 0xA5, 0x00, 0xDB,
        ])
        dp_oob_rom[:len(dp_oob_program)] = dp_oob_program
        dp_oob_path = root / 'execute-dp-oob.sfc'
        dp_oob_path.write_bytes(dp_oob_rom)
        dp_oob_output = check([executor, str(dp_oob_path), '30'], 0)
        assert 'reason=unsupported' in dp_oob_output, dp_oob_output
        # Absolute indexed X/Y accesses to low WRAM.
        idx_rom = bytearray(rom)
        idx_program = bytes([
            0xA2, 0x02, 0xA0, 0x03, 0xA9, 0x6B,
            0x9D, 0x20, 0x00, 0xA9, 0x00,
            0xB9, 0x1F, 0x00, 0xDB,
        ])
        idx_rom[:len(idx_program)] = idx_program
        idx_path = root / 'execute-indexed.sfc'
        idx_path.write_bytes(idx_rom)
        idx_output = check([executor, str(idx_path), '30'], 0)
        assert 'reason=stp' in idx_output and 'a=$006B' in idx_output, idx_output
        assert 'last_addr=$0022' in idx_output, idx_output
        # Direct-page indexed access with D set in native mode.
        dpidx_rom = bytearray(rom)
        dpidx_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x00, 0x01,
            0x5B, 0xE2, 0x20, 0xA2, 0x04, 0xA9, 0xAB,
            0x95, 0x10, 0xA9, 0x00, 0xB5, 0x10, 0xDB,
        ])
        dpidx_rom[:len(dpidx_program)] = dpidx_program
        dpidx_path = root / 'execute-dp-indexed.sfc'
        dpidx_path.write_bytes(dpidx_rom)
        dpidx_output = check([executor, str(dpidx_path), '30'], 0)
        assert 'a=$01AB' in dpidx_output and 'last_addr=$0114' in dpidx_output, dpidx_output
        # Emulation-mode direct-page indexed address wraps within its page.
        wrap_rom = bytearray(rom)
        wrap_program = bytes([
            0xA2, 0x02, 0xA9, 0x55, 0x95, 0xFF,
            0xA9, 0x00, 0xB5, 0xFF, 0xDB,
        ])
        wrap_rom[:len(wrap_program)] = wrap_program
        wrap_path = root / 'execute-dp-wrap.sfc'
        wrap_path.write_bytes(wrap_rom)
        wrap_output = check([executor, str(wrap_path), '30'], 0)
        assert 'a=$0055' in wrap_output and 'last_addr=$0001' in wrap_output, wrap_output
        # Indexed accesses to unmodeled hardware are rejected.
        idx_oob_rom = bytearray(rom)
        idx_oob_program = bytes([0xA2, 0x01, 0xBD, 0xFF, 0x20, 0xDB])
        idx_oob_rom[:len(idx_oob_program)] = idx_oob_program
        idx_oob_path = root / 'execute-indexed-oob.sfc'
        idx_oob_path.write_bytes(idx_oob_rom)
        idx_oob_output = check([executor, str(idx_oob_path), '20'], 0)
        assert 'reason=unsupported' in idx_oob_output, idx_oob_output
        # BCC/BCS must use carry without altering it; exercise taken and
        # fallthrough paths with opposite carry states.
        for branch_op, carry_op, expected in (
            (0x90, 0x18, 0x22),  # CLC; BCC taken
            (0x90, 0x38, 0x11),  # SEC; BCC not taken
            (0xB0, 0x38, 0x22),  # SEC; BCS taken
            (0xB0, 0x18, 0x11),  # CLC; BCS not taken
        ):
            carry_rom = bytearray(rom)
            carry_program = bytes([
                carry_op, branch_op, 0x04,
                0xA9, 0x11, 0x80, 0x02,
                0xA9, 0x22, 0xDB,
            ])
            carry_rom[:len(carry_program)] = carry_program
            carry_path = root / f'execute-carry-branch-{branch_op:02x}-{carry_op:02x}.sfc'
            carry_path.write_bytes(carry_rom)
            carry_output = check([executor, str(carry_path), '20'], 0)
            assert 'reason=stp' in carry_output, carry_output
            assert f'a=${expected:04X}' in carry_output, carry_output
            assert f'c={1 if carry_op == 0x38 else 0}' in carry_output, carry_output
        # JMP absolute skips instructions and retains the current ROM bank.
        jmp_rom = bytearray(rom)
        jmp_program = bytes([
            0x4C, 0x07, 0x80, 0xA9, 0x11, 0xDB, 0xEA,
            0xA9, 0x42, 0xDB,
        ])
        jmp_rom[:len(jmp_program)] = jmp_program
        jmp_path = root / 'execute-jmp-absolute.sfc'
        jmp_path.write_bytes(jmp_rom)
        jmp_output = check([executor, str(jmp_path), '10'], 0)
        assert 'reason=stp' in jmp_output and 'a=$0042' in jmp_output, jmp_output
        # JMP to $0000-$7FFF is not executable LoROM space.
        bad_jmp_rom = bytearray(rom)
        bad_jmp_program = bytes([0x4C, 0x00, 0x20, 0xDB])
        bad_jmp_rom[:len(bad_jmp_program)] = bad_jmp_program
        bad_jmp_path = root / 'execute-jmp-nonrom.sfc'
        bad_jmp_path.write_bytes(bad_jmp_rom)
        bad_jmp_output = check([executor, str(bad_jmp_path), '10'], 0)
        assert 'reason=boundary' in bad_jmp_output, bad_jmp_output
        # Self-jumps are bounded by the caller-supplied instruction limit.
        loop_rom = bytearray(rom)
        loop_program = bytes([0x4C, 0x00, 0x80])
        loop_rom[:len(loop_program)] = loop_program
        loop_path = root / 'execute-jmp-loop.sfc'
        loop_path.write_bytes(loop_rom)
        loop_output = check([executor, str(loop_path), '7'], 0)
        assert 'reason=limit steps=7' in loop_output, loop_output
        # BRL uses a signed 16-bit displacement from the next instruction.
        brl_rom = bytearray(rom)
        brl_program = bytes([
            0x82, 0x04, 0x00, 0xA9, 0x11, 0xDB, 0xEA,
            0xA9, 0x42, 0xDB,
        ])
        brl_rom[:len(brl_program)] = brl_program
        brl_path = root / 'execute-brl-forward.sfc'
        brl_path.write_bytes(brl_rom)
        brl_output = check([executor, str(brl_path), '10'], 0)
        assert 'reason=stp' in brl_output and 'a=$0042' in brl_output, brl_output
        # BRL -3 loops to itself and must honor the execution limit.
        back_rom = bytearray(rom)
        back_program = bytes([0x82, 0xFD, 0xFF])
        back_rom[:len(back_program)] = back_program
        back_path = root / 'execute-brl-back.sfc'
        back_path.write_bytes(back_rom)
        back_output = check([executor, str(back_path), '6'], 0)
        assert 'reason=limit steps=6' in back_output, back_output
        # BRL into the non-ROM lower half of the bank is rejected.
        bad_brl_rom = bytearray(rom)
        bad_brl_program = bytes([0x82, 0xFD, 0xFF, 0xDB])
        bad_brl_rom[:len(bad_brl_program)] = bad_brl_program
        # Put the branch at $8001 so a -4 offset lands at $8000;
        # a -5 offset lands at $7FFF and must be rejected.
        bad_brl_rom[:6] = bytes([0xEA, 0x82, 0xFB, 0xFF, 0xDB, 0xEA])
        bad_brl_path = root / 'execute-brl-boundary.sfc'
        bad_brl_path.write_bytes(bad_brl_rom)
        bad_brl_output = check([executor, str(bad_brl_path), '10'], 0)
        assert 'reason=boundary' in bad_brl_output, bad_brl_output
        # JSR pushes a return address and RTS resumes after the call.
        jsr_rom = bytearray(rom)
        jsr_rom[:6] = bytes([0x20, 0x10, 0x80, 0xA9, 0x42, 0xDB])
        jsr_rom[0x10:0x13] = bytes([0xA9, 0x11, 0x60])
        jsr_path = root / 'execute-jsr-rts.sfc'
        jsr_path.write_bytes(jsr_rom)
        jsr_output = check([executor, str(jsr_path), '20'], 0)
        assert 'reason=stp' in jsr_output and 'a=$0042' in jsr_output, jsr_output
        assert 'sp=$01FF depth=0' in jsr_output, jsr_output
        # Nested calls must restore the correct return address twice.
        nested_rom = bytearray(rom)
        nested_rom[:6] = bytes([0x20, 0x10, 0x80, 0xA9, 0x77, 0xDB])
        nested_rom[0x10:0x16] = bytes([0x20, 0x20, 0x80, 0xA9, 0x55, 0x60])
        nested_rom[0x20:0x23] = bytes([0xA9, 0x33, 0x60])
        nested_path = root / 'execute-nested-jsr.sfc'
        nested_path.write_bytes(nested_rom)
        nested_output = check([executor, str(nested_path), '30'], 0)
        assert 'reason=stp' in nested_output and 'a=$0077' in nested_output, nested_output
        assert 'sp=$01FF depth=0' in nested_output, nested_output
        # Bare RTS is rejected rather than interpreting uninitialized stack.
        bare_rom = bytearray(rom)
        bare_rom[0] = 0x60
        bare_path = root / 'execute-bare-rts.sfc'
        bare_path.write_bytes(bare_rom)
        bare_output = check([executor, str(bare_path), '10'], 0)
        assert 'reason=stack' in bare_output, bare_output
        # JSR into the non-ROM portion of the bank is rejected.
        bad_call_rom = bytearray(rom)
        bad_call_rom[:3] = bytes([0x20, 0x00, 0x20])
        bad_call_path = root / 'execute-jsr-nonrom.sfc'
        bad_call_path.write_bytes(bad_call_rom)
        bad_call_output = check([executor, str(bad_call_path), '10'], 0)
        assert 'reason=boundary' in bad_call_output, bad_call_output
        # PHA/PLA round-trip in emulation mode restores A and updates N/Z.
        pha8_rom = bytearray(rom)
        pha8_program = bytes([0xA9, 0x80, 0x48, 0xA9, 0x00, 0x68, 0xDB])
        pha8_rom[:len(pha8_program)] = pha8_program
        pha8_path = root / 'execute-pha-pla8.sfc'
        pha8_path.write_bytes(pha8_rom)
        pha8_output = check([executor, str(pha8_path), '20'], 0)
        assert 'reason=stp' in pha8_output and 'a=$0080' in pha8_output, pha8_output
        assert 'sp=$01FF' in pha8_output and 'n=1' in pha8_output, pha8_output
        # Native 16-bit PHA/PLA must push high then low and restore both.
        pha16_rom = bytearray(rom)
        pha16_program = bytes([
            0x18, 0xFB, 0xC2, 0x20, 0xA9, 0x34, 0x12, 0x48,
            0xA9, 0x00, 0x00, 0x68, 0xDB,
        ])
        pha16_rom[:len(pha16_program)] = pha16_program
        pha16_path = root / 'execute-pha-pla16.sfc'
        pha16_path.write_bytes(pha16_rom)
        pha16_output = check([executor, str(pha16_path), '20'], 0)
        assert 'reason=stp' in pha16_output and 'a=$1234' in pha16_output, pha16_output
        assert 'sp=$01FF' in pha16_output and 'm=16' in pha16_output, pha16_output
        # PHA must not alter flags and PLA must update Z.
        flags_rom = bytearray(rom)
        flags_program = bytes([0x38, 0xA9, 0x00, 0x48, 0xA9, 0x01, 0x68, 0xDB])
        flags_rom[:len(flags_program)] = flags_program
        flags_path = root / 'execute-pha-flags.sfc'
        flags_path.write_bytes(flags_rom)
        flags_output = check([executor, str(flags_path), '20'], 0)
        assert 'a=$0000' in flags_output and 'z=1' in flags_output, flags_output
        assert 'c=1' in flags_output, flags_output
        # A pop from an empty synthetic stack is rejected.
        empty_pla_rom = bytearray(rom)
        empty_pla_rom[0] = 0x68
        empty_pla_path = root / 'execute-empty-pla.sfc'
        empty_pla_path.write_bytes(empty_pla_rom)
        empty_pla_output = check([executor, str(empty_pla_path), '10'], 0)
        assert 'reason=stack' in empty_pla_output, empty_pla_output
        # PHX/PLX round-trip in 8-bit index mode restores X.
        phx8_rom = bytearray(rom)
        phx8_program = bytes([0xA2, 0x80, 0xDA, 0xA2, 0x00, 0xFA, 0xDB])
        phx8_rom[:len(phx8_program)] = phx8_program
        phx8_path = root / 'execute-phx-plx8.sfc'
        phx8_path.write_bytes(phx8_rom)
        phx8_output = check([executor, str(phx8_path), '20'], 0)
        assert 'reason=stp' in phx8_output and 'x=$0080' in phx8_output, phx8_output
        assert 'sp=$01FF' in phx8_output and 'n=1' in phx8_output, phx8_output
        # Native 16-bit PHX/PLX must preserve both bytes of X.
        phx16_rom = bytearray(rom)
        phx16_program = bytes([
            0x18, 0xFB, 0xC2, 0x10, 0xA2, 0x34, 0x12, 0xDA,
            0xA2, 0x00, 0x00, 0xFA, 0xDB,
        ])
        phx16_rom[:len(phx16_program)] = phx16_program
        phx16_path = root / 'execute-phx-plx16.sfc'
        phx16_path.write_bytes(phx16_rom)
        phx16_output = check([executor, str(phx16_path), '20'], 0)
        assert 'reason=stp' in phx16_output and 'x=$1234' in phx16_output, phx16_output
        assert 'sp=$01FF' in phx16_output and 'xbits=16' in phx16_output, phx16_output
        # PHX preserves carry; PLX sets zero when restoring zero.
        phx_flags_rom = bytearray(rom)
        phx_flags_program = bytes([0x38, 0xA2, 0x00, 0xDA, 0xA2, 0x01, 0xFA, 0xDB])
        phx_flags_rom[:len(phx_flags_program)] = phx_flags_program
        phx_flags_path = root / 'execute-phx-flags.sfc'
        phx_flags_path.write_bytes(phx_flags_rom)
        phx_flags_output = check([executor, str(phx_flags_path), '20'], 0)
        assert 'x=$0000' in phx_flags_output and 'z=1' in phx_flags_output, phx_flags_output
        assert 'c=1' in phx_flags_output, phx_flags_output
        # Empty PLX must stop without reading an uninitialized stack.
        empty_plx_rom = bytearray(rom)
        empty_plx_rom[0] = 0xFA
        empty_plx_path = root / 'execute-empty-plx.sfc'
        empty_plx_path.write_bytes(empty_plx_rom)
        empty_plx_output = check([executor, str(empty_plx_path), '10'], 0)
        assert 'reason=stack' in empty_plx_output, empty_plx_output
        # PHY/PLY round-trip in 8-bit index mode restores Y.
        phy8_rom = bytearray(rom)
        phy8_program = bytes([0xA0, 0x80, 0x5A, 0xA0, 0x00, 0x7A, 0xDB])
        phy8_rom[:len(phy8_program)] = phy8_program
        phy8_path = root / 'execute-phy-ply8.sfc'
        phy8_path.write_bytes(phy8_rom)
        phy8_output = check([executor, str(phy8_path), '20'], 0)
        assert 'reason=stp' in phy8_output and 'y=$0080' in phy8_output, phy8_output
        assert 'sp=$01FF' in phy8_output and 'n=1' in phy8_output, phy8_output
        # Native 16-bit PHY/PLY preserves both bytes of Y.
        phy16_rom = bytearray(rom)
        phy16_program = bytes([
            0x18, 0xFB, 0xC2, 0x10, 0xA0, 0x34, 0x12, 0x5A,
            0xA0, 0x00, 0x00, 0x7A, 0xDB,
        ])
        phy16_rom[:len(phy16_program)] = phy16_program
        phy16_path = root / 'execute-phy-ply16.sfc'
        phy16_path.write_bytes(phy16_rom)
        phy16_output = check([executor, str(phy16_path), '20'], 0)
        assert 'reason=stp' in phy16_output and 'y=$1234' in phy16_output, phy16_output
        assert 'sp=$01FF' in phy16_output and 'xbits=16' in phy16_output, phy16_output
        # PHY preserves carry; PLY sets Z after restoring zero.
        phy_flags_rom = bytearray(rom)
        phy_flags_program = bytes([0x38, 0xA0, 0x00, 0x5A, 0xA0, 0x01, 0x7A, 0xDB])
        phy_flags_rom[:len(phy_flags_program)] = phy_flags_program
        phy_flags_path = root / 'execute-phy-flags.sfc'
        phy_flags_path.write_bytes(phy_flags_rom)
        phy_flags_output = check([executor, str(phy_flags_path), '20'], 0)
        assert 'y=$0000' in phy_flags_output and 'z=1' in phy_flags_output, phy_flags_output
        assert 'c=1' in phy_flags_output, phy_flags_output
        # Empty PLY must stop instead of reading uninitialized stack.
        empty_ply_rom = bytearray(rom)
        empty_ply_rom[0] = 0x7A
        empty_ply_path = root / 'execute-empty-ply.sfc'
        empty_ply_path.write_bytes(empty_ply_rom)
        empty_ply_output = check([executor, str(empty_ply_path), '10'], 0)
        assert 'reason=stack' in empty_ply_output, empty_ply_output
        # PHP/PLP restores carry and zero without altering the accumulator.
        php_rom = bytearray(rom)
        php_program = bytes([0x38, 0xA9, 0x00, 0x08, 0x18, 0xA9, 0x01, 0x28, 0xDB])
        php_rom[:len(php_program)] = php_program
        php_path = root / 'execute-php-plp.sfc'
        php_path.write_bytes(php_rom)
        php_output = check([executor, str(php_path), '20'], 0)
        assert 'reason=stp' in php_output and 'c=1' in php_output and 'z=1' in php_output, php_output
        assert 'sp=$01FF' in php_output and 'a=$0001' in php_output, php_output
        # Native PLP restores M and X widths and truncates X/Y when X becomes 8-bit.
        native_php_rom = bytearray(rom)
        native_php_program = bytes([
            0x18, 0xFB, 0x08, 0xC2, 0x30, 0xA2, 0x34, 0x12,
            0x28, 0xDB,
        ])
        native_php_rom[:len(native_php_program)] = native_php_program
        native_php_path = root / 'execute-php-native.sfc'
        native_php_path.write_bytes(native_php_rom)
        native_php_output = check([executor, str(native_php_path), '20'], 0)
        assert 'reason=stp' in native_php_output and 'm=8' in native_php_output, native_php_output
        assert 'xbits=8' in native_php_output and 'x=$0034' in native_php_output, native_php_output
        assert 'sp=$01FF' in native_php_output, native_php_output
        # An empty PLP must stop rather than read a fabricated status byte.
        empty_plp_rom = bytearray(rom)
        empty_plp_rom[0] = 0x28
        empty_plp_path = root / 'execute-empty-plp.sfc'
        empty_plp_path.write_bytes(empty_plp_rom)
        empty_plp_output = check([executor, str(empty_plp_path), '10'], 0)
        assert 'reason=stack' in empty_plp_output, empty_plp_output
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
