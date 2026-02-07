# Keep a Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Real-time file watcher for immediate local-to-cloud sync detection
- Enhanced cloud browser UI with thumbnail support
- Bandwidth monitoring dashboard
- Device identity tracking for multi-device sync

### Fixed
- Sync conflict resolution improvements
- Delete protection edge case handling
- Stale lock file cleanup

### Changed
- Improved logging output and debug information

## [1.2.1] - 2026-02-07

### Fixed
- AppImage script discovery now works correctly (APPDIR environment variable check)
- Removed dead --skip-webclient flag from build script
- Fixed step numbering in AppImage build process (1/5 through 5/5)
- Desktop file now uses canonical version from packaging directory
- Cleaned up unused environment variables in AppRun launcher

### Added
- Automated release creation script (create-release.sh)
- GitHub Actions workflows for automated AppImage builds on releases

### Changed
- Updated repository name to proton-drive-for-linux across all documentation

## [1.2.0] - 2026-02-XX

### Added
- GTK4 GUI dependency validation with user-friendly dialogs
- Console dependency error replacement with native GTK4 dialogs
- Comprehensive documentation covering security, sync safety, and troubleshooting
- Complete architecture internals guide for developers
- Cross-distribution testing guide and compatibility matrix

### Fixed
- Sync status reporting accuracy
- File watcher reliability on network filesystems
- Memory leak in cloud browser pagination

### Changed
- Migrated dependency error handling from console to GTK4 widgets
- Improved startup validation flow

### Security
- Enhanced file index encryption implementation
- Improved credential storage in system keyring
- Added file operation validation

## [1.1.2] - Previous releases

### Notes
- Earlier versions prior to comprehensive documentation
- Initial feature parity with web client
- Basic sync and cloud browser functionality
- System tray integration

---

## Future Roadmap

### v1.3.0 (Planned)
- Real-time cloud-to-local sync notifications (if Proton API allows)
- File versioning browser
- Advanced conflict resolution UI
- Performance optimizations for large folders

### v2.0.0 (Long-term)
- Plugin system for custom sync rules
- Advanced scheduling options
- Mobile device integration
- Web dashboard for monitoring

---

## Updating This Changelog

When making a release:

1. Update this file with changes from current version
2. Change `[Unreleased]` section to `[X.Y.Z] - YYYY-MM-DD`
3. Update version in `package.json`
4. Create git tag: `git tag vX.Y.Z`
5. Document release notes on GitHub Releases

### Categories to Use

- **Added** - New features
- **Changed** - Changes in existing functionality
- **Deprecated** - Soon-to-be removed features
- **Removed** - Removed features
- **Fixed** - Bug fixes
- **Security** - Security-related changes
- **Performance** - Performance improvements
- **Documentation** - Documentation-only changes
