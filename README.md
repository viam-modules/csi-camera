# CSI-CAM
> A Viam Module for CSI Cameras

![Viam Server](./etc/viam-server.png)

___

## CSI Camera Module

Easily integrate CSI Cameras into your Viam projects using this camera module.

### Supported Modules
- **Nvidia Jetson**: Use the `csi-cam` module. [More Info](./docs/JETSON.md)
- **Raspberry Pi**: Use the `csi-cam-pi` module. [More Info](./docs/PI.md)
___

## `csi-cam` model for Jetson boards

### Requirements

The `csi-cam` module is distributed as an AppImage. AppImages require FUSE version 2 to run. See [FUSE troubleshooting](https://docs.viam.com/appendix/troubleshooting/#appimages-require-fuse-to-run) for instructions on installing FUSE 2 on your system if it is not already installed.

Currently, the `csi-cam` module supports the Linux platform only.

### Usage

To use this module, follow these instructions to [add a module from the Viam Registry](https://docs.viam.com/registry/configure/#add-a-modular-resource-from-the-viam-registry) and select the `viam:camera:csi` model from the [`csi-cam` module](https://app.viam.com/module/viam/csi-cam).

### Configure your CSI camera

> [!NOTE]  
> Before configuring your camera, you must [create a robot](https://docs.viam.com/manage/fleet/robots/#add-a-new-robot).

Navigate to the **Config** tab of your robot’s page in [the Viam app](https://app.viam.com/). Click on the **Components** subtab and click **Create component**. Select the `camera` type, then select the `csi` model. Enter a name for your camera and click **Create**.

On the new component panel, copy and paste the following attribute template into your camera's **Attributes** box. 
```json
{
  "width_px": <int>,
  "height_px": <int>,
  "frame_rate": <int>,
  "flip_method": <string>,
  "debug": <bool>
}
```

> [!NOTE]  
> For more information, see [Configure a Robot](https://docs.viam.com/manage/configuration/).

Edit the attributes as applicable and save your config.
In the **Control** tab of the [Viam app](https://app.viam.com/), you can now view the camera feed. 
If you do not see anything, check the logs tab for errors.

### Attributes

The following attributes are available for `viam:camera:csi` cameras:

| Name | Type | Inclusion | Description |
| ---- | ---- | --------- | ----------- |
| `width_px` | int | Optional | Width of the image this camera captures in pixels. <br> Default: `1920` |
| `height_px` | int | Optional | Height of the image this camera captures in pixels. <br> Default: `1080` |
| `frame_rate` | int | Optional | The image capture frame rate this camera should use. <br> Default: `30` |
| `video_path` | string | Optional | The filepath to the input sensor of this camera on your board. If none is given, your robot will attempt to detect the video path automatically. <br> Default: `"0"` </br>  |
| `flip_method` | string | Optional | The flip method this camera should use. See flip method options [here](https://gstreamer.freedesktop.org/documentation/videofilter/videoflip.html?gi-language=c#members). <br> Default: `none` |
| `debug` | boolean | Optional | Whether or not you want debug input from this camera in your robot's logs. <br> Default: `false` |

Once configured, check the [Logs tab](https://docs.viam.com/program/debug/) of your robot in the Viam app to make sure your camera has connected and no errors are being raised.

### Example Configuration

```json
{
  "modules": [
    {
      "type": "registry",
      "name": "viam_csi-cam",
      "module_id": "viam:csi-cam",
      "version": "0.3.0"
    }
  ],
  "components": [
    {
      "model": "viam:camera:csi",
      "attributes": {
        "width_px": 1920,
        "height_px": 1080,
        "frame_rate": 30,
        "debug": true
      },
      "depends_on": [],
      "name": "csicam_test",
      "namespace": "rdk",
      "type": "camera"
    }
  ]
}
```
___

## Latest Version Installation

Install the latest development version of the module on your device with these commands:

### For Jetson Devices
```bash
sudo wget http://packages.viam.com/apps/csi-camera/jetson/viam-csi-latest-aarch64.AppImage -O /usr/local/bin/csi-cam
```

### For Raspberry Pi Devices
```bash
sudo wget http://packages.viam.com/apps/csi-camera/pi/viam-csi-latest-aarch64.AppImage -O /usr/local/bin/csi-cam
```

See [local-app-config.json](./etc/local-app-config.json) for how to configure with local csi-cam appimage.
___

### Develop

View [DEVELOP.md](./docs/DEVELOP.md) for more information on how to build and run the module locally or in Docker. Pull Requests and Issues are welcome!

___

## `csi-cam-pi` model for Pi boards

See [PI.md](./docs/PI.md) for dependency information and tested setups.

### Usage

To use this module, follow these instructions to [add a module from the Viam Registry](https://docs.viam.com/registry/configure/#add-a-modular-resource-from-the-viam-registry) and select the `viam:camera:csi-pi` model from the [`csi-cam-pi` module](https://app.viam.com/module/viam/csi-cam-pi).

### Configure your CSI camera

> [!NOTE]  
> Before configuring your camera, you must [create a robot](https://docs.viam.com/manage/fleet/robots/#add-a-new-robot).

Navigate to the **Config** tab of your robot’s page in [the Viam app](https://app.viam.com/). Click on the **Components** subtab and click **Create component**. Select the `camera` type, then select the `csi-pi` model. Enter a name for your camera and click **Create**.

On the new component panel, copy and paste the following attribute template into your camera's **Attributes** box. 
```json
{
  "width_px": <int>,
  "height_px": <int>,
  "frame_rate": <int>,
  "debug": <bool>
}
```

> [!NOTE]  
> For more information, see [Configure a Robot](https://docs.viam.com/manage/configuration/).

Edit the attributes as applicable and save your config.
In the **Control** tab of the [Viam app](https://app.viam.com/), you can now view the camera feed. 
If you do not see anything, check the logs tab for errors.

### Attributes

The following attributes are available for `viam:camera:csi` cameras:

| Name | Type | Inclusion | Description |
| ---- | ---- | --------- | ----------- |
| `width_px` | int | Optional | Width of the image this camera captures in pixels. <br> Default: `1920` |
| `height_px` | int | Optional | Height of the image this camera captures in pixels. <br> Default: `1080` |
| `frame_rate` | int | Optional | The image capture frame rate this camera should use. <br> Default: `30` |
| `debug` | boolean | Optional | Whether or not you want debug input from this camera in your robot's logs. <br> Default: `false` |

Once configured, check the [Logs tab](https://docs.viam.com/program/debug/) of your robot in the Viam app to make sure your camera has connected and no errors are being raised.

### Example Configuration

```json
{
  "modules": [
    {
      "type": "registry",
      "name": "viam_csi-cam-pi",
      "module_id": "viam:csi-cam-pi",
      "version": "0.3.0"
    }
  ],
  "components": [
    {
      "name": "your-csi-cam",
      "model": "viam:camera:csi-pi",
      "type": "camera",
      "namespace": "rdk",
      "attributes": {
        "width_px": 1920,
        "height_px": 1080,
        "frame_rate": 30,
        "debug": true
      },
      "depends_on": []
    }
  ]
}
```
___

## Development

Interested in contributing? Check out [DEVELOP.md](./docs/DEVELOP.md) for guidelines on how to build and run the module `locally` or using `docker`. We welcome Pull Requests and Issues!
