# Raspberry Pi

## Dependencies

The `csi-cam-pi` module links against the host GStreamer and uses the Raspberry Pi OS `libcamerasrc` plugin. On first install the module's `first_run.sh` runs `apt-get install` for
`libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libcamera`
(as root, or via passwordless `sudo`). If that is not possible on your machine, install them yourself:

```bash
sudo apt-get install -y libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libcamera
```

Raspberry Pi OS Bullseye or newer is required (glibc 2.31+).

Make sure that the `/boot/config.txt` is configured to detect the camera. See [this guide](https://www.raspberrypi.com/documentation/computers/camera_software.html) for more information. For example, if you are using the IMX 477, you should have the following lines in your `/boot/config.txt`:

```bash
camera_auto_detect=0
dtoverlay=imx477
```

___

## Camera Attributes

| Key Name    | Value Type | Description                                  |
|-------------|------------|----------------------------------------------|
| width_px    | Integer    | Width of the video in pixels (e.g., 1920).  |
| height_px   | Integer    | Height of the video in pixels (e.g., 1080). |
| frame_rate  | Integer    | Frames per second of the video (e.g., 30).  |
| debug       | Boolean    | Flag indicating debug mode (e.g., true).    |

___

## Tested Setups

| Device | OS | Camera | Provider |
|------------------|-----------------|-----------------|-----------------|
| Raspberry Pi 5 | Debian Bookworm | OV5647 | Arducam |
| Raspberry Pi 4B | Debian Bullseye | RPI GS Camera | Raspberry Pi |
| Raspberry Pi 4B | Debian Bookworm | IMX 477 | Arducam |

____

## Tools

| Tool | Description |
|------------------|-----------------|
| test_pi_pipeline.sh | Test script to see if GST and camera sensor are working. |
