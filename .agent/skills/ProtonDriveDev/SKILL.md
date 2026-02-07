---
description: A C/C++ coding expert specialized for Proton Drive Linux (GTK4 + CMake), focusing on strict safety, correctness, and repo-specific workflows.
---

# Proton Drive C/C++ GTK4 Developer Skill

## Purpose
This skill provides expertise in the Proton Drive Linux repository, specifically the native GTK4 desktop client. It guides you to implement changes safely, correctly, and maintainably.

## Repo Context
- **Nature**: Native GTK4 desktop client (`src-native/src/`).
- **Build System**: CMake (via `Makefile` wrappers).
- **Logging**: `Logger::info/debug/error`. Debug logs require `--debug` flag.
- **Sync**: Integrated rclone + systemd services for real-time folder synchronization.

## Key Capabilities & Patterns
- **GTK4/GObject**: Correct reference counting (`g_object_ref`/`unref`), signal handling, and avoiding main-thread blocking.
- **Memory Safety**: RAII, smart pointers, and valid null checks.
- **Security**: Treat all external input (file paths, IPC, rclone output) as untrusted. Avoid command injection.

## Critical Philosophy
**NEVER suppress warnings or errors.**
1. **Find Root Cause**: Understand *why* the warning exists.
2. **Fix It**: Adjust logic, improve types, or handle the edge case.
3. **Verify**: Ensure the fix works and doesn't introduce regressions.

## Workflow Rules
1. **Understand First**: Use `grep_search` and `list_code_usages` to map the impacted area.
2. **Minimal Changes**: Keep patches small and focused. Match existing code style (spaces, not tabs).
3. **Verify Locally**:
   - Build: `make build-native`
   - Test: `make dev` (runs the app)
   - Check Errors: Ensure compiler warnings are **resolved**, not hidden.

## Common commands
- `make build-native`: Build the C++ native client.
- `make dev`: Build and run the client in dev mode.
- `./src-native/build/proton-drive --debug`: Run manually with debug logging.
- `cat ~/.cache/proton-drive/proton-drive.log`: View logs.

## Directory Structure
- `src-native/src/`: Core C++ source code.
- `patches/common/`: Patches for the WebClients submodule.
- `scripts/`: Helper scripts (e.g., `build-local-appimage.sh`).

Reference the original `ProtonDriveCppGtk4.chatmode.md` in `.github/chatmodes/` for more detailed examples if needed.
