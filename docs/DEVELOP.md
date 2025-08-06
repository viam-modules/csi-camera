# Develop

## Docker Setup

Docker is the preferred environment for development. The Dockerfiles are split into three stages: `base`, `mod`, and `test`. 

### Docker Overview

#### Base Images
The `base` stage contains minimal dependency for viam-cpp-sdk module development. This repository includes a [`jammy`](../etc/Dockerfile.base) and [`bullseye`](../etc/Dockerfile.base.bullseye) base image for the `jetson` and `pi` targets respectively. Base images include the following dependencies:
- `viam-cpp-sdk` for building the module binary.
- `appimage-builder` for packaging into an appimage.
- `docker` for running tets in the CI layer.

#### Mod Images
The `mod` stage includes the module specific dependencies and is derived from the base image. Mod images include the following dependencies:
- `gstreamer` including good, bad, and ugly plugins.
- `libcamera` or `libargus` for camera support.
- `gtest` for unit testing.

#### Test Images
The `test` stage is a fresh OS environment used for verifying that the appimage works on a clean setup. This repository includes a [`L4T`](../etc/Dockerfile.test) and a [`bookworm`](../etc/Dockerfile.test.pi) test image.

### Docker Commands

```bash
make image-mod TARGET=[pi/jetson] # Build binary and create appimage
```

```bash
make bin-mod TARGET=[pi/jetson] # Copy appimage to bin
```

```bash
make image-test TARGET=pi
make test-package TARGET=pi # Test appimage in fresh debian bookworm image
```

```bash
make image-test TARGET=jetson
make test-package TARGET=jetson # Test appimage in fresh L4T image
```

___

## Local Setup

For local development, first install the dependencies for the [viam-cpp-sdk](https://github.com/viamrobotics/viam-cpp-sdk). Refer to the example [dockerfile](https://github.com/viamrobotics/viam-cpp-sdk/blob/main/etc/docker/Dockerfile.ubuntu.focal) for guidance on setting up the environment.

### Local Commands

```bash
canon -profile=[csi-pi/csi-jetson] # Install dependencies
```

```bash
make dep TARGET=[pi/jetson] # Install platform specific dependencies
```

```bash
make build # Build binary
```

```bash
make package TARGET=[pi/jetson] # Build appiamge
```
