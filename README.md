# PSX C Emulator

A work-in-progress PlayStation 1 emulator written in C23. The project focuses
on making the console's low-level behavior explicit: MIPS instruction
execution, delayed branches and loads, memory-mapped devices, DMA transfers,
GPU command processing, and debugging facilities are implemented as separate
components with narrow interfaces.

This is an educational emulator under active development. It can execute the
PS1 BIOS and render an initial subset of GPU commands, but it is not yet
capable of running complete games.

## Current features

- MIPS R3000A instruction decoding and execution
- Branch delay slots and delayed register loads
- Exceptions, COP0 state, EPC, Cause, SR, and BadVAddr
- Byte, halfword, word, and unaligned memory operations
- PS1 virtual-address region masking and memory-mapped interconnect
- 2 MiB RAM and 512 KiB BIOS mapping
- DMA block, ordering-table clear, and GPU linked-list transfers
- Initial GP0/GP1 command processing
- Monochrome and shaded polygon rendering through SDL3 and OpenGL
- Batched CPU-to-GPU vertex uploads
- Breakpoints, read/write watchpoints, pausing, and single-stepping
- Local GDB Remote Serial Protocol server
- Optional hardware-debug frontend for CPU, DMA, GPU, and GP0 state

## Requirements

- CMake 3.24 or newer
- A C23-compatible compiler
- Git and an internet connection for the first configuration
- OpenGL
- A legally obtained 512 KiB PS1 BIOS image
- A GDB build with MIPS support for remote debugging

SDL3 and Nuklear are pinned and built inside the local `build` directory.
Neither dependency needs to be installed globally.

## BIOS setup

The emulator currently expects the BIOS at:

```text
roms/SCPH1001.bin
```

Create the directory and copy your own BIOS dump there:

```sh
mkdir -p roms
cp /path/to/your/SCPH1001.bin roms/SCPH1001.bin
```

BIOS files are copyrighted and should not be committed to the repository.

## Build and run

Build the emulator:

```sh
make
```

Run with only the PS1 display:

```sh
make run
```

Run with the hardware-debug frontend:

```sh
make debug
```

The equivalent direct commands are:

```sh
cmake -S . -B build
cmake --build build --parallel
./build/psx-emulator
./build/psx-emulator --debug-ui
```

View command-line options with:

```sh
./build/psx-emulator --help
```

## Debugging with GDB

The emulator listens on `localhost:9001` whether or not the graphical
hardware frontend is enabled.

Start the emulator in one terminal, then connect from another:

```sh
gdb
```

```gdb
set endian little
target remote localhost:9001
```

The server exposes the MIPS architecture and registers, memory inspection and
RAM modification, breakpoints, watchpoints, single-stepping, and continuing.
For example:

```gdb
info registers
x/8i $pc
stepi
break *0x80010000
watch *(unsigned int *)0x80010000
continue
```

Enable protocol logging or select another port with:

```sh
PSX_GDB_LOG=1 make run
PSX_GDB_PORT=9002 make run
```

See [DEBUGGING.md](docs/DEBUGGING.md) for protocol details and limitations.

## Hardware-debug frontend

`make debug` opens a second window containing:

- Run, pause, and step controls
- Live CPU and COP0 registers
- Changed-register highlighting
- DMA controller and channel state
- GPU status and GP0 command state
- A placeholder for the future VRAM viewer

The frontend and GDB share the same debugger core and can be used
simultaneously. The frontend uses its own OpenGL context so its refreshes do
not alter the emulated display's framebuffer or swap path.

See [DEBUG_UI.md](docs/DEBUG_UI.md) for a description of each panel.

## Project structure

```text
src/
├── common/
│   └── types.h        Fixed-width integer and memory-access types
├── cpu/
│   ├── cpu.*          CPU state, execution, and exceptions
│   └── instruction.*  Instruction field decoding
├── memory/
│   ├── interconnect.* Address masking and device routing
│   ├── ram.*          Main RAM storage
│   └── bios.*         BIOS loading and reads
├── hardware/
│   ├── dma.*          DMA controller and channels
│   └── gpu.*          GPU state and command processing
├── debug/
│   ├── debugger.*     Breakpoint, watchpoint, and stepping core
│   └── gdb_server.*   GDB Remote Serial Protocol frontend
├── frontend/
│   ├── renderer.*     SDL3 and PS1 OpenGL rendering
│   └── debug_ui.*     Nuklear hardware frontend
└── main.c             Initialization and main execution loop
```

## Major limitations

- No emulated VRAM framebuffer yet
- Texture sampling and complete image-transfer behavior are unfinished
- CD-ROM, SPU, timers, and interrupt behavior are placeholders or incomplete
- GPU and CPU timing are not cycle accurate
- GTE/COP2 functionality is incomplete
- No controller, memory-card, or disc-image support
- GDB device writes are intentionally blocked
- Raw BIOS code has no function or source symbols

## Near-term roadmap

1. Add emulated VRAM and GPU image transfers.
2. Display VRAM in the hardware-debug frontend.
3. Improve GPU drawing and texture support.
4. Implement interrupts and root counters.
5. Replace temporary CD-ROM and SPU behavior.
6. Add controller, memory-card, and executable/disc loading.

## License

No project license has been selected yet. Third-party dependencies retain
their respective licenses.
