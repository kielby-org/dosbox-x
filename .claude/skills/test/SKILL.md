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

**Step 1: Kill any existing instance**
```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
```

**Step 2: Launch without debugger and test "not active" response**
```bash
bin/x64/Debug/dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345" &
sleep 6
python -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3)
s.connect(('127.0.0.1', 12345))
print('PASS: Connected to TCP debug port')
s.sendall(b'HELP\n')
data = b''
while True:
    chunk = s.recv(4096)
    if not chunk: break
    data += chunk
    if b'---END---' in data: break
response = data.decode()
if 'Debugger not active' in response:
    print('PASS: Got expected debugger-not-active response')
else:
    print('UNEXPECTED:', repr(response))
s.close()
"
```
Expected: `PASS: Connected` and `PASS: Got expected debugger-not-active response`

**Step 3: Kill and relaunch with debugger active (interactive only)**

This step requires an interactive terminal (the debugger console needs a real TTY).
Skip in headless environments.

```bash
taskkill //F //IM dosbox-x.exe 2>/dev/null; sleep 1
bin/x64/Debug/dosbox-x.exe -defaultconf -console -set "log tcp_debug_port=12345" -break-start
```
Then from another terminal:
```bash
python -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
s.connect(('127.0.0.1', 12345))
s.sendall(b'HELP\n')
data = b''
while True:
    chunk = s.recv(4096)
    if not chunk: break
    data += chunk
    if b'---END---' in data: break
response = data.decode()
if 'Debugger not active' not in response and '---END---' in response:
    print('PASS: Got command response from active debugger')
    print(response[:500])
else:
    print('FAIL:', repr(response))
s.close()
"
```
Expected: `PASS: Got command response from active debugger` followed by help text.

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
