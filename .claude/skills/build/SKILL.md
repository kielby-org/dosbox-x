---
name: build
description: Build DOSBox-X locally using Visual Studio MSBuild. Use when the user says "build", "compile", "rebuild", "clean build", "incremental build", "build debug", "build release", "check if it compiles", or wants to build the project locally.
argument-hint: "[debug | release | clean | rebuild | sdl2 | x64 | x86]"
---

# Local Build Manager

Builds DOSBox-X locally using MSBuild (Visual Studio).

Arguments: $ARGUMENTS

---

## Build Script

A helper script exists at `.claude/skills/build/build.cmd` that handles VS environment setup, toolset detection, and MSBuild invocation.

**Usage from bash:**
```bash
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "<Configuration>" "<Platform>" "<Target>"
```

Defaults: `Debug` `x64` `dosbox-x`

---

## Mode Detection

Parse the arguments to determine configuration and action:

| Argument | Meaning |
|----------|---------|
| (none) | Incremental debug x64 build (default) |
| `debug` | Debug configuration |
| `release` | Release configuration |
| `sdl2` | Use SDL2 variant (e.g., "Debug SDL2") |
| `x64` (default) | 64-bit platform |
| `x86` / `win32` | 32-bit platform |
| `clean` | Clean before building |
| `rebuild` | Full rebuild (clean + build) |

Arguments are combinable: `/build debug sdl2 x64` = "Debug SDL2" on x64.

---

## Configuration Mapping

| User Args | build.cmd Configuration | build.cmd Platform |
|-----------|------------------------|-------------------|
| (default) | Debug | x64 |
| debug | Debug | x64 |
| release | Release | x64 |
| debug sdl2 | Debug SDL2 | x64 |
| release sdl2 | Release SDL2 | x64 |
| debug x86 | Debug | Win32 |
| release x86 | Release | Win32 |

---

## Build Commands

### Incremental Build (default)
```bash
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug" "x64" "dosbox-x"
```

### Clean
```bash
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug" "x64" "dosbox-x:Clean"
```

### Rebuild (Clean + Build)
```bash
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug" "x64" "dosbox-x:Rebuild"
```

### SDL2 Build
```bash
cmd.exe //c "C:\\Projects\\dosbox-x\\.claude\\skills\\build\\build.cmd" "Debug SDL2" "x64" "dosbox-x"
```

---

## Output Handling

- Run the build command and capture output (pipe through `tail` to limit noise)
- If build succeeds (exit code 0): report success and warning count
- If build fails: show the error output, parse the first few errors, and suggest fixes
- Output binary locations:
  - x64 Debug: `vs/bin/x64/Debug/dosbox-x.exe`
  - x64 Release: `vs/bin/x64/Release/dosbox-x.exe`
  - Win32 Debug: `vs/bin/Win32/Debug/dosbox-x.exe`

---

## Build Timeouts

- Incremental build: 5 minutes (usually seconds if few files changed)
- Clean/rebuild: 15 minutes (full build takes several minutes)
- Use `run_in_background` for rebuilds and check later

---

## Error Patterns

| Error Pattern | Likely Cause | Fix |
|---------------|-------------|-----|
| `cannot open include file` | Missing header / wrong include path | Check include paths in vcxproj |
| `unresolved external symbol` / `LNK2019` | Missing source file in project or missing lib | Add to vcxproj ClCompile or link lib |
| `LNK1120` | N unresolved externals | Usually follows LNK2019s |
| `C1083` | Cannot open source file | File missing or wrong path in vcxproj |
| `MSB8020` toolset not found | Wrong PlatformToolset | build.cmd auto-detects; check VS install |

---

## Environment Details

- **VS Version:** 18 Community (preview/insider)
- **Toolset:** v145 (auto-detected by build.cmd)
- **Solution:** `vs/dosbox-x.sln`
- **MSBuild:** `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`
- The solution has many sub-projects (SDL, zlib, freetype, etc.) — targeting just `dosbox-x` avoids rebuilding deps unless needed
- For a full solution build (all deps): use target `Build` instead of `dosbox-x`
- Warnings from fluidsynth (C4244, C4305) are pre-existing and expected
