#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>

#include "utils.h"

namespace csi {

// A single discrete capture mode a CSI sensor supports.
struct sensor_mode {
    std::string sensor_id;  // the camera's video_path; empty where it does not apply (pi)
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;  // highest rate the mode can always deliver
};

// Enumerates the sensor modes available on a device. The concrete backend is
// platform-specific (V4L2 sensor subdevs on pi, V4L2 capture nodes on jetson).
struct mode_enumerator {
    virtual ~mode_enumerator() = default;
    virtual std::vector<sensor_mode> enumerate() = 0;
};

// Frame rate per (width, height).
using rate_map = std::map<std::pair<uint32_t, uint32_t>, int>;

// A size can be listed under several raw formats, each with its own rate, and
// the pipeline rather than discovery picks the format. Keep the lowest measured
// rate, the one that is reachable whichever format is picked. 0 (not measured)
// is skipped.
void keep_lowest(rate_map& rates, uint32_t width, uint32_t height, int fps);

// Whole frames per second of a V4L2 frame interval, or 0 if the interval is invalid.
int interval_fps(uint32_t numerator, uint32_t denominator);

// Build one camera ResourceConfig per sensor mode with a known frame rate. Pure
// (no hardware access) so it is unit-tested directly. Each config is
// paste-ready and carries the camera model, so a discovered config is a
// known-good resolution.
std::vector<viam::sdk::ResourceConfig> build_configs(const std::vector<sensor_mode>& modes);

// Returns the enumerator backend for a device, or nullptr if none is available.
// The pi backend probes the sensor once, on construction, so build it before
// any camera starts streaming and share it between discovery resources.
std::unique_ptr<mode_enumerator> make_enumerator(device_type device);

class CSIDiscovery : public viam::sdk::Discovery {
   public:
    CSIDiscovery(std::string name, std::shared_ptr<mode_enumerator> enumerator);

    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    viam::sdk::ProtoStruct get_status() override;

   private:
    std::shared_ptr<mode_enumerator> enumerator_;
};

}  // namespace csi
