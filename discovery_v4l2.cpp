// Jetson discovery backend: read the CSI sensor modes straight from the tegra
// V4L2 nodes via ioctl. Argus can't be build-linked (no L4T in the focal image)
// and has no listing CLI, but the sensor's modes are exposed on /dev/video*
// through the standard V4L2 uAPI -- which needs only <linux/videodev2.h> (always
// present), so this requires no external tool and nothing for first_run to add.

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <string>

#include <viam/sdk/log/logging.hpp>

#include "discovery.h"

namespace csi {

std::string v4l2_fourcc_to_string(std::uint32_t fourcc) {
    const char chars[4] = {
        static_cast<char>(fourcc & 0xFF),
        static_cast<char>((fourcc >> 8) & 0xFF),
        static_cast<char>((fourcc >> 16) & 0xFF),
        static_cast<char>((fourcc >> 24) & 0xFF),
    };
    std::string out;
    for (const char c : chars) {
        if (std::isprint(static_cast<unsigned char>(c))) {
            out += c;
        }
    }
    return out;
}

int v4l2_interval_to_fps(std::uint32_t numerator, std::uint32_t denominator) {
    // A V4L2 frame interval is seconds-per-frame (numerator/denominator), so fps
    // is its reciprocal. Floor, not round: never advertise above the achievable
    // rate (epsilon guards float error so an exact 30 doesn't floor to 29).
    if (numerator == 0) {
        return 0;
    }
    return static_cast<int>(std::floor(static_cast<double>(denominator) / static_cast<double>(numerator) + 1e-3));
}

namespace {

int xioctl(int fd, unsigned long request, void* arg) {
    int r = 0;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

// Highest fps a (format, size) supports, via VIDIOC_ENUM_FRAMEINTERVALS.
int max_fps(int fd, std::uint32_t pixelformat, std::uint32_t width, std::uint32_t height) {
    int best = 0;
    v4l2_frmivalenum fi{};
    fi.pixel_format = pixelformat;
    fi.width = width;
    fi.height = height;
    for (fi.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; fi.index++) {
        if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            best = std::max(best, v4l2_interval_to_fps(fi.discrete.numerator, fi.discrete.denominator));
        } else {
            // Stepwise/continuous: the shortest interval (min) is the fastest fps.
            best = std::max(best, v4l2_interval_to_fps(fi.stepwise.min.numerator, fi.stepwise.min.denominator));
            break;
        }
    }
    return best;
}

void enumerate_type(int fd, std::uint32_t buf_type, const std::string& sensor_id, std::vector<sensor_mode>& out) {
    v4l2_fmtdesc fmt{};
    fmt.type = buf_type;
    for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++) {
        const std::string format = v4l2_fourcc_to_string(fmt.pixelformat);
        v4l2_frmsizeenum fs{};
        fs.pixel_format = fmt.pixelformat;
        for (fs.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; fs.index++) {
            // Only discrete sizes are concrete modes; skip stepwise/continuous.
            if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                break;
            }
            sensor_mode m;
            m.sensor_id = sensor_id;
            m.width_px = static_cast<int>(fs.discrete.width);
            m.height_px = static_cast<int>(fs.discrete.height);
            m.format = format;
            m.frame_rate = max_fps(fd, fmt.pixelformat, fs.discrete.width, fs.discrete.height);
            out.push_back(m);
        }
    }
}

std::vector<sensor_mode> enumerate_device(const std::string& dev, const std::string& sensor_id) {
    std::vector<sensor_mode> modes;
    const int fd = open(dev.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return modes;
    }
    // Tegra sensor nodes may be single- or multi-planar capture; try both.
    enumerate_type(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, sensor_id, modes);
    enumerate_type(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, sensor_id, modes);
    close(fd);
    return modes;
}

constexpr int kMaxVideoNodes = 8;

class v4l2_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        std::vector<sensor_mode> all;
        for (int i = 0; i < kMaxVideoNodes; i++) {
            const std::string dev = "/dev/video" + std::to_string(i);
            if (access(dev.c_str(), F_OK) != 0) {
                continue;
            }
            auto modes = enumerate_device(dev, std::to_string(i));
            all.insert(all.end(), modes.begin(), modes.end());
        }
        if (all.empty()) {
            VIAM_SDK_LOG(warn) << "no camera modes found on /dev/video* via V4L2";
        }
        return all;
    }
};

}  // namespace

std::unique_ptr<mode_enumerator> make_v4l2_enumerator() {
    return std::make_unique<v4l2_enumerator>();
}

}  // namespace csi
