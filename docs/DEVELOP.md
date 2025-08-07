# Develop

## Base Images
The `base` images contains minimal dependency for viam-cpp-sdk module development. This repository includes a [`jammy`](../etc/Dockerfile.base) and [`bullseye`](../etc/Dockerfile.base.bullseye) base image for the `jetson` and `pi` targets respectively. Base images include the following dependencies:
- `viam-cpp-sdk` for building the module binary
- `appimage-builder` for packaging into an appimage

```bash
make TARGET=[pi/jetson] image-base # Rebuild base image
```

```bash
make TARGET=[pi/jetson] push # Push updated base image to container registry
```

## Build Locally with Canon

```bash
canon -profile=[csi-pi/csi-jetson] # Loads base image with Canon
```

```bash
make dep TARGET=[pi/jetson] # Install platform specific dependencies
```
- Jetson:
    - `gstreamer`
- Pi:
    - `gstreamer`
    - `libcamera`

```bash
make build # Build binary
```

```bash
make package TARGET=[pi/jetson] # Build appiamge
```
- Jetson appimage [recipe](../etc/viam-csi-jetson-arm64.yml)
- Pi appimage [recipe](../etc/viam-csi-pi-arm64.yml)
