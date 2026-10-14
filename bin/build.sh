#!/usr/bin/env bash
#
# Build and package the module as module.tar.gz. Run inside a cpp-sdk-conan
# image.

set -euxo pipefail

cd "$(dirname "$0")/.."

VERSION="$(conan inspect . --format json | python3 -c 'import json, sys; print(json.load(sys.stdin)["version"])')"

# Host-gstreamer recipe is not on any remote yet.
conan export etc/conan/gstreamer

conan create . \
    -o "&:with_tests=False" \
    -c tools.system.package_manager:mode=install \
    --build=missing

conan install --requires="viam-csi/${VERSION}" \
    --lockfile-partial \
    --deployer-package "&" \
    --envs-generation false
