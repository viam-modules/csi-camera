// Jetson discovery backend: shell out to `v4l2-ctl --list-formats-ext` over the
// tegra V4L2 nodes. Argus can't be build-linked (no L4T in the focal build image)
// and has no listing CLI, but the CSI sensor's modes are exposed on /dev/video*,
// so this needs no Argus headers or linking -- just a subprocess, like the Pi path.

#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include <viam/sdk/log/logging.hpp>

#include "discovery.h"

namespace csi {

namespace {

// Jetson exposes a handful of /dev/videoN; scan a small range for sensor nodes.
constexpr int kMaxVideoNodes = 8;

std::string run_command(const std::string& cmd) {
    std::string out;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return out;
    }
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
        out += buf.data();
    }
    return out;
}

}  // namespace

// Output shape (v4l2-ctl --list-formats-ext):
//   [0]: 'RG10' (10-bit Bayer RGRG/GBGB)
//       Size: Discrete 1920x1080
//           Interval: Discrete 0.017s (59.999 fps)
// The format token persists across its sizes; a size may list several intervals,
// so the mode's rate is the fastest (highest fps) of them.
std::vector<sensor_mode> parse_v4l2_formats(const std::string& output, const std::string& sensor_id) {
    static const std::regex format_re(R"(\[\d+\]:\s*'([^']+)')");
    static const std::regex size_re(R"(Size:\s*Discrete\s*(\d+)x(\d+))");
    static const std::regex fps_re(R"(\(([0-9.]+)\s*fps\))");

    std::vector<sensor_mode> modes;
    std::istringstream stream(output);
    std::string line;
    std::string current_format;
    while (std::getline(stream, line)) {
        std::smatch m;
        if (std::regex_search(line, m, format_re)) {
            current_format = m[1].str();
            continue;
        }
        if (std::regex_search(line, m, size_re)) {
            sensor_mode sm;
            sm.sensor_id = sensor_id;
            sm.width_px = std::stoi(m[1].str());
            sm.height_px = std::stoi(m[2].str());
            sm.format = current_format;
            modes.push_back(sm);
            continue;
        }
        if (std::regex_search(line, m, fps_re) && !modes.empty()) {
            const int fps = static_cast<int>(std::lround(std::stod(m[1].str())));
            modes.back().frame_rate = std::max(modes.back().frame_rate, fps);
        }
    }
    return modes;
}

namespace {

class v4l2_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        std::vector<sensor_mode> all;
        for (int i = 0; i < kMaxVideoNodes; i++) {
            const std::string dev = "/dev/video" + std::to_string(i);
            if (access(dev.c_str(), F_OK) != 0) {
                continue;
            }
            auto modes = parse_v4l2_formats(run_command("v4l2-ctl --list-formats-ext -d " + dev + " 2>/dev/null"), std::to_string(i));
            all.insert(all.end(), modes.begin(), modes.end());
        }
        if (all.empty()) {
            VIAM_SDK_LOG(warn) << "no camera modes parsed from v4l2-ctl --list-formats-ext";
        }
        return all;
    }
};

}  // namespace

std::unique_ptr<mode_enumerator> make_v4l2_enumerator() {
    return std::make_unique<v4l2_enumerator>();
}

}  // namespace csi
