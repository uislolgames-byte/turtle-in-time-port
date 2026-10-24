# Bounded manual serial joypads

This original diagnostic model is enabled only with the bus MMIO opt-in. Host API
`set_joypad_buttons(port, mask)` accepts ports 0/1 and raw twelve-bit pressed masks
in B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R order, least-significant bit first.
Out-of-range inputs fail before mutation. Opposite directions are raw diagnostic
bits; no mechanical or host input conflict policy is implemented.

CPU byte writes to $4016 broadcast bit 0 as strobe to both ports. Other write bits
are ignored. Only transitions reset serial positions, and falling strobe captures
held buttons. High strobe reads the live B bit without advancing. Low strobe reads
12 captured buttons, four zero signature bits, then permanent 1, with independent
positions per port capped at 16. A repeated low write does not recapture. $4017
writes are ignored. Input changes while low do not alter the captured sample.

Only standard pads on the first data line are modeled. $4016 reads have D0 as the
serial bit and other bits zero. $4017 has D0 plus fixed D2..D4=1 (0x1c); other bits
are zero. These diagnostic constants do not implement general CPU open bus.
Mirrors follow the existing low system-bank mapping. WRAM banks are unaffected.
Const reads, word accesses (including a word starting at $4015), stack/block moves
and DMA A-source I/O do not consume controller bits; unsupported accesses fail.
Timed byte accesses use the existing 12-master-clock $4000..$41ff classification,
with normal code fetch costs. Capacity/event polling precedes port effects.

Device/CPU reset clears strobe, captured serial data and positions while preserving
externally held buttons, RAM and other video memories. The host can release buttons
with mask zero; ordinary RAM/video clears do not change controller state. There is
no auto-poll ($4200 bit 0 remains rejected), $4218..$421f result registers, busy flag,
multitap, mouse, light gun, serial second line or native keyboard/controller binding.

An independent queue-based reference checks all 4096 masks paired with complements,
strobes, changes mid-read, tails, mirrors, reset and access rejection. Two generated
original fixtures use LDA absolute/STA to exercise both ports, repeated strobe,
ignored writes, signature/tail and relatch. Full WRAM, CPU flags/state, port state,
nominal/master clocks and NMI at instruction boundaries are compared independently.

Primary semantic references, used for an original implementation:
- [ares standard gamepad serial order, strobe and signature](https://github.com/ares-emulator/ares/blob/master/ares/sfc/controller/gamepad/gamepad.cpp)
- [ares CPU JOYSER I/O](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/io.cpp)
