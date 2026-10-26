#!/usr/bin/env bash
#
# Installs host runtime deps: the gstreamer the module links against plus the
# per-platform tool discovery shells out to (rpicam-apps on Pi, v4l-utils on
# Jetson). JetPack already ships gstreamer + Argus.

# -e omitted: a failing sudo -n must not abort before the warning prints.
set -uo pipefail

[[ "$(uname -s)" == "Linux" ]] || exit 0

model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null | tr '[:upper:]' '[:lower:]')
case "$model" in
    *raspberry*pi*)
        # rpicam-apps provides rpicam-hello, which discovery shells out to.
        PKGS=(libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libcamera rpicam-apps)
        ;;
    *nvidia*|*jetson*)
        # JetPack ships gstreamer + Argus; v4l-utils provides v4l2-ctl for discovery.
        PKGS=(v4l-utils)
        ;;
    *)
        echo "Unrecognized device; nothing to install."; exit 0
        ;;
esac
MANUAL="sudo apt-get install -y ${PKGS[*]}"

SUDO=""
if [[ "$EUID" -ne 0 ]]; then
    if sudo -n true 2>/dev/null; then
        SUDO="sudo -n"
    else
        echo "WARNING: not root and passwordless sudo unavailable; cannot install runtime deps."
        echo "To install manually, run: $MANUAL"
        exit 0
    fi
fi

export DEBIAN_FRONTEND=noninteractive
if $SUDO apt-get update && $SUDO apt-get install -y --no-install-recommends "${PKGS[@]}"; then
    echo "Installed runtime deps: ${PKGS[*]}"
else
    echo "WARNING: failed to install runtime deps."
    echo "To install manually, run: $MANUAL"
fi
