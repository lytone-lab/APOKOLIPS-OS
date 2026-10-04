#!/bin/bash
# =========================================================
# Apokolips OS — bootstrap
# Run on a fresh Ubuntu 26.04 install to recreate everything.
# Usage: ./bootstrap.sh
# =========================================================
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
say() { echo -e "${GREEN}==>${NC} $1"; }
warn() { echo -e "${YELLOW}!!${NC} $1"; }
fail() { echo -e "${RED}ERR${NC} $1"; exit 1; }

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
SYS="$REPO_DIR/system"

[ -d "$SYS" ] || fail "system/ folder missing — clone the full repo"

# ---- 1. apt packages ----
say "Installing apt packages (this may take 5-10 min)..."
sudo apt update
sudo apt install -y \
    build-essential cmake g++ git \
    qt6-base-dev qt6-declarative-dev qt6-base-dev-tools qt6-wayland \
    libgl1-mesa-dev \
    cage \
    plymouth plymouth-themes imagemagick \
    gnome-terminal eog totem \
    openssh-server

# ---- 2. Plymouth theme (Ember Pulse) ----
say "Installing Plymouth Ember Pulse theme..."
sudo mkdir -p /usr/share/plymouth/themes/apokolips
sudo cp -r "$SYS/apokolips/"* /usr/share/plymouth/themes/apokolips/ 2>/dev/null || true
sudo cp "$REPO_DIR/omega-logo-red.png" \
        /usr/share/plymouth/themes/apokolips/omega.png 2>/dev/null || true

# Regenerate glow + ember PNGs
magick -size 600x600 xc:none -fill '#c8102e' \
    -draw "circle 300,300 300,150" -blur 0x30 /tmp/glow.png
magick -size 14x14 xc:none -fill '#ff5522' \
    -draw "circle 7,7 7,2" -blur 0x2 /tmp/ember.png
sudo cp /tmp/glow.png  /usr/share/plymouth/themes/apokolips/glow.png
sudo cp /tmp/ember.png /usr/share/plymouth/themes/apokolips/ember.png

# Late-phase wordmark theme
sudo mkdir -p /usr/share/plymouth/themes/apokolips-late
magick -background none -font DejaVu-Sans-ExtraLight -pointsize 64 \
    -fill '#ffeae0' -kerning 18 label:'Apokolips' \
    /tmp/wordmark.png 2>/dev/null || \
magick -background none -font DejaVu-Sans -pointsize 64 \
    -fill '#ffeae0' -kerning 18 label:'Apokolips' /tmp/wordmark.png
sudo cp /tmp/wordmark.png \
    /usr/share/plymouth/themes/apokolips-late/wordmark.png
if [ -f "$SYS/apokolips-late.plymouth" ]; then
    sudo cp "$SYS/apokolips-late.plymouth" \
        /usr/share/plymouth/themes/apokolips-late/apokolips-late.plymouth
fi
if [ -f "$SYS/apokolips-late.script" ]; then
    sudo cp "$SYS/apokolips-late.script" \
        /usr/share/plymouth/themes/apokolips-late/apokolips-late.script
fi

# Point Plymouth at Ember Pulse
sudo tee /etc/plymouth/plymouthd.conf > /dev/null <<EOF
[Daemon]
Theme=apokolips
ShowDelay=0
DeviceTimeout=8
EOF

# Late-phase text fallback
sudo sed -i 's/^title=.*/title=Apokolips OS 26.04/' \
    /usr/share/plymouth/themes/ubuntu-text/ubuntu-text.plymouth
sudo sed -i 's/^black=.*/black=0x1a0208/' \
    /usr/share/plymouth/themes/ubuntu-text/ubuntu-text.plymouth
sudo sed -i 's/^brown=.*/brown=0xc8102e/' \
    /usr/share/plymouth/themes/ubuntu-text/ubuntu-text.plymouth

# ---- 3. GRUB branding ----
say "Branding GRUB..."
sudo sed -i 's/^GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="Apokolips OS"/' \
    /etc/default/grub
sudo sed -i '/^GRUB_COLOR_NORMAL=/d;/^GRUB_COLOR_HIGHLIGHT=/d' \
    /etc/default/grub
sudo tee -a /etc/default/grub > /dev/null <<EOF
GRUB_COLOR_NORMAL="light-gray/black"
GRUB_COLOR_HIGHLIGHT="white/dark-red"
EOF
sudo update-grub

# ---- 4. GDM login logo + version string ----
say "Branding GDM login..."
sudo mkdir -p /usr/share/images/vendor-logos
sudo magick "$REPO_DIR/omega-logo-red.png" -resize x64 \
    /usr/share/images/vendor-logos/logo-text-version-64.png
sudo magick "$REPO_DIR/omega-logo-red.png" -resize x128 \
    /usr/share/images/vendor-logos/logo-text-version-128.png
sudo sed -i "s|^#logo=.*|logo='/usr/share/images/vendor-logos/logo-text-version-64.png'|" \
    /etc/gdm3/greeter.dconf-defaults 2>/dev/null || true
sudo sed -i 's/^PRETTY_NAME=.*/PRETTY_NAME="Apokolips OS 26.04"/' \
    /etc/os-release /usr/lib/os-release
sudo dconf update 2>/dev/null || true

# ---- 5. Apokolips Wayland session ----
say "Registering Apokolips session..."
sudo mkdir -p /usr/share/wayland-sessions
cat > /tmp/apokolips.desktop <<EOF
[Desktop Entry]
Name=Apokolips
Comment=Apokolips OS - custom shell
Exec=cage $REPO_DIR/run-session.sh
Type=Application
DesktopNames=Apokolips
EOF
sudo cp /tmp/apokolips.desktop /usr/share/wayland-sessions/apokolips.desktop

cat > "$REPO_DIR/run-session.sh" <<EOF
#!/bin/bash
cd "$REPO_DIR"
exec ./build/apokolips-shell
EOF
chmod +x "$REPO_DIR/run-session.sh"

# ---- 6. AccountsService — default this user to Apokolips ----
say "Setting default session to Apokolips..."
CURRENT_USER="${SUDO_USER:-$USER}"
sudo mkdir -p /var/lib/AccountsService/users
sudo tee /var/lib/AccountsService/users/$CURRENT_USER > /dev/null <<EOF
[User]
Session=apokolips
XSession=apokolips
SystemAccount=false
EOF
sudo chmod 600 /var/lib/AccountsService/users/$CURRENT_USER
sudo chown root:root /var/lib/AccountsService/users/$CURRENT_USER

# ---- 7. Rebuild initramfs (for Plymouth) ----
say "Rebuilding initramfs..."
sudo update-initramfs -u

# ---- 8. Build the shell ----
say "Building Apokolips shell..."
cd "$REPO_DIR"
cmake -S . -B build
cmake --build build

say "Done. Reboot to see the fully branded boot chain."
say "Login: pick 'Apokolips' from the session gear, or it's already default."
