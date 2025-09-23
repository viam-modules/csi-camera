#!/bin/sh

if [ -f /etc/os-release ]; then
    ID=$(grep '^ID=' /etc/os-release | cut -d'=' -f2 | sed 's/"//g')
    if [ "$ID" = "debian" ] || [ "$ID" = "ubuntu" ]; then
        apt-get install -qqy libfuse2
        if [ $? -eq 0 ]; then
            echo "Successfully installed libfuse2"
        else
            echo "Failed to install libfuse2. Make sure that libfuse2 is installed. See https://github.com/AppImage/AppImageKit/wiki/FUSE."
        fi
    else
        echo "Detected a non-Debian or Ubuntu system, unable to automatically install libfuse2. Make sure that libfuse2 is installed. See https://github.com/AppImage/AppImageKit/wiki/FUSE."
    fi
else
    echo "Unable to determine the operating system, unable to automatically install libfuse2. Make sure that libfuse2 is installed. See https://github.com/AppImage/AppImageKit/wiki/FUSE."
fi

exec ./bin/viam-csi-latest-aarch64.AppImage "$@"
