# Patches Directory

This directory is reserved for patches that may be needed for building on specific distributions or package formats.

## Structure

```
patches/
├── common/          # Shared patches for ALL builds
├── aur/             # Arch Linux AUR-specific
├── deb/             # Debian/Ubuntu-specific
├── rpm/             # Fedora/RHEL-specific
├── flatpak/         # Flatpak-specific
└── snap/            # Snap-specific
```

## Current Patches

No patches are currently required for native GTK4 builds.

## Adding New Patches

1. Create patch: `git diff > patches/<type>/descriptive-name.patch`
2. Use descriptive kebab-case names
3. Place in `common/` if needed for all builds, otherwise in specific package dir
