# Original input-controlled diagnostic

The separate Windows Input demo maps held Z to serial pad B (bit 0) and X to Y
(bit 1). On-screen push-like checkboxes toggle the same buttons. The effective mask
is the OR of keyboard and toggled inputs. Deactivation releases only held keyboard
bits; Reset clears both. Keyboard handling is scoped to this window and its child
controls, with no global keyboard polling. Repeat key-downs do not rerun the CPU.
Closing/reopening creates a fresh session. Physical controllers are not bound.

`input_scene_rom()` preserves the legacy scene builders. It starts with the 158
instructions of the validated high-priority BG1/BG2/OBJ setup, then pulses $4016,
reads two serial bits into WRAM $0100/$0101, and executes compiled LDA/BEQ/BRA/STA
paths. B selects INIDISP $80 (blank), otherwise Y selects $07 (dim), otherwise $0F.
The host never writes the brightness register for this demo. Existing CPU/port/video
operations are used; no ISA extension, opcode interpreter or ROM-dependent fallback.

The generated entry is named `run_control_generated`, distinct from the default
scene entry so both programs link into the same native executable. The optional
linear-resume profile remains unchanged and rejects this branching input program.
Every effective mask change runs the input program from RESET to STP on a fresh bus,
uploading original assets by DMA and producing a diagnostic snapshot. This is not
continuous SNES execution, frame synchronization, character control, audio or game
execution. Masks use the existing twelve-bit raw joypad contract; only B/Y affect
this program. Failure or invalid mask preserves the previously published frame.

Portable tests execute all budgets, two initial flags and three master-clock origins
for all four B/Y combinations, compare CPU/memory/ports/time/raster and independent
pixels, and schedule NMI around the longest path's boundaries. Snapshot tests also
verify unused bits, repeated masks, independent sessions and reset. Five validated
captures are saved. Windows automated smoke tests exercise keys, combined inputs,
repeat suppression, child focus routing, deactivation, mouse toggles, reset, reuse,
close/reopen and GDI color/orientation readback. This is automated verification;
interactive visual acceptance must be recorded separately.
