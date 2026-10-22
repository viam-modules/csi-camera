// Pi discovery backend: enumerate a camera's native sensor modes via libcamera.
// Compiled only with VIAM_CSI_WITH_LIBCAMERA (needs libcamera dev + runtime).
//
// The Raw stream role yields the sensor's native modes (the short list
// `rpicam-hello --list-cameras` shows), not the thousands of ISP scale targets
// the libcamerasrc gstreamer caps advertise.
//
// UNVERIFIED: not yet built or run against real hardware.

#include <libcamera/libcamera.h>

#include <cmath>
#include <cstdint>

#include <viam/sdk/log/logging.hpp>

#include "discovery.h"

namespace csi {

namespace {

// Max fps a mode supports = 1e6 / shortest frame duration. libcamera reports
// FrameDurationLimits per the *configured* mode, so the camera must be acquired
// and configured to this size first. Returns 0 (unknown) if anything fails.
int max_fps_for_mode(const std::shared_ptr<libcamera::Camera>& camera,
                     libcamera::CameraConfiguration* config,
                     libcamera::StreamConfiguration& stream,
                     const libcamera::PixelFormat& pixelformat,
                     const libcamera::Size& size) {
    stream.pixelFormat = pixelformat;
    stream.size = size;
    if (config->validate() == libcamera::CameraConfiguration::Invalid) {
        return 0;
    }
    if (camera->configure(config) != 0) {
        return 0;
    }

    const libcamera::ControlInfoMap& controls = camera->controls();
    const auto it = controls.find(&libcamera::controls::FrameDurationLimits);
    if (it == controls.end()) {
        return 0;
    }
    // FrameDurationLimits is in microseconds; the minimum duration is the
    // fastest frame, hence the highest fps.
    const std::int64_t min_duration_us = it->second.min().get<std::int64_t>();
    if (min_duration_us <= 0) {
        return 0;
    }
    return static_cast<int>(std::llround(1e6 / static_cast<double>(min_duration_us)));
}

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

            // Reading per-mode fps needs the camera acquired (to configure it).
            // If it's busy, still report the modes, just without fps.
            const bool acquired = camera->acquire() == 0;
            if (!acquired) {
                VIAM_SDK_LOG(warn) << "could not acquire camera " << camera->id() << "; reporting modes without fps";
            }

            libcamera::StreamConfiguration& stream = config->at(0);
            for (const auto& pixelformat : stream.formats().pixelformats()) {
                for (const auto& size : stream.formats().sizes(pixelformat)) {
                    sensor_mode m;
                    m.sensor_id = camera->id();
                    m.width_px = static_cast<int>(size.width);
                    m.height_px = static_cast<int>(size.height);
                    m.format = pixelformat.toString();
                    m.frame_rate = acquired ? max_fps_for_mode(camera, config.get(), stream, pixelformat, size) : 0;
                    modes.push_back(m);
                }
            }

            if (acquired) {
                camera->release();
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
