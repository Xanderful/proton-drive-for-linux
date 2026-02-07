# Contributing to Proton Drive Linux

Thank you for your interest in contributing to Proton Drive Linux! This guide explains how to set up your development environment, code standards, and the process for submitting contributions.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Development Setup](#development-setup)
3. [Code Standards](#code-standards)
4. [Making Changes](#making-changes)
5. [Testing](#testing)
6. [Submitting PRs](#submitting-prs)
7. [Architecture Guide](#architecture-guide)

---

## Getting Started

### Prerequisites

- **Linux** development machine (Ubuntu 22.04+ recommended)
- **Git** for version control
- **C++17 or later** compiler (GCC 9+, Clang 10+)
- **CMake 3.16+** for building
- **GTK4** development libraries
- Basic familiarity with Linux development tools

### Fork & Clone

1. Fork the repository on GitHub: https://github.com/Xanderful/proton-drive-linux
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/proton-drive-linux.git
   cd proton-drive-linux
   ```
3. Add upstream remote for sync:
   ```bash
   git remote add upstream https://github.com/Xanderful/proton-drive-linux.git
   ```

---

## Development Setup

### Install Dependencies

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  libgtk-4-dev \
  libcurl4-openssl-dev \
  libsqlite3-dev \
  libssl-dev \
  pkg-config \
  git
```

#### Fedora/RHEL
```bash
sudo dnf install -y \
  gcc-c++ \
  cmake \
  gtk4-devel \
  libcurl-devel \
  sqlite-devel \
  openssl-devel \
  pkgconfig \
  git
```

#### Arch
```bash
sudo pacman -S \
  base-devel \
  cmake \
  gtk4 \
  curl \
  sqlite \
  openssl
```

### Build for Development

```bash
# Clone repo
git clone https://github.com/YOUR_USERNAME/proton-drive-linux.git
cd proton-drive-linux

# Build
mkdir -p src-native/build && cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run with debug logging
./proton-drive --debug
```

Or use the Makefile shortcut:
```bash
cd ~/path/to/proton-drive-linux
make dev    # Builds + runs with --debug
```

### Project Structure

```
src-native/
├── CMakeLists.txt           # Build configuration
└── src/
    ├── main_native.cpp      # Entry point
    ├── app_window.{cpp,hpp}  # Main UI window
    ├── sync_manager.{cpp,hpp}# Sync orchestration
    ├── file_index.{cpp,hpp}  # Cloud file cache
    ├── file_watcher.{cpp,hpp}# inotify monitoring
    ├── cloud_browser.{cpp,hpp}# Cloud explorer UI
    ├── settings.{cpp,hpp}    # Preferences
    ├── notifications.{cpp,hpp}# Desktop notifications
    ├── logger.{cpp,hpp}      # Logging system
    └── ...                   # Other components

docs/
├── QUICKSTART.md            # User quickstart guide
├── ARCHITECTURE-INTERNALS.md# Component deep-dive
├── TROUBLESHOOTING.md       # Issue diagnosis
├── SYNC_SETUP.md            # rclone configuration
├── SYNC_SAFETY_FEATURES.md  # Safety mechanisms
└── ...

scripts/
├── setup-rclone.sh          # rclone installation
├── test-sync-comprehensive.sh# Integration tests
└── ...
```

---

## Code Standards

### C++ Style Guide

**General Principles:**
- Follow Google C++ Style Guide (with modifications)
- Modern C++17 features encouraged (smart pointers, auto, range-based for loops)
- No C-style casts; use `static_cast`, `reinterpret_cast`, etc.
- RAII for resource management (no manual delete)

### Naming Conventions

```cpp
// Classes: PascalCase
class FileWatcher { };
class SyncManager { };

// Functions/Methods: snake_case
void watch_path(const std::string& path);
void trigger_sync(const std::string& job_id);

// Member variables: snake_case with trailing underscore
class MyClass {
  std::string name_;
  int count_;
};

// Constants: UPPER_SNAKE_CASE
const int MAX_RETRIES = 3;
const char* SYNC_CACHE_DIR = ".cache/proton-drive";

// Enums: PascalCase for enum name, UPPER_SNAKE_CASE for values
enum class JobStatus {
  RUNNING,
  PAUSED,
  ERROR,
  IDLE
};
```

### Code Examples

**Good: Smart pointers, clear error handling**
```cpp
std::unique_ptr<FileWatcher> watcher = std::make_unique<FileWatcher>();
if (!watcher->initialize(watch_path)) {
  Logger::error("Failed to initialize watcher: " + watch_path);
  return false;
}
```

**Good: STL, range-based loops**
```cpp
std::vector<std::string> files = list_dir(path);
for (const auto& file : files) {
  process_file(file);
}

// Use auto for verbose types
std::map<std::string, std::unique_ptr<SyncJob>>& jobs = sync_manager_->jobs();
```

**Good: Modern C++ features**
```cpp
// Structured bindings (C++17)
auto [path, size] = get_file_info();

// Optional (C++17)
std::optional<Token> token = keyring_.get_token();
if (token) { /* use *token */ }

// String formatting (if available)
Logger::info(fmt::format("Synced {} files in {:.2f}s", count, duration));
```

**Bad: C-style, manual memory management**
```cpp
// ❌ Old style
char* buffer = (char*)malloc(1024);
strcpy(buffer, filename);
delete buffer;  // Wrong! Should be free()

// ✅ Modern C++
std::string buffer = filename;  // RAII handles cleanup
auto buffer_ptr = std::make_unique<std::string>(filename);
```

### Header Comments

Include file purpose and key classes:

```cpp
// file_watcher.hpp
// Real-time file system monitoring via inotify
//
// Watches local folders for file creation/modification/deletion,
// triggers sync operations via SyncManager.
//
// Key Classes:
//   - FileWatcher: Main monitoring coordinator
//   - FileWatcherImpl: Platform-specific inotify implementation

#ifndef PROTON_DRIVE_FILE_WATCHER_HPP
#define PROTON_DRIVE_FILE_WATCHER_HPP

// ...
```

### Function Documentation

Use Doxygen-style comments for public APIs:

```cpp
/**
 * Watches a filesystem path for changes.
 *
 * @param path The directory path to watch (must exist)
 * @param callback Function called when files change
 * @return true if watch was established, false on error
 *
 * @note Watches recursively; includes subdirectories
 * @note Callback may be invoked from worker thread
 */
bool watch_path(const std::string& path, 
                FileChangeCallback callback);
```

---

## Making Changes

### Create a Feature Branch

```bash
# Update main branch first
git fetch upstream
git checkout main
git reset --hard upstream/main

# Create feature branch
git checkout -b feature/my-feature-name
```

### Commit Messages

Use clear, descriptive commit messages:

```
Format: <type>(<scope>): <subject>

Types:
  feat      - New feature
  fix       - Bug fix
  refactor  - Code restructuring (no behavior change)
  perf      - Performance improvement
  test      - Tests/test infrastructure
  docs      - Documentation
  chore     - Build, deps, CI/CD

Examples:
  feat(sync): add pause/resume sync job functionality
  fix(file_watcher): prevent duplicate sync triggers on rapid edits
  refactor(file_index): improve FTS5 query performance
  perf(cloud_browser): cache folder listings to reduce API calls
  docs(quickstart): add 5-minute setup guide
```

### Code Organization

**One responsibility per class:**
```cpp
// ✅ Good: Clear responsibility
class FileWatcher { /* monitors filesystem */ };
class FileIndexUpdater { /* updates search cache */ };

// ❌ Bad: Too much in one class
class SyncEngine {
  void watch_files();
  void update_index();
  void resolve_conflicts();
  void handle_ui_updates();
  // Too much!
};
```

**Keep headers clean:**
```cpp
// In header: declare public API
class SyncManager {
public:
  bool create_job(const JobConfig& config);
  void start_job(const std::string& job_id);
  // ...
private:
  // Implementation details hidden
};

// In .cpp: all implementation
bool SyncManager::create_job(const JobConfig& config) {
  // Large implementation here
}
```

---

## Testing

### Unit Testing

Currently, manual integration testing is preferred. For new features:

1. **Create a test scenario** documented in comments
2. **Test manually** via the GUI or CLI
3. **Document test steps** in PR description

Example test scenario:
```cpp
/**
 * Test scenario: Conflict resolution
 *
 * Steps:
 * 1. Create sync job: ~/Documents <-> proton:/Documents
 * 2. Locally: echo "v1" > report.txt
 * 3. Cloud: Upload report.txt with content "v2" via web app
 * 4. Run sync within 15 minutes
 * 5. Verify: Both versions preserved (v1_conflict_*, v2)
 */
```

### Integration Testing

```bash
# Run comprehensive sync tests
scripts/test-sync-comprehensive.sh

# Run safety feature tests
scripts/test-sync-safety.sh

# Manual smoke test
make dev
# In app:
# 1. Login
# 2. Create sync job
# 3. Add files locally and in cloud
# 4. Verify sync works both directions
```

### Performance Testing

For sync/file index changes, test with large datasets:

```bash
# Create test folder with 5000+ files
mkdir -p ~/test-sync-perf
for i in {1..5000}; do
  echo "test content $i" > ~/test-sync-perf/file-$i.txt
done

# Sync and measure time
time rclone bisync ~/test-sync-perf proton:/test-perf

# Monitor memory/CPU
top -p $(pgrep proton-drive)
```

---

## Submitting PRs

### Before You Submit

1. **Ensure your code compiles and runs cleanly:**
   ```bash
   cd src-native/build
   make clean && make -j$(nproc)
   ./proton-drive --debug
   # Test manually
   ```

2. **Check for common issues:**
   ```bash
   # View your changes
   git diff

   # Look for:
   # - Trailing whitespace
   # - Inconsistent formatting
   # - Debug printing left in code
   # - Hard-coded paths
   ```

3. **Sync with upstream:**
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

4. **Push to your fork:**
   ```bash
   git push origin feature/my-feature-name
   ```

### PR Description Template

```markdown
## Description
Brief explanation of what this PR does.

## Type of Change
- [ ] New feature (non-breaking)
- [ ] Bug fix
- [ ] Performance improvement
- [ ] Documentation
- [ ] Refactoring

## Testing
How was this tested?
- [ ] Manual testing (describe steps)
- [ ] Existing tests still pass
- [ ] New test added

## Files Changed
List of modified files and brief purpose.

## Screenshots (if UI changes)
Include screenshots of new feature.

## Checklist
- [ ] Code compiles without warnings
- [ ] No hard-coded paths or credentials
- [ ] Commit messages are clear
- [ ] No unrelated changes included
```

### What to Expect

1. **Code review:** Maintainers review for correctness, style, and design
2. **Feedback:** May request changes; keep commits organized
3. **Merge:** Once approved, PR is merged to main
4. **Release:** Your feature appears in next release

---

## Architecture Guide

### Key Design Patterns

**Observer Pattern:** File changes trigger sync
```cpp
FileWatcher (observable)
    ↓ notifies
   ↓
SyncManager (observer)
    → Executes rclone bisync
```

**Singleton Pattern:** Logger, Settings
```cpp
Logger::info("message");  // Global logger instance
Settings::get("key");     // Global settings store
```

**Factory Pattern:** Creating sync jobs
```cpp
SyncJob job = SyncManager::create_job(config);
```

### Threading Model

- **Main Thread:** GTK event loop (UI only)
- **Background Threads:** Sync, file watching, API calls
- **Lock-free:** Use `g_idle_add()` to post UI updates from workers
- **No mutex hell:** Prefer message passing where possible

### Adding a New Feature

**Example: Add support for selective folder sync**

1. **Design Phase:**
   - Update `SyncJob` struct with `include_paths`/`exclude_paths`
   - Design UI: checkboxes in sync config dialog
   - Plan data persistence: store in job metadata JSON

2. **Implementation:**
   - `sync_manager.cpp`: Update `create_job()` to accept filters
   - `app_window_sync.cpp`: Add filter checkboxes to UI
   - Update rclone command: `--exclude`, `--include` flags
   - `file_index.cpp`: Mark excluded files as unsynced

3. **Testing:**
   - Create sync job with filters
   - Verify excluded files don't sync
   - Verify included files do sync
   - Test enabling/disabling filters mid-sync

4. **Documentation:**
   - Update QUICKSTART.md with filter example
   - Document in SYNC_SETUP.md
   - Update ARCHITECTURE-INTERNALS.md data flow

5. **Submit PR:**
   - Clear commit message: `feat(sync): add selective folder filtering`
   - Include test steps in PR description

---

## Common Issues & Solutions

### Issue: Code doesn't compile

**Check:**
```bash
# Verify all dependencies installed
pkg-config --cflags --libs gtk4 libcurl sqlite3 openssl

# If missing, install (Ubuntu):
sudo apt install libgtk-4-dev libcurl4-openssl-dev

# Clean rebuild
cd src-native/build
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Issue: Crash on startup

**Debug:**
```bash
# Run with debug symbols and core dump
ulimit -c unlimited
proton-drive --debug

# Watch logs
tail -f ~/.cache/proton-drive/proton-drive.log

# Check for crash dump
cat ~/.cache/proton-drive/crash.log
```

### Issue: Merge conflicts

**Resolve:**
```bash
# Pull latest upstream
git fetch upstream
git rebase upstream/main

# Fix conflicts in editor
# Then:
git add conflicted_file.cpp
git rebase --continue
git push -f origin feature/my-feature
```

---

## Questions?

- **Architecture questions:** See [ARCHITECTURE-INTERNALS.md](ARCHITECTURE-INTERNALS.md)
- **Sync behavior:** See [SYNC_SAFETY_FEATURES.md](SYNC_SAFETY_FEATURES.md) and [SYNC_SETUP.md](../SYNC_SETUP.md)
- **Issues/bugs:** Open GitHub issue with reproducible steps
- **Discussions:** GitHub Discussions tab (if enabled)

