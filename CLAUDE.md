# DOSBox-X TCP Debug Interface

## Purpose

This fork adds a TCP-based debug/control interface to DOSBox-X so that external tools
(including LLM agents via MCP) can programmatically control the built-in debugger.

The existing ncurses debugger continues to work alongside TCP — this is purely additive.

## Why

DOSBox-X's built-in debugger is excellent for DOS reverse engineering: segment-aware
addressing, breakpoints, memory/register inspection, interrupt tracing. But it has no
external control interface. We need programmatic access for automated debugging and testing.

Alternative approaches (GDB stubs, Bochs) were evaluated and rejected:
- dosbox-x-gdb requires embedding a stub in the target program (can't debug unmodified binaries)
- Bochs/QEMU require switching emulators and setting up FreeDOS
- DOSBox-X's debugger already does everything we need — it just needs an external interface

## Architecture

```
External Client  <--TCP-->  DOSBox-X (this fork)
(MCP server,                debug_tcp module
 telnet, etc.)              polls alongside ncurses input
                            routes commands to ParseCommand()
                            returns text responses
```

**No threading required.** The debugger already uses non-blocking input polling (`getch()`
with `nodelay()`). TCP recv() is added to the same polling loop.

## Key Source Files

### Debugger Core: `src/debug/debug.cpp` (~6060 lines)

| Symbol | Line | Purpose |
|--------|------|---------|
| `ParseCommand(char* str)` | ~1906 | Command dispatcher — 105+ commands, sequential if/else |
| `DEBUG_Loop()` | ~4753 | Main debugger loop — polls input, updates display |
| `DEBUG_CheckKeys()` | ~4310 | Input polling — `getch()` at line 4314. **TCP poll goes here.** |
| `DEBUG_Init()` | ~5663 | Initialization. **TCP listener init goes after line 5670.** |
| `DEBUG_ShutDown()` | ~5634 | Cleanup. **TCP socket cleanup goes here.** |
| `DEBUG_ShowMsg()` | various | Output to ncurses log window (228 call sites) |
| `DrawRegisters()` | ~1135 | Formats register values to ncurses window |
| `DrawData()` | ~994 | Memory display |
| `DrawCode()` | ~835 | Disassembly display |
| `DEBUG_Run()` | ~4296 | Execute N instructions |
| `CBreakpoint::*` | ~460+ | Breakpoint management (add, delete, check) |
| `GetAddress()` | ~442 | Resolves seg:off to physical address |
| `debug_running` | ~332 | Flag: true = program executing, false = paused in debugger |
| `debugging` | ~331 | Flag: debugger active |

### Debugger GUI: `src/debug/debug_gui.cpp` (~1044 lines)

| Symbol | Line | Purpose |
|--------|------|---------|
| `DBGUI_StartUp()` | ~590 | ncurses init (initscr, nodelay, keypad) |
| `MakeSubWindows()` | ~453 | Creates register/code/data/output windows |
| `LOG::SetupConfigSection()` | ~1000 | Registers `[log]` config section (includes `debuggerrun`) |
| `debuggerrun` | ~56 | Global: 0=debugger, 1=normal, 2=watch mode |

### Socket Infrastructure: `src/hardware/serialport/misc_util.h/.cpp`

| Class | Purpose |
|-------|---------|
| `NETServerSocket` | Abstract server socket — `NETServerFactory(type, port)` |
| `NETClientSocket` | Abstract client socket — `GetcharNonBlock()`, `SendArray()`, `ReceiveArray()` |
| `TCPServerSocket` | TCP server implementation (wraps SDL_net) |
| `TCPClientSocket` | TCP client implementation |

**Usage pattern:**
```cpp
auto* server = NETServerSocket::NETServerFactory(SOCKET_TYPE_TCP, port);
if (!server->isopen) { /* failed */ }
auto* client = server->Accept();  // returns nullptr if no pending connection
if (client) {
    uint8_t byte;
    SocketState state = client->GetcharNonBlock(byte);  // Good, Empty, or Closed
    client->SendArray(data, len);
}
```

### Build System

| File | Where to add debug_tcp.cpp |
|------|---------------------------|
| `src/debug/Makefile.am` | Add to `libdebug_a_SOURCES` list |
| `vs/dosbox-x.vcxproj` | Add `<ClCompile>` entry near line 1154 (where debug.cpp is) |

### Existing CI Workflows (`.github/workflows/`)

- `vsbuild64.yml` — VS Win64 builds (builds SDL1+SDL2, runs `-tests` smoke test)
- `vsbuild32.yml` — VS Win32 builds
- `linux.yml` — Linux builds
- `macos.yml` — macOS builds
- `mingw32.yml`, `mingw64.yml` — MinGW builds

All trigger on push, PR, and workflow_dispatch. We don't create custom build scripts —
these existing workflows validate our changes across all platforms.

## Config

No `[debug]` config section exists yet. The `debuggerrun` option lives in `[log]`
(debug_gui.cpp:1016). Our TCP port setting needs to be registered somewhere — either:
- Add to existing `[log]` section (simpler, follows existing pattern)
- Create new `[debug]` section (cleaner separation)

Command-line override: `-set log:tcp_debug_port=12345` (or `debug:` if new section)

## TCP Protocol (Plain Text, Request-Response)

- Client sends: `COMMAND args\n`
- Server responds: `response lines\n---END---\n`
- No push notifications — client polls with `STATUS`
- Testable with `telnet localhost 12345`

## Ground Rules

1. **Minimal patch surface.** One new file (`debug_tcp.cpp` + header), small hooks in
   existing code. Keeps upstream rebases easy.
2. **Cross-platform.** Must compile on Windows, Linux, macOS. Use existing socket
   abstraction (`NETServerSocket`/`NETClientSocket`).
3. **Don't break existing debugger.** ncurses UI must continue to work. TCP is additive.
4. **Don't break existing builds.** All CI workflows must stay green.
5. **Follow existing patterns.** Config registration, socket usage, file organization
   should match what DOSBox-X already does.

## Local Build

```bash
# Incremental debug x64 build (default)
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug" "x64" "dosbox-x"
```

Output binary: `bin/x64/Debug/dosbox-x.exe`

## Runtime Smoke Test (TCP Debug Interface)

After building, verify the TCP debug interface works end-to-end:

### 1. Start DOSBox-X with TCP debug enabled
```bash
# From repo root — port 12345, start in debugger mode
bin/x64/Debug/dosbox-x.exe -set log:tcp_debug_port=12345 -set log:debuggerrun=debugger
```
DOSBox-X will open with the debugger window. The TCP listener should be active on port 12345.

### 2. Connect from another terminal
```bash
# Using netcat (or telnet)
nc localhost 12345
```

### 3. Send test commands
```
HELP
```
Expected: debugger help text followed by `---END---`

```
STATUS
```
Expected: register dump or status info followed by `---END---`

### 4. Verify
- Commands produce text responses terminated by `---END---\n`
- The ncurses debugger still works (keyboard input alongside TCP)
- Disconnecting the TCP client doesn't crash DOSBox-X
- Reconnecting works after disconnect

### 5. Shutdown
Close DOSBox-X normally (type `QUIT` in debugger or close the window).

## Status

### Completed
- [x] Fork created at kielby-org/dosbox-x
- [x] Source code analyzed — hook points mapped with line numbers
- [x] Architecture designed — no threading needed, polls alongside getch()
- [x] Protocol defined — plain text request-response
- [x] Command set defined (see plan)
- [x] Create `feature/tcp-debug` branch
- [x] Trigger CI on fork to verify baseline builds pass (linux, vsbuild64, vsbuild32 enabled; others disabled to save minutes)
- [x] Add stub `debug_tcp.cpp` + header (compiles, no functionality)
- [x] Add to Makefile.am, VS project, and VS filters
- [x] Verify CI green with stub
- [x] Implement TCP listener (accept connection on configured port) — config `log:tcp_debug_port`, single-client, response capture via DEBUG_ShowMsg hook
- [ ] Implement command routing (STATUS, REGS, BP, STEP, RUN)
- [ ] Implement input injection (SENDKEY, SENDMOUSE, SENDCLICK)
- [ ] Build MCP server (Python, `mcp-server/` directory)
