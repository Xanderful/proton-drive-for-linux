# Security Policy

## Reporting Security Issues

⚠️ **Please do not open public GitHub issues for security vulnerabilities.**

If you discover a security vulnerability in this project, please report it privately to the maintainer(s) instead of using the public issue tracker.

### How to Report

1. **Email:** Contact the maintainer privately
2. **Subject:** Include `[SECURITY]` in email subject
3. **Include:**
   - Detailed description of the vulnerability
   - Steps to reproduce (if applicable)
   - Potential impact and severity assessment
   - Suggested fix (if you have one)
   - Your name and contact information (optional)

### Timeline

- **48 hours** - Acknowledgment of receipt
- **7 days** - Initial assessment and plan
- **30 days** - Fix development and testing
- **Public disclosure** - Coordinated after fix is released

## Security Features

This project implements the following security practices:

### Authentication & Authorization
- **OAuth2** authentication through Proton's official APIs
- No plaintext credentials stored locally
- Credentials managed via system keyring (XDG Secret Service)

### Data Storage
- **File index encryption:** SQLite database with PBKDF2 (100k iterations)
- **Local cache:** `~/.cache/proton-drive/file_index.db` (encrypted)
- **Configuration:** `~/.config/proton-drive/` (user-owned, restricted permissions)

### File Operations
- **inotify-based monitoring** for local file changes
- **rclone integration** with path validation
- **Sync safety features:** Conflict detection, delete protection

### Network Security
- All communication over HTTPS
- OAuth2 tokens handled securely
- No sensitive data logged in debug output

## Supported Versions

Only the **latest released version** receives security updates and fixes.

Older versions may not receive patches for newly discovered vulnerabilities. We recommend always updating to the latest version.

## Known Limitations

### API Limitations
This project relies on Proton Drive's public API and rclone for synchronization. Security is limited by:
- Proton Drive API capabilities
- rclone's handling of credentials
- System keyring implementation

### Encryption Scope
- **Local index:** Encrypted at rest (PBKDF2)
- **Cloud files:** Encrypted by Proton Drive (end-to-end)
- **Credentials:** Encrypted in system keyring
- **Logs:** NOT encrypted (may contain file paths)

For maximum security, we recommend:
- Using full-disk encryption (LUKS/BitLocker)
- Securing system keyring with strong password
- Reviewing log contents for sensitive information
- Using over a trusted network connection

## Security Best Practices for Users

1. **Keep Updated** - Always use the latest version
2. **Protect Credentials** - Use a strong, unique Proton account password
3. **System Security** - Maintain up-to-date OS and dependencies
4. **File Permissions** - Don't run the app with elevated privileges (sudo)
5. **Network** - Use VPN for untrusted networks (if needed)
6. **Keyring** - Lock system keyring when not in use

## Disclosure Policy

We follow responsible disclosure practices:

1. **Private Report** - Vulnerabilities reported privately
2. **Verification** - We verify and assess the issue
3. **Fix Development** - We develop and test a fix
4. **Coordinated Release** - We release fix publicly
5. **Credit** - Reporter credited (unless they prefer anonymity)

---

For more information on security implementation, see [FILE_INDEX_SECURITY.md](docs/FILE_INDEX_SECURITY.md).
