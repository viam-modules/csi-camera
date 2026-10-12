#include "discovery.h"

#include <unordered_set>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"

using namespace viam::sdk;

namespace {

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
        case device_type::pi:
            // TODO(RSDK-12030): native backends (libcamera on pi, Argus on jetson).
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
