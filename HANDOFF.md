# APOKOLIPS-OS Handoff & Current State

## 1. Context
APOKOLIPS-OS is a custom Linux desktop environment built in Qt6 C++17 on Sway/Wayland. 
Repo: /home/lytone/apokolips-shell
Build: `cmake --build build -j1` (MUST be -j1 to avoid OOM segfaults)
Restart: `systemctl --user restart apokolips-shell.service`

## 2. What Works (V1 Shell is stable)
- **Theming**: GTK3/GTK4 (Sigil dark CSS) and Qt5/Qt6 (Kvantum Sigil theme). External apps like Nautilus and VLC match the OS.
- **Audio**: Bounded via `audio_safe.h` (`playSoundSafe` with 2s hard timeout + kill-previous). No more hanging paplay processes. VM quantum set to 4096.
- **Drag & Drop**: Finder -> Desktop, Desktop Icons, and Dock icons. The receiving apps and the app-launch-with-file path are fully wired.
- **Core UX**: Mission Control (Super+A), Lockscreen, SigilDialog (free-space morph), Workspaces UI, Dynamic Dock.
- **External Apps**: VLC fallback for BoomTube media errors (uses XWayland via env stripping).

## 3. What We Are Doing Right Now
We are building the **Level 1 Live ISO** (no installer, just bootable) using a custom CLI script because the Cubic GUI crashed due to VirtualBox keyboard/Wayland focus bugs.

**The Build Script**: `/home/lytone/iso-build/build-iso-cli.sh`
- It extracts the Ubuntu 26.04.1 ISO to `~/iso-build/work/mnt`
- Unsquashes `casper/minimal.squashfs` (Ubuntu 26.04 uses overlay layers, not a single `filesystem.squashfs`)
- Chroots in, installs sway/qt6/vlc/etc via `customize.sh`
- Repacks `minimal.squashfs` using `zstd`
- Rebuilds the ISO using `xorriso`

**Current Bug Being Fixed**: The `rsync` command excluded `minimal.squashfs` from the ISO copy, so `rm` failed. We patched it to `rm -f` and are re-running the build.

## 4. What To Do Next (For the Next AI)
1. **Wait for the ISO build to finish.** The output ISO will be at `/home/lytone/iso-build/APOKOLIPS-1.0.iso`.
2. **Test the ISO.** Create a new VirtualBox VM with:
   - **Graphics Controller: VBoxSVGA** (cursor fix)
   - **VRAM: 128 MB**
   - **RAM: 4096 MB**
   - Boot from the ISO.
3. **Debug the Boot.** If it boots to a black screen or tty1 instead of the shell:
   - Log in at tty1 (`ubuntu:ubuntu`)
   - Check `journalctl --user -u apokolips-shell -n 50`
   - Check if `gdm3` was successfully disabled (we run `systemctl set-default multi-user.target` and mask gdm).
   - Check if `/usr/local/bin/apokolips-shell` exists and is executable.
   - Check if `/etc/skel/.bash_profile` execs `sway`.
4. **Iterate the ISO**: If packages are missing, edit `~/iso-build/customize.sh`, then run `sudo ~/iso-build/build-iso-cli.sh` again.

## 5. Critical Rules for the Next AI
- **Do NOT use the Cubic GUI.** It is broken on this VM.
- **Do NOT use sudo with $HOME** in scripts; hardcode `/home/lytone/` paths.
- **Do NOT use `sudo cubic`** (it errors).
- **Never claim success without seeing build output.**
- **Use nano for long text files.** Heredocs get cut off by the terminal.
- Read `TEAM_PROTOCOL.md` for the rules of engagement.

## 6. Git State
Latest commit: `8e075a7` (Audio bounded + drag-drop complete + SigilDialog button fix).
