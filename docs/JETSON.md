# Jetson

## Dependencies

On Jetson devices, GStreamer and the Libargus plugin come packaged with JetPack. The module links against them at runtime; nothing else needs to be installed. JetPack 5 or newer is required (glibc 2.31+).

___

## Camera Attributes

| Key Name    | Value Type | Description                                  |
|-------------|------------|----------------------------------------------|
| video_path  | String     | Input sensor number (e.g., "0") or path to video file (e.g. /dev/video0). |
| width_px    | Integer    | Width of the video in pixels (e.g., 1920).  |
| height_px   | Integer    | Height of the video in pixels (e.g., 1080). |
| frame_rate  | Integer    | Frames per second of the video (e.g., 30).  |
| debug       | Boolean    | Flag indicating debug mode (e.g., true).    |

___

## Tested Setups

| Device | OS | Camera | Provider |
|------------------|-----------------|-----------------|-----------------|
| Jetson Orin-Nano | L4T 35.3.1 | IMX 219 | Waveshare |
| Jetson Nano | L4T 32.7.1 | IMX 477 | Arducam |

____

## Tools

| Tool | Description |
|------------------|-----------------|
| test_jetson_pipeline.sh | Test script to see if GST and camera sensor are working. |
| make restart-argus | Run this if argus backend gets into a bad state. |
