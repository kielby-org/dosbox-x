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
 telnet, etc.)              polls in GFX_Events() (always running)
                            routes commands to ParseCommand()
                            captures DEBUG_ShowMsg output
                            returns text responses
```

**No threading required.** TCP polling happens in `GFX_Events()` which runs every frame
regardless of debugger state. When debugger is not active, commands get an error response.
When active, commands route through `ParseCommand()` and output is captured.

### TCP Module: `src/debug/debug_tcp.cpp`

| Function | Purpose |
|----------|---------|
| `DEBUG_TCP_Init()` | Read config, create TCP listener. Called from `DEBUG_Init()`. |
| `DEBUG_TCP_Shutdown()` | Close sockets. Called from `DEBUG_ShutDown()`. |
| `DEBUG_TCP_Poll()` | Accept connections, read bytes, process commands. Called from `GFX_Events()`. |
| `DEBUG_TCP_CaptureMsg()` | Buffer a DEBUG_ShowMsg line during command processing. |
| `DEBUG_TCP_IsCapturing()` | True while a TCP command is being processed. |

### Response Capture Design

`ParseCommand()` produces output via `DEBUG_ShowMsg()`, which normally writes to ncurses.
During TCP command processing:
1. `capturing_response` flag is set
2. `DEBUG_ShowMsg()` checks `DEBUG_TCP_IsCapturing()` early — if true, formats the
   message, appends to capture buffer, writes to log file, then **returns before
   ncurses** (avoids `wrefresh()` blocking when debugger window is in a bad state)
3. After `ParseCommand()` returns, captured output + `---END---\n` is sent to client

**Known limitation:** `ParseCommand` can call ncurses functions directly (e.g.
`DrawCode()`, `DrawRegisters()`) which are NOT intercepted. These may block if the
debugger window isn't in a good state. Commands that only produce `DEBUG_ShowMsg`
output work reliably; commands that redraw the UI may not.

### Safety

- `PING` command bypasses debugger state (connection testing)
- No authentication — bind to localhost only in production
- Guard: `#if C_DEBUG && C_MODEM` (requires both debugger and SDL_net)

## Key Source Files

### Debugger Core: `src/debug/debug.cpp` (~6060 lines)

| Symbol | Line | Purpose |
|--------|------|---------|
| `ParseCommand(char* str)` | ~1906 | Command dispatcher — 105+ commands, sequential if/else |
| `DEBUG_Loop()` | ~4753 | Main debugger loop — polls input, updates display |
| `DEBUG_CheckKeys()` | ~4310 | Input polling — `getch()` at line 4314 |
| `DEBUG_Init()` | ~5663 | Initialization — calls `DEBUG_TCP_Init()` |
| `DEBUG_ShutDown()` | ~5634 | Cleanup — calls `DEBUG_TCP_Shutdown()` |
| `IsDebuggerActive()` | ~345 | Returns true when debugger is paused/active |
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

TCP debug port is registered in the `[log]` section (debug_gui.cpp) as `tcp_debug_port`.
Default is 0 (disabled).

Command-line override: `-set "log tcp_debug_port=12345"` (note: space, not colon)

Other relevant config: `debuggerrun` in `[log]` — controls debugger start mode
(debugger/normal/watch).

## TCP Protocol (Plain Text, Request-Response)

- Client sends: `COMMAND args\n`
- Server responds: `response lines\n---END---\n`
- No push notifications — client polls with `STATUS`
- Testable with Python socket or telnet

### Built-in TCP Commands (handled in debug_tcp.cpp, before ParseCommand)
| Command | Requires Debugger | Description |
|---------|-------------------|-------------|
| `PING` | No | Connection test — returns `PONG` |
| `STATUS` | No | Debugger state: `debugger=active\|inactive`, `mode=paused\|running\|off`, `cs=`, `ip=`, `cycles=` |
| `REGS` | Yes | Register dump: EAX-ESP, EIP, segment regs, flags |
| `SENDKEY <key> [DOWN\|UP]` | No | Inject keyboard event. Default: press+release. Keys: `a`-`z`, `0`-`9`, `f1`-`f12`, `enter`, `space`, `esc`, `leftshift`, `leftctrl`, `leftalt`, arrows, etc. |
| `SENDCLICK <btn>` | No | Mouse click (press+release). `0`/`left`, `1`/`right`, `2`/`middle` |
| `SENDMOUSE <xrel> <yrel>` | No | Relative mouse movement in pixels |

### All Other Commands
All other commands pass through to `ParseCommand()` — the full DOSBox-X debugger
command set (105+ commands). Key ones:
- `HELP` — command list
- `BP seg:off` — set breakpoint
- `BPINT intNr` — interrupt breakpoint
- `BPLIST` — list breakpoints
- `BPDEL nr` — delete breakpoint
- `RUN` — resume execution
- `CPU` / `FPU` — CPU/FPU info (via DEBUG_ShowMsg, captured)
- `SR reg val` — set register
- `C seg:off` / `D seg:off` — set code/data view
- `MEMDUMP seg ofs num` — dump memory

## Ground Rules

1. **Minimal patch surface.** One new file (`debug_tcp.cpp` + header), small hooks in
   existing code. Keeps upstream rebases easy.
2. **Cross-platform.** Must compile on Windows, Linux, macOS. Use existing socket
   abstraction (`NETServerSocket`/`NETClientSocket`).
3. **Don't break existing debugger.** ncurses UI must continue to work. TCP is additive.
4. **Don't break existing builds.** All CI workflows must stay green.
5. **Follow existing patterns.** Config registration, socket usage, file organization
   should match what DOSBox-X already does.

## Working Process

### Git & Commits
- **Never amend commits.** Always create new, separate commits for follow-up changes.
- **Review before commit.** Show the user a summary of changes (files modified, key diffs)
  and wait for approval before running `git commit`. Do not commit autonomously.
- **Update CLAUDE.md status** when completing a task — move items from Next to Completed.

### Code Style
- **Match local OS line endings** in new files. Git checks out files with the local OS
  convention (CRLF on Windows, LF on Linux/macOS). The Write tool may create files with
  LF regardless of OS. After creating files, verify with `file <path>` and convert if
  needed (`unix2dos` on Windows, `dos2unix` on Linux/macOS).

### Task Workflow
1. Plan the task (use plan mode for non-trivial work)
2. Implement the changes
3. Build locally (`/build`)
4. Show changes to user for review
5. User approves → commit and push
6. Trigger CI if appropriate (`/ci run`)

### Skills Available
- `/build` — local VS build (debug/release, x64/x86, SDL1/SDL2, clean/rebuild)
- `/ci` — GitHub Actions CI (trigger runs, check status, enable/disable workflows)
- `/test` — launch, test, and manage DOSBox-X (smoke test, unit tests, process lifecycle)

## Local Build

```bash
# Incremental debug x64 build (default)
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug" "x64" "dosbox-x"
```

Output binary: `bin/x64/Debug/dosbox-x.exe`

## Runtime Smoke Test (TCP Debug Interface)

After building, verify the TCP debug interface works end-to-end:

Use `/test smoke` to run the automated smoke test, or test manually:

**Always use** `-defaultconf` (ignores user config) and `-console` (shows log window).

### Without debugger active
```bash
bin/x64/Debug/dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345"
```
- `PING` → `PONG\n---END---\n`
- `HELP` → `ERROR: Debugger not active...\n---END---\n`
- `QUIT` → DOSBox-X exits

### With debugger active
```bash
# On Windows, use `start` so debugger gets its own console window:
start "DOSBox" bin\x64\Debug\dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345" -break-start
```
- `PING` → `PONG\n---END---\n`
- `HELP` → full debugger help text + `---END---\n`
- `QUIT` → DOSBox-X exits
- Unknown command → `ERROR: Unknown command\n---END---\n`

### Known limitations
- `ParseCommand` can call ncurses directly (DrawCode, DrawRegisters) — these calls
  aren't intercepted and may block if the debugger window is in a bad state
- `SendArray` doesn't handle partial sends — very large responses could truncate
- No authentication — anyone who can connect to the port can send commands

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
- [x] Implement TCP listener — config `log tcp_debug_port`, single-client, response capture, PING, debugger-state-aware responses
- [x] Implement command routing — STATUS, REGS as built-in TCP commands; all other debugger commands pass through ParseCommand
- [x] Implement input injection — SENDKEY, SENDCLICK, SENDMOUSE; work regardless of debugger state
- [ ] Build MCP server (Python, `mcp-server/` directory)
