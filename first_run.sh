#!/usr/bin/env bash
#
# Installs the host gstreamer runtime the module links against. JetPack ships
# it; Raspberry Pi OS carries it (and the libcamera source plugin) in apt.

# -e omitted: a failing sudo -n must not abort before the warning prints.
set -uo pipefail

[[ "$(uname -s)" == "Linux" ]] || exit 0

model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null | tr '[:upper:]' '[:lower:]')
case "$model" in
    *raspberry*pi*) ;;
    *) echo "Not a Raspberry Pi; nothing to install."; exit 0 ;;
esac

PKGS=(libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libcamera)
MANUAL="sudo apt-get install -y ${PKGS[*]}"

SUDO=""
if [[ "$EUID" -ne 0 ]]; then
    if sudo -n true 2>/dev/null; then
        SUDO="sudo -n"
    else
        echo "WARNING: not root and passwordless sudo unavailable; cannot install gstreamer runtime."
        echo "To install manually, run: $MANUAL"
        exit 0
    fi
fi

export DEBIAN_FRONTEND=noninteractive
if $SUDO apt-get update && $SUDO apt-get install -y --no-install-recommends "${PKGS[@]}"; then
    echo "Installed gstreamer runtime: ${PKGS[*]}"
else
    echo "WARNING: failed to install gstreamer runtime."
    echo "To install manually, run: $MANUAL"
fi
