#!/usr/bin/env bash
#
# Build and package the module as module.tar.gz for TARGET (jetson|pi, default
# jetson). Run inside a cpp-sdk-conan image: its baked `default` profile and
# viamconan remote are the toolchain of record.

set -euxo pipefail

cd "$(dirname "$0")/.."

TARGET="${TARGET:-jetson}"
case "$TARGET" in
    jetson|pi) ;;
    *) echo "TARGET must be jetson or pi, got '$TARGET'" >&2; exit 1 ;;
esac

VERSION="$(conan inspect . --format json | python3 -c 'import json, sys; print(json.load(sys.stdin)["version"])')"

# Host-gstreamer recipe is not on any remote yet.
conan export etc/conan/gstreamer

conan create . \
    -o "&:with_tests=False" \
    -o "viam-csi/*:target=${TARGET}" \
    -c tools.system.package_manager:mode=install \
    --build=missing

conan install --requires="viam-csi/${VERSION}" \
    -o "viam-csi/*:target=${TARGET}" \
    --lockfile-partial \
    --deployer-package "&" \
    --envs-generation false
