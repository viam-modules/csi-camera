#include "discovery.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <unordered_set>
#include <utility>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"

using namespace viam::sdk;

namespace {

// Upper bound on the /dev/videoN nodes scanned for CSI sensors.
constexpr int MAX_VIDEO_DEVICES = 64;
// V4L2 driver name of the Jetson VI (CSI capture) nodes.
constexpr const char* TEGRA_VIDEO_DRIVER = "tegra-video";

int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// Highest discrete frame rate reported for a frame size, or 0 if none is.
int max_frame_rate(int fd, uint32_t pixel_format, uint32_t width, uint32_t height) {
    int best = 0;
    v4l2_frmivalenum ival{};
    ival.pixel_format = pixel_format;
    ival.width = width;
    ival.height = height;
    for (ival.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) == 0; ival.index++) {
        if (ival.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
            break;
        }
        if (ival.discrete.numerator > 0) {
            const double fps = static_cast<double>(ival.discrete.denominator) / ival.discrete.numerator;
            best = std::max(best, static_cast<int>(std::lround(fps)));
        }
    }
    return best;
}

// Modes of one tegra-video capture node: each discrete frame size at its highest
// frame rate. A size listed under several raw formats (e.g. RG10 and RG12) is
// reported once, since Argus outputs NV12 whichever raw format the sensor uses.
std::vector<sensor_mode> enumerate_device(int fd, const std::string& sensor_id) {
    std::vector<sensor_mode> modes;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    v4l2_fmtdesc fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++) {
        v4l2_frmsizeenum size{};
        size.pixel_format = fmt.pixelformat;
        for (size.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; size.index++) {
            // Sensor modes are discrete; a stepwise range is not a known-good mode.
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                break;
            }
            const uint32_t width = size.discrete.width;
            const uint32_t height = size.discrete.height;
            if (!seen.insert({width, height}).second) {
                continue;
            }
            modes.push_back(
                {sensor_id, static_cast<int>(width), static_cast<int>(height), max_frame_rate(fd, fmt.pixelformat, width, height), ""});
        }
    }
    return modes;
}

// Jetson CSI sensors are exposed as tegra-video V4L2 capture nodes whose discrete
// frame sizes and intervals are the sensor modes Argus selects from, so they can
// be read with plain ioctls instead of linking libargus. The queries are
// read-only and succeed while nvarguscamerasrc is streaming from the sensor.
class v4l2_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        std::vector<sensor_mode> modes;
        // Assumes Argus sensor-ids follow /dev/videoN order, so the k-th
        // tegra-video node is sensor-id k (verified on a single-sensor Orin Nano).
        // Other capture nodes, e.g. USB webcams, are skipped and take no id.
        int sensor_index = 0;
        for (int n = 0; n < MAX_VIDEO_DEVICES; n++) {
            const std::string path = "/dev/video" + std::to_string(n);
            const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0) {
                if (errno != ENOENT) {
                    VIAM_SDK_LOG(warn) << "could not open " << path << ": " << std::strerror(errno);
                }
                continue;
            }
            v4l2_capability cap{};
            if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 && std::strcmp(reinterpret_cast<const char*>(cap.driver), TEGRA_VIDEO_DRIVER) == 0) {
                auto device_modes = enumerate_device(fd, std::to_string(sensor_index++));
                if (device_modes.empty()) {
                    VIAM_SDK_LOG(warn) << "no discrete sensor modes reported by " << path;
                }
                modes.insert(modes.end(), device_modes.begin(), device_modes.end());
            }
            close(fd);
        }
        return modes;
    }
};

// Canned modes so discovery returns something in test mode and so the service
// can be exercised end-to-end without a camera attached.
class stub_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        return {
            {"0", 1920, 1080, 30, "NV12"},
            {"0", 1280, 720, 60, "NV12"},
            {"0", 640, 480, 30, "NV12"},
        };
    }
};

std::string mode_name(const sensor_mode& m) {
    std::string name = "csi";
    if (!m.sensor_id.empty()) {
        name += "-" + m.sensor_id;
    }
    name += "-" + std::to_string(m.width_px) + "x" + std::to_string(m.height_px);
    if (m.frame_rate > 0) {
        name += "-" + std::to_string(m.frame_rate) + "fps";
    }
    return name;
}

}  // namespace

std::vector<ResourceConfig> build_configs(const std::vector<sensor_mode>& modes, device_type device) {
    const api_params api = get_api_params(device);
    const Model model{api.api_namespace, api.api_type, api.api_subtype};

    std::vector<ResourceConfig> configs;
    std::unordered_set<std::string> used_names;
    for (const auto& m : modes) {
        ProtoStruct attributes;
        attributes["video_path"] = m.sensor_id;
        // Numeric attrs are read back as doubles by the camera's set_attr.
        attributes["width_px"] = static_cast<double>(m.width_px);
        attributes["height_px"] = static_cast<double>(m.height_px);
        attributes["frame_rate"] = static_cast<double>(m.frame_rate);

        // Disambiguate names that collide (e.g. same size+rate, different format).
        std::string name = mode_name(m);
        std::string unique = name;
        for (int i = 1; used_names.count(unique) == 1; i++) {
            unique = name + "-" + std::to_string(i);
        }
        used_names.insert(unique);

        configs.emplace_back("camera", unique, API_NAMESPACE, attributes, "rdk:component:camera", model, log_level::info);
    }
    return configs;
}

std::unique_ptr<mode_enumerator> make_enumerator(device_type device) {
    switch (device.value) {
        case device_type::test:
            return std::make_unique<stub_enumerator>();
        case device_type::jetson:
            return std::make_unique<v4l2_enumerator>();
        case device_type::pi:
            // TODO(RSDK-12030): libcamera backend.
            VIAM_SDK_LOG(warn) << "CSI resolution discovery not yet implemented for " << device.name;
            return nullptr;
        default:
            return nullptr;
    }
}

CSIDiscovery::CSIDiscovery(std::string name, device_type device, std::unique_ptr<mode_enumerator> enumerator)
    : Discovery(std::move(name)), device_(device), enumerator_(std::move(enumerator)) {}

std::vector<ResourceConfig> CSIDiscovery::discover_resources(const ProtoStruct&) {
    if (enumerator_ == nullptr) {
        return {};
    }
    return build_configs(enumerator_->enumerate(), device_);
}

ProtoStruct CSIDiscovery::do_command(const ProtoStruct&) {
    VIAM_SDK_LOG(error) << "do_command not implemented";
    return ProtoStruct{};
}

ProtoStruct CSIDiscovery::get_status() {
    return ProtoStruct{};
}
