---
name: ci
description: Manage GitHub Actions CI — trigger workflow runs, check build status, view logs, enable/disable workflows. Use when the user says "run CI", "check builds", "build status", "enable workflow", "disable workflow", "CI logs", "trigger build", or wants to interact with GitHub Actions.
argument-hint: "[status | run | logs <run-id> | enable <workflow> | disable <workflow> | list]"
---

# GitHub Actions CI Manager

Manages CI workflows on the fork via the `gh` CLI.

Arguments: $ARGUMENTS

**gh binary location:** `/c/Program Files/GitHub CLI/gh.exe`
Use this path directly since it may not be in the bash PATH. Alias it at the start of any bash session:
```bash
GH="/c/Program Files/GitHub CLI/gh.exe"
```

**Repository:** Determine from `git remote -v` (origin). The fork is expected to be on GitHub.

---

## Mode Detection

- **No argument / `status`** — Show status of recent workflow runs
- **`run` / `run <workflow>`** — Trigger a workflow run on the current branch
- **`logs <run-id>`** — Show logs for a specific run
- **`enable <workflow>` / `enable all`** — Enable workflow(s)
- **`disable <workflow>` / `disable all`** — Disable workflow(s)
- **`list`** — List all workflows and their enabled/disabled state
- **`cancel <run-id>`** — Cancel a running workflow

---

## Commands Reference

### List all workflows and their state
```bash
"$GH" workflow list --all
```

### Check status of recent runs
```bash
# All recent runs
"$GH" run list --limit 10

# Runs for current branch only
"$GH" run list --branch "$(git branch --show-current)" --limit 10

# Runs for a specific workflow
"$GH" run list --workflow <workflow-file> --limit 5
```

### Trigger a workflow run
```bash
# Trigger specific workflow on current branch
"$GH" workflow run <workflow-file> --ref "$(git branch --show-current)"

# Example: trigger just the linux build
"$GH" workflow run linux.yml --ref feature/tcp-debug
```

### View run details and logs
```bash
# View run summary
"$GH" run view <run-id>

# View failed job logs
"$GH" run view <run-id> --log-failed

# Watch a run in progress
"$GH" run watch <run-id>
```

### Enable/disable workflows
```bash
# Enable a specific workflow
"$GH" workflow enable <workflow-file>

# Disable a specific workflow
"$GH" workflow disable <workflow-file>
```

### Cancel a run
```bash
"$GH" run cancel <run-id>
```

---

## Recommended Workflow Configuration for This Fork

These workflows should be **enabled** (cost-effective, validates our changes):
- `linux.yml` — cheapest, fast feedback, has unit tests
- `vsbuild64.yml` — primary Windows build, validates VS project changes
- `vsbuild32.yml` — secondary Windows, validates Win32

These should be **disabled** (expensive, not needed during development):
- `macos.yml` — 10x billing rate
- `hxdos.yml` — niche target, hardcoded upstream download
- `mingw32.yml` — 3 redundant variants
- `mingw64.yml` — redundant with VS coverage
- `vsbuild_xp.yml` — XP compat irrelevant
- `windows-installers.yml` — heaviest workflow, builds installer

### Quick setup (enable recommended, disable rest):
```bash
GH="/c/Program Files/GitHub CLI/gh.exe"
for wf in linux.yml vsbuild64.yml vsbuild32.yml; do "$GH" workflow enable "$wf"; done
for wf in macos.yml hxdos.yml mingw32.yml mingw64.yml vsbuild_xp.yml windows-installers.yml; do "$GH" workflow disable "$wf"; done
```

---

## Status Display Format

When showing status, present it as a concise table:

```
Branch: feature/tcp-debug
Recent runs (last 10):

| Run | Workflow | Status | Duration | Triggered |
|-----|----------|--------|----------|-----------|
| 123 | linux    | pass   | 4m       | 2h ago    |
| 122 | vsbuild64| fail   | 12m      | 2h ago    |
```

If any runs failed, automatically show the failed job names and offer to show logs.

---

## Important Notes

- Always confirm with the user before enabling workflows (they cost CI minutes)
- When triggering runs, default to current branch unless told otherwise
- The fork uses only `github.token` (no custom secrets needed)
- Release publishing is tag-gated — safe to run all workflows without accidentally publishing
