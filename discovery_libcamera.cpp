// Pi discovery backend: enumerate a camera's native sensor modes via libcamera.
// Compiled only with VIAM_CSI_WITH_LIBCAMERA (needs libcamera dev + runtime).
//
// The Raw stream role yields the sensor's native modes (the short list
// `rpicam-hello --list-cameras` shows), not the thousands of ISP scale targets
// the libcamerasrc gstreamer caps advertise.
//
// UNVERIFIED: not yet built or run against real hardware. frame_rate per mode is
// still TODO (see below), so emitted configs currently omit it.

#include <libcamera/libcamera.h>

#include <viam/sdk/log/logging.hpp>

#include "discovery.h"

namespace csi {

namespace {

class libcamera_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        std::vector<sensor_mode> modes;

        libcamera::CameraManager manager;
        if (manager.start() != 0) {
            VIAM_SDK_LOG(error) << "libcamera CameraManager failed to start";
            return modes;
        }

        for (const auto& camera : manager.cameras()) {
            auto config = camera->generateConfiguration({libcamera::StreamRole::Raw});
            if (!config || config->empty()) {
                VIAM_SDK_LOG(warn) << "no raw configuration for camera " << camera->id();
                continue;
            }

            const libcamera::StreamFormats& formats = config->at(0).formats();
            for (const auto& pixelformat : formats.pixelformats()) {
                for (const auto& size : formats.sizes(pixelformat)) {
                    sensor_mode m;
                    m.sensor_id = camera->id();
                    m.width_px = static_cast<int>(size.width);
                    m.height_px = static_cast<int>(size.height);
                    // TODO(RSDK-12030): derive per-mode fps from
                    // controls::FrameDurationLimits; 0 => config omits frame_rate.
                    m.frame_rate = 0;
                    m.format = pixelformat.toString();
                    modes.push_back(m);
                }
            }
        }

        manager.stop();
        return modes;
    }
};

}  // namespace

std::unique_ptr<mode_enumerator> make_libcamera_enumerator() {
    return std::make_unique<libcamera_enumerator>();
}

}  // namespace csi
