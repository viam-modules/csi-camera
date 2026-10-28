// Pi discovery backend: shell out to `rpicam-hello --list-cameras` and parse its
// output. The RPi tool uses the host's exact libcamera, so this needs no
// build-time libcamera (focal has none) and no linking — just a subprocess.

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <regex>
#include <sstream>

#include <viam/sdk/log/logging.hpp>

#include "discovery.h"

namespace csi {

namespace {

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

// Output shape (rpicam-apps):
//   0 : ov5647 [2592x1944 10-bit RGGB] (/base/.../ov5647@36)
//       Modes: 'SRGGB10_CSI2P' : 640x480 [58.92 fps - (...) crop]
//                                1296x972 [43.25 fps - (...) crop]
// A header line sets the current camera; each mode line carries "WxH [F fps";
// the format token ('…') persists across the continuation lines under a header.
std::vector<sensor_mode> parse_rpicam_list(const std::string& output) {
    static const std::regex header_re(R"(^\s*(\d+)\s*:\s*\S+.*\((.+)\)\s*$)");
    static const std::regex mode_re(R"((\d+)x(\d+)\s*\[\s*([0-9.]+)\s*fps)");
    static const std::regex format_re(R"('([^']+)')");

    std::vector<sensor_mode> modes;
    std::istringstream stream(output);
    std::string line;
    std::string sensor_id;
    std::string current_format;
    while (std::getline(stream, line)) {
        std::smatch m;
        if (std::regex_search(line, m, header_re)) {
            sensor_id = m[1].str();  // camera index
            current_format.clear();
            continue;
        }
        if (std::regex_search(line, m, format_re)) {
            current_format = m[1].str();
        }
        // regex_search returns the first match, which is the mode resolution; a
        // trailing crop like "/2560x1920" is not followed by "[ fps" so is ignored.
        if (std::regex_search(line, m, mode_re)) {
            sensor_mode sm;
            sm.sensor_id = sensor_id;
            sm.width_px = std::stoi(m[1].str());
            sm.height_px = std::stoi(m[2].str());
            // Floor, not round: advertising above the sensor's real max makes the
            // camera request an unachievable rate and the pipeline stalls. The
            // epsilon guards float error so an exact 30.00 doesn't floor to 29.
            sm.frame_rate = static_cast<int>(std::floor(std::stod(m[3].str()) + 1e-3));
            sm.format = current_format;
            modes.push_back(sm);
        }
    }
    return modes;
}

namespace {

class rpicam_enumerator : public mode_enumerator {
   public:
    std::vector<sensor_mode> enumerate() override {
        // rpicam-hello is the current name; libcamera-hello is the older alias.
        for (const char* tool : {"rpicam-hello", "libcamera-hello"}) {
            auto modes = parse_rpicam_list(run_command(std::string(tool) + " --list-cameras 2>/dev/null"));
            if (!modes.empty()) {
                return modes;
            }
        }
        VIAM_SDK_LOG(warn) << "no camera modes parsed from rpicam-hello --list-cameras";
        return {};
    }
};

}  // namespace

std::unique_ptr<mode_enumerator> make_rpicam_enumerator() {
    return std::make_unique<rpicam_enumerator>();
}

}  // namespace csi
