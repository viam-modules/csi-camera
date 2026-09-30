# Develop

## Base Images
The `base` images contains minimal dependency for viam-cpp-sdk module development. Both the `jetson` and `pi` targets build from the [`jammy`](../etc/Dockerfile.base) base image (the legacy [`bullseye`](../etc/Dockerfile.base.bullseye) image is EOL and unused). Base images include the following dependencies:
- `viam-cpp-sdk` for building the module binary
- `appimage-builder` for packaging into an appimage

```bash
make TARGET=[pi/jetson] image-base # Rebuild base image
```

Bump `BASE_TAG` in the `Makefile` before publishing a new release; `push-base`
refuses to overwrite an existing semver tag. It pushes both the semver tag and
`latest`.

```bash
make TARGET=[pi/jetson] push-base # Push updated base image to container registry
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
