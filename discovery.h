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
// platform-native (libcamera on pi, Argus on jetson) and the seam the native
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

// Defined in discovery_libcamera.cpp, compiled only with VIAM_CSI_WITH_LIBCAMERA.
std::unique_ptr<mode_enumerator> make_libcamera_enumerator();

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
