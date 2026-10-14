#pragma once

#include <memory>
#include <string>
#include <vector>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>

#include "utils.h"

// A single discrete capture mode a CSI sensor supports.
struct sensor_mode {
    std::string sensor_id;  // maps to the camera's video_path attribute
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;
    std::string format;  // optional; empty when the backend does not report one
};

// Enumerates the sensor modes available on a device. The concrete backend is
// platform-specific (libcamera on pi, Argus on jetson) and selected at build
// time, so this interface is the seam the native query lives behind.
struct mode_enumerator {
    virtual ~mode_enumerator() = default;
    virtual std::vector<sensor_mode> enumerate() = 0;
};

// Build one camera ResourceConfig per sensor mode. Pure (no hardware access) so
// it is unit-tested directly. Each config is paste-ready and carries the
// platform's camera model, so a discovered config is a known-good resolution.
std::vector<viam::sdk::ResourceConfig> build_configs(const std::vector<sensor_mode>& modes, device_type device);

// Returns the enumerator backend for a device, or nullptr if none is available.
std::unique_ptr<mode_enumerator> make_enumerator(device_type device);

class CSIDiscovery : public viam::sdk::Discovery {
   public:
    CSIDiscovery(std::string name, device_type device, std::unique_ptr<mode_enumerator> enumerator);

    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    viam::sdk::ProtoStruct get_status() override;

   private:
    device_type device_;
    std::unique_ptr<mode_enumerator> enumerator_;
};
