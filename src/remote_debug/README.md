# PinMAME Remote Debugger

A high-performance, thread-safe remote debugging extension for PinMAME with a web-based dashboard and REST API.

## Features
- **Headless Operation**: Optimized for server/CI environments with `-headless` mode.
- **Thread Safety**: All machine access is synchronized via recursive mutexes.
- **REST API**: Full control over emulation, memory, and CPU state via HTTP.
- **High-Fidelity Rendering**:
  - Full DMD support (128x32 raw frame capture).
  - Hardware-accurate Williams 16-Segment Alphanumeric Starburst rendering (verified against VPinball).
- **Matrix Visualizer**: Live updates for Lamps, Switches, Solenoids, and Dedicated inputs.
- **Interactive Inputs**: Pulse cabinet buttons and toggle matrix switches directly from the UI.
- **Advanced Debugging**:
  - **Bank-Aware Disassembler**: View code in any ROM bank (e.g., WPC banks 0x00-0x3F).
  - **Callstack Tracking**: Real-time tracking of subroutine calls and interrupts, including full register context (A, B, X, Y, U, S, DP, CC) and ROM banking information.
  - **Banked Breakpoints**: Trigger halts on specific `bank:addr` combinations.
  - Hardware Breakpoints & Watchpoints (Read/Write/Access).
  - Step Over & "Run To" execution modes.
  - Memory Hex Editor with Pattern Search and Block Fill.
  - NVRAM Management (Dump/Clear).

## Setup & Build
1. Define `REMOTE_DEBUG=1` in your build environment or at the end of `makefile.unix`.
2. Run PinMAME with the game name: `./xpinmamed.x11 <romname> -headless`.
3. Access the dashboard at `http://localhost:8926/ui`.

## API Reference

### System Info & Captures
- `GET /api/info`: Returns JSON with game status, lamps, switches, segments, and solenoids.
- `GET /ui`: Serves the Web Debugger Dashboard (HTML).

#### DMD Endpoints
- `GET /api/dmd/info`: JSON with current DMD `width` and `height`.
- `GET /api/dmd/raw`: Raw binary DMD frame buffer (1 byte per pixel, 0-255 luminance).
- `GET /api/dmd/pnm`: Grayscale image of the DMD in PPM (P5) format.

#### Screenshot Endpoints
- `GET /api/screenshot/info`: JSON with current screen `width` and `height`.
- `GET /api/screenshot/raw`: Raw RGB24 binary buffer (3 bytes per pixel).
- `GET /api/screenshot/pnm`: Full game window screenshot in PPM (P6) format.

### Debugger Control
- `GET /api/debugger/control?cmd=[pause|resume|step|exit|stepover]`: Execution control.
- `GET /api/debugger/control/runto?addr=[HEX]`: Run until PC reaches address.
- `GET /api/debugger/state`: JSON with registers and flags for all active CPUs.
- `GET /api/debugger/dasm?addr=[HEX]&lines=[INT]&cpu=[ID]&bank=[HEX]`: JSON disassembly. If `bank` is provided, the disassembler temporarily maps that ROM bank.
- `GET /api/debugger/messages`: JSON list of debugger console messages.
- `GET /api/debugger/callstack`: Returns a list of callstack objects, each containing `caller`, `receiver`, `bank`, and full register context (`pc`, `u`, `s`, `x`, `y`, `a`, `b`, `dp`, `cc`).

### Points (Breakpoints & Watchpoints)
- `GET /api/debugger/points`: JSON list of all active BPs and WPs.
- `GET /api/debugger/points?cmd=[toggle|delete]&type=[bp|wp]&idx=[INT]`: Modify existing points.
- `GET /api/debugger/breakpoints?cmd=add&addr=[HEX]&bank=[HEX]`: Add a new (optionally banked) instruction breakpoint.
- `GET /api/debugger/breakpoints?cmd=clear`: Remove all breakpoints.
- `GET /api/debugger/watchpoints?cmd=add&addr=[HEX]&mode=[1:R|2:W|3:RW]`: Add a memory watchpoint.
- `GET /api/debugger/watchpoints?cmd=clear`: Remove all watchpoints.

### Memory & NVRAM
- `GET /api/debugger/memory?addr=[HEX]&size=[INT]&cpu=[ID]`: Read memory block as JSON array.
- `GET /api/debugger/memory/write?addr=[HEX]&val=[HEX]&cpu=[ID]`: Write a single byte.
- `GET /api/debugger/memory/fill?addr=[HEX]&size=[INT]&val=[HEX]&cpu=[ID]`: Fill memory range.
- `GET /api/debugger/memory/find?addr=[HEX]&size=[HEX]&pattern=[HEX_STRING]&cpu=[ID]`: Search for hex pattern.
- `GET /api/debugger/nvram/dump`: Raw binary dump of the WPC NVRAM (8KB).
- `GET /api/debugger/nvram?cmd=clear`: Wipe CMOS/NVRAM and reset machine.

### Input
- `GET /api/input?sw=[INT]&val=[0|1]`: Toggle or pulse a switch (Cabinet or Matrix).
- `GET /api/debugger/command?cmd=[STRING]`: Execute classic MAME-style debugger commands (URL encoded). Supports `BP [bank]:[addr]` syntax.
- `GET /api/doc`: Simple text-based API quick-reference.

## Misc

### Williams 16-Segment Mapping
The Alphanumeric renderer uses the definitive hardware mapping (bits are 0-indexed):
- **Bits 0-5**: Outer segments (a, b, c, d, e, f)
- **Bit 6**: Middle-Left horizontal (g1)
- **Bit 11**: Middle-Right horizontal (g2)
- **Bit 9**: Center-Top vertical (h)
- **Bit 13**: Center-Bottom vertical (i)
- **Bit 8**: Diag Top-Left (j)
- **Bit 10**: Diag Top-Right (k)
- **Bit 14**: Diag Bottom-Left (l)
- **Bit 12**: Diag Bottom-Right (m)
- **Bit 15**: Period (DP)
- **Bit 7**: Comma