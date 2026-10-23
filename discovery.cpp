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
#include <mutex>
#include <set>
#include <unordered_set>
#include <utility>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"

using namespace viam::sdk;

namespace csi {

namespace {

// Upper bound on the /dev/videoN nodes scanned for CSI sensors.
constexpr int MAX_VIDEO_DEVICES = 64;
// Upper bound on the /dev/mediaN nodes scanned for camera sensors.
constexpr int MAX_MEDIA_DEVICES = 16;
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

// A camera sensor's subdev node and whether a camera currently holds its media graph.
struct sensor_node {
    std::string subdev_path;
    bool in_use;
};

// libcamera lockf()s every media device of a camera it has acquired, so a lock
// means the sensor may be streaming. Queried with an OFD lock, which conflicts
// with lockf() locks even when they are held by this process's own camera.
bool media_in_use(int fd) {
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_OFD_GETLK, &lock) != 0) {
        return true;  // Unknown: assume in use rather than risk disturbing a stream.
    }
    return lock.l_type != F_UNLCK;
}

// The camera-sensor entities in the media graphs, in /dev/mediaN then entity order.
std::vector<sensor_node> sensor_nodes() {
    std::vector<sensor_node> nodes;
    for (int n = 0; n < MAX_MEDIA_DEVICES; n++) {
        const int fd = open(("/dev/media" + std::to_string(n)).c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        const bool in_use = media_in_use(fd);
        media_entity_desc entity{};
        entity.id = MEDIA_ENT_ID_FLAG_NEXT;
        while (xioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity) == 0) {
            if (entity.type == MEDIA_ENT_F_CAM_SENSOR) {
                // /sys/dev/char/<major>:<minor> links to the subdev's sysfs node,
                // which is named after its /dev node.
                std::error_code ec;
                const auto target = std::filesystem::read_symlink(
                    "/sys/dev/char/" + std::to_string(entity.dev.major) + ":" + std::to_string(entity.dev.minor), ec);
                if (!ec) {
                    nodes.push_back({"/dev/" + target.filename().string(), in_use});
                }
            }
            entity.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }
        close(fd);
    }
    return nodes;
}

// Highest frame rate of the sensor's active mode: pixel_rate divided by the
// line and frame lengths at minimum blanking, which is the limit libcamera
// derives. Floored so the rate is always achievable. 0 if a control is missing.
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
    if (frame_pixels <= 0) {
        return 0;
    }
    // The epsilon keeps an exact rate (e.g. 30) from flooring to one below.
    return static_cast<int>(std::floor(static_cast<double>(pixel_rate.value64) / frame_pixels + 1e-6));
}

// Modes of one sensor, and whether every one of them got its frame rate.
struct probe_result {
    std::vector<sensor_mode> modes;
    bool complete = true;
};

// Every discrete mode of one sensor subdev at its max frame rate. A mode's
// timing controls are only valid while it is the active format, so each mode
// is set in turn and the original format restored. That would disturb a
// streaming camera (some drivers accept it mid-stream), so while the sensor is
// in use only the active mode's rate is read, and the other modes are left out
// rather than reported with a guessed rate.
probe_result probe_sensor(int fd, const sensor_node& node, const std::string& sensor_id) {
    const std::string& path = node.subdev_path;
    v4l2_subdev_format original{};
    original.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (xioctl(fd, VIDIOC_SUBDEV_G_FMT, &original) != 0) {
        VIAM_SDK_LOG(warn) << "could not read the active format of " << path << ": " << std::strerror(errno);
        return {{}, false};
    }

    probe_result result;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    bool busy = node.in_use;
    bool changed_format = false;
    int skipped = 0;
    v4l2_subdev_mbus_code_enum code{};
    code.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    for (code.index = 0; xioctl(fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &code) == 0; code.index++) {
        v4l2_subdev_frame_size_enum size{};
        size.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        size.code = code.code;
        for (size.index = 0; xioctl(fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, &size) == 0; size.index++) {
            // Sensor modes are discrete; a size range is not a known-good mode.
            if (size.min_width != size.max_width || size.min_height != size.max_height) {
                continue;
            }
            const uint32_t width = size.max_width;
            const uint32_t height = size.max_height;
            if (!seen.insert({width, height}).second) {
                continue;
            }
            int frame_rate = 0;
            if (!busy) {
                v4l2_subdev_format fmt = original;
                fmt.format.code = code.code;
                fmt.format.width = width;
                fmt.format.height = height;
                if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) == 0) {
                    changed_format = true;
                    if (fmt.format.width == width && fmt.format.height == height) {
                        frame_rate = active_max_frame_rate(fd, width, height);
                    }
                } else if (errno == EBUSY) {
                    busy = true;
                }
            }
            if (busy && width == original.format.width && height == original.format.height) {
                frame_rate = active_max_frame_rate(fd, width, height);
            }
            if (frame_rate > 0) {
                result.modes.push_back({sensor_id, static_cast<int>(width), static_cast<int>(height), frame_rate, ""});
            } else {
                skipped++;
            }
        }
    }
    if (changed_format && xioctl(fd, VIDIOC_SUBDEV_S_FMT, &original) != 0) {
        VIAM_SDK_LOG(warn) << "could not restore the active format of " << path << ": " << std::strerror(errno);
    }
    if (skipped > 0) {
        result.complete = false;
        VIAM_SDK_LOG(warn) << "left out " << skipped << " mode(s) of " << path << " with unknown frame rate"
                           << (busy ? " (camera in use)" : "");
    }
    return result;
}

// Raspberry Pi sensors are V4L2 subdevs in the camera's media graph, read
// directly rather than through libcamera, whose ABI changes every release.
// Modes are probed once at construction: main() builds the enumerator before
// serving, so no camera of this module is streaming yet and every mode's rate
// can be read. CSI sensors are not hot-pluggable, so a complete probe stays
// valid; an incomplete one (e.g. another process held the camera) is retried
// on each call until it completes.
class subdev_enumerator : public mode_enumerator {
   public:
    subdev_enumerator() : cache_(probe()) {}

    std::vector<sensor_mode> enumerate() override {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!cache_.complete) {
            probe_result fresh = probe();
            if (fresh.complete || fresh.modes.size() > cache_.modes.size()) {
                cache_ = std::move(fresh);
            }
        }
        return cache_.modes;
    }

   private:
    // The pi camera pipeline does not select a sensor (libcamerasrc opens the
    // first), so sensor_id is only the sensor's ordinal.
    static probe_result probe() {
        probe_result result;
        int sensor_index = 0;
        for (const auto& node : sensor_nodes()) {
            const std::string sensor_id = std::to_string(sensor_index++);
            const int fd = open(node.subdev_path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0) {
                VIAM_SDK_LOG(warn) << "could not open " << node.subdev_path << ": " << std::strerror(errno);
                result.complete = false;
                continue;
            }
            probe_result sensor = probe_sensor(fd, node, sensor_id);
            close(fd);
            result.modes.insert(result.modes.end(), sensor.modes.begin(), sensor.modes.end());
            result.complete = result.complete && sensor.complete;
        }
        return result;
    }

    std::mutex mutex_;
    probe_result cache_;
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

std::vector<ResourceConfig> build_configs(const std::vector<sensor_mode>& modes) {
    const Model model{API_NAMESPACE, API_TYPE, API_SUBTYPE};

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
    VIAM_SDK_LOG(error) << "do_command not implemented";
    return ProtoStruct{};
}

ProtoStruct CSIDiscovery::get_status() {
    return ProtoStruct{};
}

}  // namespace csi
