# Debugging the emulated PS1 with GDB

The emulator exposes its emulated MIPS R3000A CPU through the GDB Remote
Serial Protocol on `localhost:9001`.

Start the emulator:

```sh
make run
```

In a second terminal, connect a GDB build that supports MIPS:

```gdb
target remote localhost:9001
```

The server supplies its MIPS architecture and register layout through a target
description, so `set architecture` is not normally required. The standard
target description does not separately declare byte order. If GDB has not
remembered the setting from an earlier session, select the PS1's byte order
before connecting:

```gdb
set endian little
target remote localhost:9001
```

Useful commands:

```gdb
info registers
x/8i $pc
stepi
continue
break *0x80010000
watch *(unsigned int *)0x80010000
set $t0 = 0x12345678
set {unsigned int}0x80010000 = 0x89abcdef
```

Register writes update both of the CPU's register banks. Writing the PC also
resets pending branch state and selects the following word as `next_pc`.
Memory writes are restricted to RAM: GDB cannot write the BIOS or invoke
memory-mapped device callbacks.

Debugger memory reads use a side-effect-free examination path. RAM, BIOS, DMA,
GPU status, CD-ROM placeholder state, and current placeholder registers can be
examined without triggering watchpoints or device actions. Unsupported and
write-only addresses return an error to GDB instead of terminating the
emulator.

To log Remote Serial Protocol packets, start the emulator with:

```sh
PSX_GDB_LOG=1 make run
```

Payloads longer than 160 characters are abbreviated in the log. Logging is
disabled by default because GDB sends many register and memory packets.

Port `9001` is the default. A different local port can be selected when
running multiple emulator instances:

```sh
PSX_GDB_PORT=9002 make run
```

Current limitations:

- The emulator exposes one thread representing the emulated CPU.
- GDB requires a MIPS FPU register feature; the PS1 has no general-purpose
  FPU, so those compatibility registers read as zero and writes are ignored.
- Watchpoints match the starting address of a CPU access.
- Device register writes from GDB are intentionally unsupported.
- Function names, stack frames, and source lines require a matching ELF symbol
  file; raw BIOS code appears as `??` in GDB.
