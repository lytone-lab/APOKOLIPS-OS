# apokolips-managed: external-app theming for apps you start from a terminal.
# Loaded from ~/.bashrc. Delete that one line (or this file) to turn it off.
export GTK_THEME=Adwaita:dark
export QT_QPA_PLATFORMTHEME=qt6ct:qt5ct
export QT_STYLE_OVERRIDE=Fusion
# VLC keeps its own dark palette; give it the same setup as the Dock does.
vlc() { env -u QT_STYLE_OVERRIDE QT_QPA_PLATFORMTHEME=gtk3 GTK_THEME=Adwaita-dark vlc "$@"; }
