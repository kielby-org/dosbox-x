---
name: test
description: Launch, test, and manage DOSBox-X processes. Use when the user says "test", "run", "launch", "start dosbox", "smoke test", "kill dosbox", "stop dosbox", "run tests", "unit tests", or wants to run/manage the built binary.
argument-hint: "[smoke | run | launch <args> | tests | status | kill]"
---

# DOSBox-X Test & Launch Manager

Launches, monitors, and manages DOSBox-X processes for testing.

Arguments: $ARGUMENTS

---

## Binary Location

The debug build output is at:
```
bin/x64/Debug/dosbox-x.exe
```
Other configurations:
- `bin/x64/Release/dosbox-x.exe`
- `bin/Win32/Debug/dosbox-x.exe`

Always verify the binary exists before launching. If not, suggest running `/build` first.

---

## Mode Detection

| Argument | Action |
|----------|--------|
| (none) / `run` / `launch` | Normal launch (default config) |
| `smoke` | TCP debug smoke test |
| `tests` / `unit` | Run unit test suite |
| `status` | Check if DOSBox-X is running |
| `kill` / `stop` | Terminate running DOSBox-X |
| `launch <args>` | Launch with custom arguments |

---

## Default Flags

**Always** include these flags unless the user explicitly asks otherwise:

- `-defaultconf` — ignore user's host machine config, use built-in defaults. Prevents
  unrelated personal config from tainting test results.
- `-console` — show the logging console window (Windows). Essential for seeing LOG_MSG
  output, DEBUG_TCP messages, errors, etc.

These are prepended to every launch command automatically.

```bash
bin/x64/Debug/dosbox-x.exe -defaultconf -console <other-args> &
```

---

## Process Management

### Check if running
```bash
tasklist //FI "IMAGENAME eq dosbox-x.exe" 2>/dev/null
```

### Kill existing instances
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null
```

### Launch in background
```bash
bin/x64/Debug/dosbox-x.exe <args> &
```

**Important:** Always check for and kill existing instances before launching a new one to avoid port conflicts and confusion.

---

## Launch Modes

### 1. Normal Launch
Start DOSBox-X with default settings for manual testing.
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
bin/x64/Debug/dosbox-x.exe -defaultconf -console &
sleep 3
tasklist //FI "IMAGENAME eq dosbox-x.exe" 2>/dev/null
```
Report whether the process started successfully.

### 2. TCP Debug Smoke Test (`smoke`)
Launch with TCP debug enabled and run automated tests. This covers both the
"debugger not active" and "debugger active" cases.

The smoke test uses a Python helper function to send commands and check responses.
Run each step's bash block sequentially.

**Step 1: Kill any existing instance**
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
```

**Step 2: Launch without debugger and test "not active" responses**
```bash
bin/x64/Debug/dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345" &
sleep 6
python -c "
import socket
def test(s, cmd, expect):
    s.sendall(cmd.encode() + b'\n')
    data = b''
    while b'---END---' not in data: data += s.recv(4096)
    resp = data.decode()
    ok = expect in resp
    print('  %s: %s -> %s' % ('PASS' if ok else 'FAIL', cmd, expect))
    if not ok: print('    GOT:', repr(resp[:200]))
    return ok
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3)
s.connect(('127.0.0.1', 12345))
print('PASS: Connected')
test(s, 'PING', 'PONG')
test(s, 'STATUS', 'debugger=inactive')
test(s, 'HELP', 'Debugger not active')
test(s, 'REGS', 'Debugger not active')
test(s, 'SENDKEY a', 'OK')
test(s, 'SENDKEY enter', 'OK')
test(s, 'SENDKEY leftshift DOWN', 'OK')
test(s, 'SENDKEY leftshift UP', 'OK')
test(s, 'SENDKEY invalidkey', 'ERROR')
test(s, 'SENDCLICK 0', 'OK')
test(s, 'SENDCLICK left', 'OK')
test(s, 'SENDMOUSE 10 -5', 'OK')
test(s, 'SENDMOUSE', 'ERROR')
s.close()
" 2>&1 | grep -E "PASS|FAIL"
```
Expected: all PASS.

**Step 3: Kill and relaunch with debugger active**

On Windows, use the run_debug.cmd helper to launch with its own console window
(required for the ncurses debugger):
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\test\\run_debug.cmd" 2>&1
sleep 8
python -c "
import socket
def test(s, cmd, expect):
    s.sendall(cmd.encode() + b'\n')
    data = b''
    while b'---END---' not in data: data += s.recv(4096)
    resp = data.decode()
    ok = expect in resp
    print('  %s: %s -> %s' % ('PASS' if ok else 'FAIL', cmd, expect))
    if not ok: print('    GOT:', repr(resp[:200]))
    return ok
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
s.connect(('127.0.0.1', 12345))
print('PASS: Connected')
test(s, 'PING', 'PONG')
test(s, 'STATUS', 'debugger=active')
test(s, 'STATUS', 'mode=paused')
test(s, 'REGS', 'EAX=')
test(s, 'REGS', 'CS=')
test(s, 'HELP', 'Debugger commands')
test(s, 'BPLIST', '---END---')
test(s, 'SENDKEY a', 'OK')
test(s, 'SENDCLICK left', 'OK')
test(s, 'SENDMOUSE 5 5', 'OK')
test(s, 'XYZZY', 'Unknown command')
s.close()
" 2>&1 | grep -E "PASS|FAIL"
```
Expected: all PASS.

**Step 4: Cleanup**
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null
```

### 3. Unit Tests (`tests`)
Run the built-in Google Test suite and report results.
```bash
bin/x64/Debug/dosbox-x.exe -defaultconf -console -tests -set "waitonerror=false" 2>&1 | tail -30
```
Check exit code: 0 = all tests passed, non-zero = failures.

### 4. Launch with Custom Args (`launch <args>`)
Pass user-specified arguments through to DOSBox-X.
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
bin/x64/Debug/dosbox-x.exe -defaultconf -console <user-provided-args> &
sleep 3
tasklist //FI "IMAGENAME eq dosbox-x.exe" 2>/dev/null
```

Common useful arguments:
- `-set "section property=value"` — override config options
- `-break-start` — break into debugger at startup
- `-conf <file>` — use specific config file
- `-tests` — run unit tests
- `-noconsole` — no logging console window

---

## Status Reporting

After any launch, report:
- Whether the process started (PID if available)
- Any relevant log output (grep for errors, DEBUG_TCP messages, SDLNET)
- For smoke test: connection test results

After kill:
- Confirm process was terminated
- Report if no process was found

---

## Important Notes

- DOSBox-X opens GUI windows — in headless/remote environments the debugger console
  may not work properly (ncurses needs a real terminal)
- The `-tests` flag runs unit tests and exits — no GUI interaction needed
- TCP smoke test can verify listener + connection headlessly, but command response
  testing requires the debugger to be active (interactive only)
- Always use `taskkill //F` (force) — DOSBox-X may not respond to graceful termination
  when the debugger is active
