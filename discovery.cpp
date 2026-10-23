#include "discovery.h"

#include <fcntl.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"

using namespace viam::sdk;

namespace csi {

namespace {

// Upper bound on the /dev/videoN and /dev/mediaN nodes scanned for sensors.
constexpr int MAX_DEVICE_NODES = 64;
// V4L2 driver name of the Jetson VI (CSI capture) nodes.
constexpr const char* TEGRA_VIDEO_DRIVER = "tegra-video";

// Highest frame rate per (width, height).
using rate_map = std::map<std::pair<uint32_t, uint32_t>, int>;

int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// Whole frames per second a mode can always deliver. The epsilon keeps an
// exact rate (e.g. 30) from flooring to one below.
int achievable_fps(double fps) {
    return static_cast<int>(std::floor(fps + 1e-6));
}

// A size can be listed under several raw formats; keep its highest rate.
void keep_max(rate_map& rates, uint32_t width, uint32_t height, int fps) {
    int& best = rates[{width, height}];
    best = std::max(best, fps);
}

std::vector<sensor_mode> to_modes(const rate_map& rates, const std::string& sensor_id) {
    std::vector<sensor_mode> modes;
    for (auto it = rates.rbegin(); it != rates.rend(); ++it) {  // largest first
        modes.push_back({sensor_id, static_cast<int>(it->first.first), static_cast<int>(it->first.second), it->second});
    }
    return modes;
}

// Highest discrete frame rate reported for a frame size, or 0 if none is.
int max_frame_rate(int fd, uint32_t pixel_format, uint32_t width, uint32_t height) {
    int best = 0;
    v4l2_frmivalenum ival{};
    ival.pixel_format = pixel_format;
    ival.width = width;
    ival.height = height;
    for (ival.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) == 0 && ival.type == V4L2_FRMIVAL_TYPE_DISCRETE; ival.index++) {
        if (ival.discrete.numerator > 0) {
            best = std::max(best, achievable_fps(static_cast<double>(ival.discrete.denominator) / ival.discrete.numerator));
        }
    }
    return best;
}

// Discrete frame sizes of a tegra-video capture node, each at its highest rate.
rate_map capture_node_rates(int fd) {
    rate_map rates;
    v4l2_fmtdesc fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++) {
        v4l2_frmsizeenum size{};
        size.pixel_format = fmt.pixelformat;
        // Sensor modes are discrete; a stepwise range is not a known-good mode.
        for (size.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0 && size.type == V4L2_FRMSIZE_TYPE_DISCRETE; size.index++) {
            const uint32_t width = size.discrete.width;
            const uint32_t height = size.discrete.height;
            keep_max(rates, width, height, max_frame_rate(fd, fmt.pixelformat, width, height));
        }
    }
    return rates;
}

// Jetson CSI sensors are exposed as tegra-video V4L2 capture nodes whose discrete
// frame sizes and intervals are the sensor modes Argus selects from, so they can
// be read with plain ioctls instead of linking libargus. The queries are
// read-only and succeed while nvarguscamerasrc is streaming from the sensor.
class v4l2_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        // Assumes Argus sensor-ids follow /dev/videoN order, so the k-th
        // tegra-video node is sensor-id k (verified on a single-sensor Orin Nano).
        // Other capture nodes, e.g. USB webcams, are skipped and take no id.
        std::vector<sensor_mode> modes;
        int sensor_index = 0;
        for (int n = 0; n < MAX_DEVICE_NODES; n++) {
            const std::string path = "/dev/video" + std::to_string(n);
            const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0 && errno == ENOENT) {
                continue;
            }
            v4l2_capability cap{};
            if (fd < 0 || xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
                // An unreadable node may be a sensor, which would shift every later sensor-id.
                const int err = errno;
                VIAM_SDK_LOG(warn) << "could not query " << path << ", skipping discovery: " << std::strerror(err);
                if (fd >= 0) {
                    close(fd);
                }
                return {};
            }
            if (std::strcmp(reinterpret_cast<const char*>(cap.driver), TEGRA_VIDEO_DRIVER) == 0) {
                const auto node_modes = to_modes(capture_node_rates(fd), std::to_string(sensor_index++));
                modes.insert(modes.end(), node_modes.begin(), node_modes.end());
            }
            close(fd);
        }
        return modes;
    }
};

// A camera sensor's subdev node and whether a camera holds its media device.
struct sensor_node {
    std::string subdev_path;
    bool in_use;
};

// libcamera lockf()s every media device of a camera it has acquired. Queried
// with an OFD lock, which conflicts with lockf() locks even from this process.
bool media_in_use(int fd) {
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_OFD_GETLK, &lock) != 0) {
        return true;  // Unknown: assume in use rather than risk disturbing a stream.
    }
    return lock.l_type != F_UNLCK;
}

// The first camera-sensor entity in the media graphs, in /dev/mediaN order.
std::optional<sensor_node> first_sensor() {
    for (int n = 0; n < MAX_DEVICE_NODES; n++) {
        const std::string path = "/dev/media" + std::to_string(n);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT) {
                const int err = errno;
                VIAM_SDK_LOG(warn) << "could not open " << path << ": " << std::strerror(err);
            }
            continue;
        }
        std::optional<sensor_node> found;
        media_entity_desc entity{};
        entity.id = MEDIA_ENT_ID_FLAG_NEXT;
        while (!found && xioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity) == 0) {
            if (entity.type == MEDIA_ENT_F_CAM_SENSOR) {
                // /sys/dev/char/<major>:<minor> links to the subdev's sysfs node,
                // which is named after its /dev node.
                std::error_code ec;
                const auto target = std::filesystem::read_symlink(
                    "/sys/dev/char/" + std::to_string(entity.dev.major) + ":" + std::to_string(entity.dev.minor), ec);
                if (!ec) {
                    found = sensor_node{"/dev/" + target.filename().string(), media_in_use(fd)};
                }
            }
            entity.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }
        close(fd);
        if (found) {
            return found;
        }
    }
    return std::nullopt;
}

// Highest frame rate of the sensor's active mode: pixel_rate divided by the
// line and frame lengths at minimum blanking, which is the limit libcamera
// derives. 0 if a control is missing.
int active_max_frame_rate(int fd, uint32_t width, uint32_t height) {
    v4l2_ext_control pixel_rate{};
    pixel_rate.id = V4L2_CID_PIXEL_RATE;
    v4l2_ext_controls ctrls{};
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &pixel_rate;
    v4l2_queryctrl hblank{};
    hblank.id = V4L2_CID_HBLANK;
    v4l2_queryctrl vblank{};
    vblank.id = V4L2_CID_VBLANK;
    if (xioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls) != 0 || xioctl(fd, VIDIOC_QUERYCTRL, &hblank) != 0 ||
        xioctl(fd, VIDIOC_QUERYCTRL, &vblank) != 0) {
        return 0;
    }
    const double frame_pixels = static_cast<double>(width + hblank.minimum) * (height + vblank.minimum);
    return frame_pixels > 0 ? achievable_fps(static_cast<double>(pixel_rate.value64) / frame_pixels) : 0;
}

// Discrete modes of a sensor subdev, each at its highest rate. A mode's timing
// controls are only valid while it is the active format, so each mode is set in
// turn and the original format restored. The first refused change ends the
// probe; only measured modes are kept.
rate_map subdev_rates(int fd, const std::string& path) {
    rate_map rates;
    v4l2_subdev_format original{};
    original.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (xioctl(fd, VIDIOC_SUBDEV_G_FMT, &original) != 0) {
        const int err = errno;
        VIAM_SDK_LOG(warn) << "could not read the format of " << path << ": " << std::strerror(err);
        return rates;
    }

    bool ok = true;
    bool changed = false;
    v4l2_subdev_mbus_code_enum code{};
    code.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    for (code.index = 0; ok && xioctl(fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &code) == 0; code.index++) {
        v4l2_subdev_frame_size_enum size{};
        size.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        size.code = code.code;
        for (size.index = 0; ok && xioctl(fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, &size) == 0; size.index++) {
            // Sensor modes are discrete; a size range is not a known-good mode.
            if (size.min_width != size.max_width || size.min_height != size.max_height) {
                continue;
            }
            v4l2_subdev_format fmt = original;
            fmt.format.code = code.code;
            fmt.format.width = size.max_width;
            fmt.format.height = size.max_height;
            if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) != 0) {
                const int err = errno;
                VIAM_SDK_LOG(warn) << "stopped probing " << path << ": " << std::strerror(err);
                ok = false;
                continue;
            }
            changed = true;
            if (fmt.format.width == size.max_width && fmt.format.height == size.max_height) {
                keep_max(rates, size.max_width, size.max_height, active_max_frame_rate(fd, size.max_width, size.max_height));
            }
        }
    }
    if (changed && xioctl(fd, VIDIOC_SUBDEV_S_FMT, &original) != 0) {
        const int err = errno;
        VIAM_SDK_LOG(warn) << "could not restore the format of " << path << ": " << std::strerror(err);
    }
    return rates;
}

// Raspberry Pi sensors are V4L2 subdevs in the camera's media graph, read
// directly rather than through libcamera, whose ABI changes every release. The
// probe runs once, at construction: main() builds this before serving, so none
// of this module's cameras is streaming yet. CSI sensors are not hot-pluggable,
// so the result is kept for the module's lifetime.
class subdev_enumerator : public mode_enumerator {
   public:
    subdev_enumerator() : modes_(probe()) {}

    std::vector<sensor_mode> enumerate() override {
        return modes_;
    }

   private:
    // The pi pipeline cannot select a camera (libcamerasrc opens libcamera's
    // first), so only the first sensor is probed, and without a sensor id since
    // video_path does not apply. Assumes libcamera's first camera is the first
    // sensor in /dev/mediaN order.
    static std::vector<sensor_mode> probe() {
        const auto sensor = first_sensor();
        if (!sensor) {
            return {};
        }
        if (sensor->in_use) {
            VIAM_SDK_LOG(warn) << sensor->subdev_path << " is in use; restart the module once it is free to discover its modes";
            return {};
        }
        const int fd = open(sensor->subdev_path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            const int err = errno;
            VIAM_SDK_LOG(warn) << "could not open " << sensor->subdev_path << ": " << std::strerror(err);
            return {};
        }
        auto modes = to_modes(subdev_rates(fd, sensor->subdev_path), "");
        close(fd);
        return modes;
    }

    const std::vector<sensor_mode> modes_;
};

// Canned modes so discovery returns something in test mode and so the service
// can be exercised end-to-end without a camera attached.
class stub_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        return {
            {"0", 1920, 1080, 30},
            {"0", 1280, 720, 60},
            {"0", 640, 480, 30},
        };
    }
};

std::string mode_name(const sensor_mode& m) {
    std::string name = "csi";
    if (!m.sensor_id.empty()) {
        name += "-" + m.sensor_id;
    }
    return name + "-" + std::to_string(m.width_px) + "x" + std::to_string(m.height_px) + "-" + std::to_string(m.frame_rate) + "fps";
}

}  // namespace

std::vector<ResourceConfig> build_configs(const std::vector<sensor_mode>& modes) {
    const Model model{API_NAMESPACE, API_TYPE, API_SUBTYPE};

    std::vector<ResourceConfig> configs;
    for (const auto& m : modes) {
        // The camera requires a positive frame_rate, so a mode without one is not paste-ready.
        if (m.frame_rate <= 0) {
            continue;
        }
        ProtoStruct attributes;
        // Numeric attrs are read back as doubles by the camera's set_attr.
        attributes["width_px"] = static_cast<double>(m.width_px);
        attributes["height_px"] = static_cast<double>(m.height_px);
        attributes["frame_rate"] = static_cast<double>(m.frame_rate);
        if (!m.sensor_id.empty()) {
            attributes["video_path"] = m.sensor_id;
        }
        configs.emplace_back("camera", mode_name(m), API_NAMESPACE, attributes, "rdk:component:camera", model, log_level::info);
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
            return std::make_unique<subdev_enumerator>();
        default:
            return nullptr;
    }
}

CSIDiscovery::CSIDiscovery(std::string name, std::shared_ptr<mode_enumerator> enumerator)
    : Discovery(std::move(name)), enumerator_(std::move(enumerator)) {}

std::vector<ResourceConfig> CSIDiscovery::discover_resources(const ProtoStruct&) {
    if (enumerator_ == nullptr) {
        return {};
    }
    return build_configs(enumerator_->enumerate());
}

ProtoStruct CSIDiscovery::do_command(const ProtoStruct&) {
    VIAM_RESOURCE_LOG(warn) << "do_command not implemented";
    return ProtoStruct{};
}

ProtoStruct CSIDiscovery::get_status() {
    return ProtoStruct{};
}

}  // namespace csi
