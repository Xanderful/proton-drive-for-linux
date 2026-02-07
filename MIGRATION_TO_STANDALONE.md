# Migration to Standalone Repository

## Summary

This repository has been migrated from a forked repository to a standalone, independently-maintained repository. All references to the original fork have been updated to reflect the new ownership under `Xanderful/proton-drive-linux`.

## Changes Made

### 1. Documentation

- **[README.md](README.md)**: Updated clone URL from placeholder `your-username` to `Xanderful`
  - Before: `git clone https://github.com/your-username/proton-drive-linux.git`
  - After: `git clone https://github.com/Xanderful/proton-drive-linux.git`

### 2. AUR Packaging

- **[aur/PKGBUILD](aur/PKGBUILD)**:
  - Maintainer: `DonnieDice` → `Xanderful`
  - Maintainer email: Updated to match new owner
  - Repository URL: `DonnieDice/protondrive-linux` → `Xanderful/proton-drive-linux`
  - Release download URL: Updated to point to new repository

### 3. GitHub Actions Workflows

- **[.github/workflows/publish-aur.yml](.github/workflows/publish-aur.yml)**:
  - `.SRCINFO` generation: Updated repository URL in metadata
  - Release asset downloads: Updated to use new repository path
  - Git config: Updated committer name and email for AUR package commits

- **[.github/workflows/generate-package-specs.yml](.github/workflows/generate-package-specs.yml)**:
  - PKGBUILD maintainer comment: Updated to new owner
  - Maintainer email: Updated to new contact information

### 4. Packaging Configuration

- **[snap/snapcraft.yaml.template](snap/snapcraft.yaml.template)**:
  - Contact URL: Updated to `Xanderful/proton-drive-linux`

### 5. CMake Build Configuration

- **[src-native/CMakeLists.txt](src-native/CMakeLists.txt)**:
  - `CPACK_PACKAGE_CONTACT`: `DonnieDice` → `Xanderful`
  - `CPACK_PACKAGE_VENDOR`: `DonnieDice` → `Xanderful`
  - These values are used in generated DEB, RPM, and other package metadata

## What Was NOT Changed

### Dependencies

- **SYNC_SETUP.md**: References to `coderFrankenstain/rclone` remain unchanged
  - This is a legitimate dependency fork used to apply Proton Drive-specific fixes
  - Not modified as part of this migration since it's a build-time dependency, not the main repository

### Package Configuration

- **[package.json](package.json)**: Already correctly referenced `Xanderful/proton-drive-linux` in repository field

### Dynamic References

- GitHub Actions workflows use `${{ github.repository }}` where appropriate, which automatically resolves to the current repository

## Migration Checklist

✅ Repository owner references updated
✅ All hardcoded URLs changed to new owner
✅ AUR package metadata updated
✅ Snap configuration updated
✅ CMake packaging configuration updated
✅ GitHub Actions workflows updated
✅ Documentation verified for accuracy
✅ No placeholder text remaining
✅ Dependency fork references retained (intentional)

## Next Steps for Repository Transfer

When transferring to the new standalone repository:

1. **Create new repository** at `github.com/Xanderful/proton-drive-linux`
2. **Mirror content** using:

   ```bash
   git clone --mirror https://github.com/old-fork/protondrive-linux.git
   cd protondrive-linux.git
   git push --mirror https://github.com/Xanderful/proton-drive-linux.git
   cd ..
   rm -rf protondrive-linux.git
   ```

3. **Verify** that:
   - All GitHub Actions workflows pass
   - Release artifacts are generated correctly
   - AUR package can be published
   - Snap build succeeds
   - AppImage creation works

4. **Update** any external links:
   - ProtonMail/Proton community forums (if posted)
   - AUR package page (ensure the package is linked to correct repository)
   - Any CI/CD secrets needed for publishing

5. **Test the entire release flow**:
   - Create a test release/tag
   - Verify AppImage downloads successfully
   - Test snap build in snapcraft CI
   - Verify AUR publication workflow

## Important Notes

- **Email address**: All maintainer emails should be updated to `xanderful@proton.me` (or appropriate email for new owner)
- **GitHub organization**: Ensure all branch protections and permissions are set up correctly
- **Secrets**: Verify that GitHub Actions secrets (like AUR_SSH_KEY) are properly configured in the new repository
- **CI/CD**: All workflows will need to be validated in the new repository context

---

**Migration completed on**: 2026-02-07
**Status**: Ready for repository transfer
