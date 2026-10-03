#!/bin/bash
export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
export QT_QPA_PLATFORM=wayland
exec ./build/apokolips-shell "$@"
