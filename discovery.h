#pragma once

#include <memory>
#include <string>
#include <vector>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>

#include "utils.h"

namespace csi {

// One discrete capture mode a CSI sensor supports. frame_rate 0 means unknown
// (the emitted config then omits it and the camera uses its default).
struct sensor_mode {
    std::string sensor_id;
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;
    std::string format;  // optional; empty when the backend does not report one
};

// Enumerates a device's supported sensor modes. The concrete backend is
// platform-specific (rpicam-hello on pi, Argus on jetson) and the seam the native
// query sits behind.
struct mode_enumerator {
    virtual ~mode_enumerator() = default;
    virtual std::vector<sensor_mode> enumerate() = 0;
};

// Build one paste-ready camera ResourceConfig per sensor mode. Pure (no hardware
// access) so it is unit-tested directly; every config carries the viam:camera:csi
// model, so a discovered config is a known-good resolution.
std::vector<viam::sdk::ResourceConfig> build_configs(const std::vector<sensor_mode>& modes);

// Backend for a device, or nullptr if none is available for it.
std::unique_ptr<mode_enumerator> make_enumerator(device_type device);

// Parse `rpicam-hello --list-cameras` output into modes. Pure, so unit-tested
// directly; defined in discovery_rpicam.cpp.
std::vector<sensor_mode> parse_rpicam_list(const std::string& output);

// Pi backend: shells out to rpicam-hello. Defined in discovery_rpicam.cpp.
std::unique_ptr<mode_enumerator> make_rpicam_enumerator();

// Parse `v4l2-ctl --list-formats-ext` output into modes for one sensor index.
// Pure, so unit-tested directly; defined in discovery_v4l2.cpp.
std::vector<sensor_mode> parse_v4l2_formats(const std::string& output, const std::string& sensor_id);

// Jetson backend: shells out to v4l2-ctl over /dev/video* (the tegra V4L2 nodes
// expose the sensor modes Argus would report). Defined in discovery_v4l2.cpp.
std::unique_ptr<mode_enumerator> make_v4l2_enumerator();

class CSIDiscovery : public viam::sdk::Discovery {
   public:
    CSIDiscovery(std::string name, std::unique_ptr<mode_enumerator> enumerator);

    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    viam::sdk::ProtoStruct get_status() override;

   private:
    std::unique_ptr<mode_enumerator> enumerator_;
};

}  // namespace csi
