#include "discovery.h"

#include <unordered_set>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"

namespace csi {

using namespace viam::sdk;

namespace {

// Canned modes so discovery returns something in test mode and the service can
// be exercised without a camera attached.
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
        // Omit an unknown rate (0) rather than emit an invalid one: the camera
        // rejects frame_rate <= 0, and absent means it uses its default.
        if (m.frame_rate > 0) {
            attributes["frame_rate"] = static_cast<double>(m.frame_rate);
        }

        std::string base = mode_name(m);
        std::string name = base;
        for (int i = 1; used_names.count(name) == 1; i++) {
            name = base + "-" + std::to_string(i);
        }
        used_names.insert(name);

        configs.emplace_back("camera", name, API_NAMESPACE, attributes, "rdk:component:camera", model, log_level::info);
    }
    return configs;
}

std::unique_ptr<mode_enumerator> make_enumerator(device_type device) {
    switch (device.value) {
        case device_type::test:
            return std::make_unique<stub_enumerator>();
        case device_type::pi:
            return make_rpicam_enumerator();
        case device_type::jetson:
            // TODO(RSDK-12030): Argus backend (CameraProvider::getAllSensorModes).
            VIAM_SDK_LOG(warn) << "Argus discovery backend not yet implemented; returning no resolutions";
            return nullptr;
        default:
            return nullptr;
    }
}

CSIDiscovery::CSIDiscovery(std::string name, std::unique_ptr<mode_enumerator> enumerator)
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
