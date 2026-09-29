# Develop

Builds use [Conan](https://conan.io) for dependencies and CMake for everything
else, packaged as `module.tar.gz` (binary, `meta.json`, `first_run.sh`).

Build inside the `ghcr.io/viamrobotics/cpp-sdk-conan-ubuntu:focal` image: its
baked `default` profile and `viamconan` remote are the toolchain of record, and
focal's glibc 2.31 is the floor that lets one binary run on JetPack 5/6 and
Raspberry Pi OS Bullseye and newer. The repo's `.canon.yaml` selects it:

```bash
canon            # drops into the focal image
```

## Build the module tarball

```bash
TARGET=jetson ./bin/build.sh   # or TARGET=pi; picks which meta.json is packaged
```

## Build and test

```bash
make test                      # conan install with tests, cmake presets, ctest
```

Tests run in `VIAM_CSI_TEST_MODE=1`, which swaps the camera source for
`videotestsrc`, so they need `gstreamer1.0-plugins-base` and
`gstreamer1.0-plugins-good` installed.

## Integration tests

```bash
TARGET=pi ./bin/build.sh
mkdir -p module && tar xzf module.tar.gz -C module
cd tests/integration && VIAM_CSI_DEVICE=pi VIAM_CSI_TEST_MODE=1 go test -v
```

`VIAM_CSI_MODULE_PATH` overrides the binary location.

## Lint

```bash
make lint
```

## Updating the SDK pin

`bump-viam-cpp-sdk.yml` opens a PR daily when a newer SDK release exists. It
re-seeds `conan.lock` from the SDK release's `conan.lock` so transitive
revisions match the binaries on `viamconan`. To do it by hand:

```bash
gh release download releases/vX.Y.Z --repo viamrobotics/viam-cpp-sdk --pattern conan.lock --output sdk.lock
conan export etc/conan/gstreamer
conan lock create . -o "&:with_tests=True" --lockfile sdk.lock --lockfile-partial --lockfile-out conan.lock
```
