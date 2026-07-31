# PS1 hardware debug frontend

The hardware frontend is opt-in. Start it with:

```sh
./build/psx-emulator --debug-ui
```

or:

```sh
make debug
```

This opens two SDL3/OpenGL windows:

- **PSX** displays emulated drawing output.
- **PSX Hardware Debugger** displays live emulator state and controls.

The debug frontend uses the project-local Nuklear v4.13.3 dependency. It runs
in a separate OpenGL context so refreshing the UI does not alter the PS1
display's framebuffer or swap timing. The PS1 context is restored before the
CPU resumes.

## Panels

### Execution

- **Run** resumes normal execution.
- **Pause** stops before the next instruction.
- **Step** executes exactly one instruction.
- The current PC, stop reason, and stop address are displayed.

These buttons use the same `Debugger` object as GDB. A stop requested from
either frontend is immediately visible to the other.

### CPU Registers

Displays the 32 MIPS general-purpose registers plus HI, LO, SR, Cause, EPC,
and BadVAddr. General-purpose registers that changed since the previous UI
frame are highlighted.

### DMA Channels

Displays DPCR, DICR, and all seven DMA channels. Each channel shows its base
address, control value, block size, block count, and active state.

### GPU / GP0

Displays GPUSTAT, current GP0 command, buffered command words, remaining image
words, DMA direction, drawing offset, and drawing area.

### VRAM

The panel is present but intentionally reports that VRAM is unavailable. The
current GPU implementation translates drawing commands directly to OpenGL and
does not yet own the PS1's 1024x512 array of 16-bit pixels. Once VRAM storage
and image transfers are implemented, this panel can upload that array as a
debug texture without changing the UI architecture.

## Relationship with GDB

The hardware frontend complements rather than replaces GDB:

- Use GDB for disassembly, symbols, breakpoints, watchpoints, expressions, and
  memory/register modification.
- Use the hardware frontend for live CPU, DMA, GPU, GP0, and future VRAM state.

Both interfaces remain usable at the same time.
