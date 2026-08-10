# Third-Party Licenses

This project bundles and depends on several open-source components. This file documents their licenses and attribution requirements.

## Core Components (Bundled in APK)

### PRoot
- **Source**: https://github.com/proot-me/proot
- **License**: GPL-2.0-only
- **Description**: Rootless Linux userspace isolation via ptrace
- **Usage**: Embedded binary for running Debian ELF binaries on Android
- **Attribution**: "This product includes software developed by the PRoot project (https://github.com/proot-me/proot)"

### Debian 13 (Trixie) Rootfs
- **Source**: https://www.debian.org/
- **License**: Various (DFSG-compatible)
- **Description**: Base Linux userspace (bash, coreutils, apt, systemd, etc.)
- **Usage**: Complete Debian ARM64 root filesystem
- **Attribution**: "This product includes software from the Debian Project (https://www.debian.org/)"

### XFCE Desktop Environment
- **Source**: https://xfce.org/
- **License**: GPL-2.0, LGPL-2.0, BSD-2-Clause
- **Description**: Lightweight desktop environment
- **Components**: xfce4-panel, xfdesktop4, xfwm4, xfce4-settings, thunar, xfce4-terminal, xfce4-whiskermenu-plugin
- **Attribution**: "This product includes software from the XFCE Project (https://xfce.org/)"

### Firefox ESR
- **Source**: https://www.mozilla.org/firefox/
- **License**: MPL-2.0
- **Description**: Web browser (Extended Support Release)
- **Usage**: Firefox ESR from Debian repositories
- **Attribution**: "This product includes software from the Mozilla Foundation (https://www.mozilla.org/)"

### X.Org Server (Xvfb, Xwayland)
- **Source**: https://www.x.org/
- **License**: MIT
- **Description**: X Window System display server
- **Components**: xserver-xorg-core, xvfb, xwayland, xauth, x11-xserver-utils
- **Attribution**: "This product includes software from the X.Org Foundation (https://www.x.org/)"

### LightDM
- **Source**: https://github.com/canonical/lightdm
- **License**: GPL-3.0, LGPL-3.0
- **Description**: Display manager with auto-login support
- **Attribution**: "This product includes software from the LightDM Project"

### D-Bus
- **Source**: https://www.freedesktop.org/wiki/Software/dbus/
- **License**: GPL-2.0, AFL-2.1
- **Description**: Inter-process communication system
- **Attribution**: "This product includes software from the D-Bus Project"

### Python 3
- **Source**: https://www.python.org/
- **License**: PSF-2.0
- **Description**: Python programming language runtime
- **Attribution**: "This product includes software from the Python Software Foundation"

### Git
- **Source**: https://git-scm.com/
- **License**: GPL-2.0
- **Description**: Distributed version control system
- **Attribution**: "This product includes software from the Git Project"

### OpenSSH
- **Source**: https://www.openssh.com/
- **License**: BSD-2-Clause, BSD-3-Clause, ISC
- **Description**: SSH client and server
- **Attribution**: "This product includes software from the OpenSSH Project"

### curl / wget
- **curl Source**: https://curl.se/
- **curl License**: MIT
- **wget Source**: https://www.gnu.org/software/wget/
- **wget License**: GPL-3.0
- **Description**: Command-line HTTP/FTP clients
- **Attribution**: "This product includes software from the curl project and GNU Wget"

### nano / vim-tiny
- **nano Source**: https://www.nano-editor.org/
- **nano License**: GPL-3.0
- **vim Source**: https://www.vim.org/
- **vim License**: Vim License (GPL-compatible)
- **Description**: Text editors
- **Attribution**: "This product includes GNU nano and Vim editor"

### qemu-user-static
- **Source**: https://www.qemu.org/
- **License**: GPL-2.0, LGPL-2.0
- **Description**: User-mode emulation for debootstrap second stage
- **Usage**: Build-time only (not bundled in APK)
- **Attribution**: "This product includes software from the QEMU Project"

## Android Dependencies (Gradle)

### AndroidX Libraries
- **Source**: https://developer.android.com/jetpack/androidx
- **License**: Apache-2.0
- **Components**: core-ktx, appcompat, material, constraintlayout, lifecycle, activity, fragment, work-runtime

### Kotlin Standard Library
- **Source**: https://kotlinlang.org/
- **License**: Apache-2.0
- **Version**: 1.9.22

### Timber (Logging)
- **Source**: https://github.com/JakeWharton/timber
- **License**: Apache-2.0
- **Version**: 5.0.1

### Android Gradle Plugin
- **Source**: https://developer.android.com/studio/releases/gradle-plugin
- **License**: Apache-2.0
- **Version**: 8.5.0

### Kotlin Gradle Plugin
- **Source**: https://kotlinlang.org/
- **License**: Apache-2.0
- **Version**: 1.9.22

## Build Tools (Not Bundled)

### debootstrap
- **Source**: https://wiki.debian.org/Debootstrap
- **License**: GPL-2.0
- **Usage**: Build-time rootfs creation

### Gradle
- **Source**: https://gradle.org/
- **License**: Apache-2.0
- **Usage**: Build system

### Android SDK / NDK
- **Source**: https://developer.android.com/
- **License**: Apache-2.0 (SDK), Custom (NDK)
- **Usage**: Build toolchain

## License Compatibility Summary

| Component | License | GPL-3.0 Compatible? | Notes |
|-----------|---------|---------------------|-------|
| Linux Cyberdeck (this project) | GPL-3.0 | Yes | Main license |
| PRoot | GPL-2.0-only | No* | *GPL-2.0-only is not compatible with GPL-3.0, but PRoot is a separate process |
| Debian packages | Various DFSG | Yes | Debian ensures compatibility |
| XFCE | GPL-2.0/LGPL-2.0/BSD | Yes | LGPL-2.0 compatible with GPL-3.0 |
| Firefox | MPL-2.0 | Yes | MPL-2.0 compatible with GPL-3.0 |
| X.Org | MIT | Yes | MIT compatible with GPL-3.0 |
| LightDM | GPL-3.0 | Yes | Same license |
| D-Bus | GPL-2.0/AFL-2.1 | Yes | GPL-2.0 compatible with GPL-3.0 via "or later" |
| Python | PSF-2.0 | Yes | Compatible |
| Git | GPL-2.0 | Yes | Compatible via "or later" |
| OpenSSH | BSD/ISC | Yes | Compatible |
| curl | MIT | Yes | Compatible |
| wget | GPL-3.0 | Yes | Same license |
| nano | GPL-3.0 | Yes | Same license |
| vim | Vim License | Yes | GPL-compatible |
| AndroidX/Kotlin | Apache-2.0 | Yes | Compatible |

**Note on PRoot GPL-2.0-only**: PRoot runs as a separate process communicating via ptrace. The GPL-2.0-only license applies to PRoot itself, not to the entire combined work. Since Linux Cyberdeck communicates with PRoot at arm's length (separate process, standard IPC), this is generally considered acceptable under GPL-3.0. However, for maximum compliance, consider using a PRoot fork with "GPL-2.0-or-later" or obtaining permission.

## Mozilla Firefox Trademark Notice

Firefox is a trademark of the Mozilla Foundation. This project uses the unbranded Firefox ESR from Debian repositories. If you distribute a modified version of Firefox, you must comply with Mozilla's trademark policy: https://www.mozilla.org/en-US/foundation/trademarks/policy/

## Debian Trademark Notice

Debian is a registered trademark of Software in the Public Interest, Inc. This project uses the official Debian distribution. The name "Linux Cyberdeck" does not imply endorsement by the Debian Project.

## Attribution Requirements

When distributing this APK, you must:

1. Include a copy of the GPL-3.0 license (LICENSE file)
2. Provide access to the Corresponding Source (source code)
3. Retain all copyright notices in source files
4. Include this THIRD_PARTY_LICENSES.md file
5. For Firefox: Comply with MPL-2.0 and Mozilla trademark policy
6. For PRoot: Include GPL-2.0 license text and attribution
7. For all components: Provide attribution as listed above

## Source Code Availability

The complete corresponding source code for this project is available at:
https://github.com/yourusername/linux-cyberdeck

This includes:
- Android application source (Kotlin, XML, Gradle)
- Native layer source (C, CMake)
- Build scripts (bash)
- Documentation
- Rootfs build scripts

For bundled binaries (PRoot, Debian packages), source is available from their respective upstream repositories.

## Contact

For license questions or compliance issues:
- Open an issue at https://github.com/yourusername/linux-cyberdeck/issues
- Email: your-email@example.com