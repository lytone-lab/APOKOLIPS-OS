# APOKOLIPS-OS Handoff & Current State

## 1. Context
APOKOLIPS-OS is a custom Linux desktop environment built in Qt6 C++17 on Sway/Wayland. 
Repo: /home/lytone/apokolips-shell
Build: `cmake --build build -j1` (MUST be -j1 to avoid OOM segfaults)
Restart: `systemctl --user restart apokolips-shell.service`

## 2. What Works (V1 Shell is stable)
- **Theming**: GTK3/GTK4 (Sigil dark CSS) and Qt5/Qt6 (Kvantum Sigil theme).
- **Audio**: Bounded via `audio_safe.h` (2s hard timeout + kill-previous).
- **Drag & Drop**: Finder -> Desktop, Icons, Dock. Launching apps with dropped files works.
- **Core UX**: Mission Control (Super+A), Lockscreen, SigilDialog (free-space morph), Dynamic Dock.
- **External Apps**: VLC fallback for BoomTube media errors.

## 3. What We Are Doing Right Now
We are building the **Level 1 Live ISO** (no installer, just bootable) using a custom CLI script because the Cubic GUI crashed due to VirtualBox keyboard/Wayland focus bugs.

**The Build Script**: `/home/lytone/iso-build/build-iso-cli.sh`
- It extracts the Ubuntu 26.04.1 ISO to `~/iso-build/work/mnt`
- Unsquashes `casper/minimal.squashfs` (Ubuntu 26.04 uses overlay layers)
- Chroots in, installs sway/qt6/vlc/etc via `customize.sh`
- Repacks `minimal.squashfs` using `zstd`
- Rebuilds the ISO using `xorriso`

**Current Status**: 
1. The extraction and repacking phases *succeeded* but filled up the VM's disk.
2. The final `xorriso` command failed because we ran out of space.
3. The VM disk needs to be expanded (VirtualBox Settings -> Storage -> increase disk size, then resize partition in Ubuntu using `gparted` or `cfdisk` and `resize2fs`).
4. Once disk space is available, re-run `sudo ~/iso-build/build-iso-cli.sh`.

## 4. What To Do Next (For the Next AI)
1. **Expand VM Disk**: Guide the user to increase the VirtualBox VDI size to at least 40GB, then use `growpart` and `resize2fs` to expand the filesystem.
2. **Re-run the build**: `sudo ~/iso-build/build-iso-cli.sh`.
3. **Test the ISO**: Boot the resulting `APOKOLIPS-1.0.iso` in a new VM (Graphics: VBoxSVGA, 128MB VRAM, 4GB RAM).
4. **Debug the Boot**: If it boots to a black screen or tty1 instead of the shell, check `journalctl --user -u apokolips-shell -n 50`. Ensure `gdm3` is masked and `/etc/skel/.bash_profile` execs `sway`.

## 5. Critical Rules for the Next AI
- **Do NOT use the Cubic GUI.** It is broken on this VM.
- **Do NOT use sudo with $HOME** in scripts; hardcode `/home/lytone/` paths.
- **Use nano for long text files.** Heredocs get cut off by the terminal.
- **Never claim success without seeing build output.**
- Read `TEAM_PROTOCOL.md` for the rules of engagement.
