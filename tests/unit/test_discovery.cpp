#include <gtest/gtest.h>

#include <unordered_set>

#include <viam/sdk/common/instance.hpp>

#include "../../constraints.h"
#include "../../discovery.cpp"
#include "../../utils.cpp"

using namespace viam::sdk;
using namespace csi;

static void ensure_runtime() {
    static Instance inst;
}

static device_type pi_device() {
    return device_type(device_type::pi, "pi");
}

TEST(CSIDiscovery, BuildConfigsOnePerMode) {
    ensure_runtime();
    std::vector<sensor_mode> modes = {
        {"0", 1920, 1080, 30, "NV12"},
        {"0", 1280, 720, 60, "NV12"},
    };

    const auto configs = build_configs(modes);

    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].type(), "camera");
    EXPECT_EQ(configs[0].api().to_string(), "rdk:component:camera");
    // Discovered configs must use the module's (unified) camera model.
    EXPECT_EQ(configs[0].model().to_string(), std::string(API_NAMESPACE) + ":" + API_TYPE + ":" + API_SUBTYPE);
}

TEST(CSIDiscovery, AttributesArePasteReady) {
    ensure_runtime();
    std::vector<sensor_mode> modes = {{"1", 640, 480, 30, "NV12"}};

    const auto configs = build_configs(modes);

    ASSERT_EQ(configs.size(), 1u);
    const auto& attrs = configs[0].attributes();
    EXPECT_EQ(*attrs.at("video_path").get<std::string>(), "1");
    EXPECT_EQ(*attrs.at("width_px").get<double>(), 640);
    EXPECT_EQ(*attrs.at("height_px").get<double>(), 480);
    EXPECT_EQ(*attrs.at("frame_rate").get<double>(), 30);
}

TEST(CSIDiscovery, NamesAreUnique) {
    ensure_runtime();
    // Same size+rate, different format -> names would collide without disambiguation.
    std::vector<sensor_mode> modes = {
        {"0", 1920, 1080, 30, "NV12"},
        {"0", 1920, 1080, 30, "YUYV"},
    };

    const auto configs = build_configs(modes);

    ASSERT_EQ(configs.size(), 2u);
    std::unordered_set<std::string> names{configs[0].name(), configs[1].name()};
    EXPECT_EQ(names.size(), 2u);
}

TEST(CSIDiscovery, EmptyModesEmptyConfigs) {
    ensure_runtime();
    EXPECT_TRUE(build_configs({}).empty());
}

TEST(CSIDiscovery, StubEnumeratorForTestDevice) {
    ensure_runtime();
    CSIDiscovery svc("disco", make_enumerator(device_type(device_type::test, "test")));
    EXPECT_FALSE(svc.discover_resources({}).empty());
}

TEST(CSIDiscovery, NullEnumeratorYieldsNoConfigs) {
    ensure_runtime();
    // Unknown devices have no backend -> null enumerator.
    CSIDiscovery svc("disco", make_enumerator(device_type()));
    EXPECT_TRUE(svc.discover_resources({}).empty());
}

TEST(CSIDiscovery, JetsonHasNativeEnumerator) {
    ensure_runtime();
    auto enumerator = make_enumerator(device_type(device_type::jetson, "jetson"));
    ASSERT_NE(enumerator, nullptr);
    // Off-device there are no tegra-video nodes; this checks the scan is safe.
    EXPECT_NO_THROW(enumerator->enumerate());
}

TEST(CSIDiscovery, PiHasNativeEnumerator) {
    ensure_runtime();
    auto enumerator = make_enumerator(pi_device());
    ASSERT_NE(enumerator, nullptr);
    // Off-device there are no camera-sensor entities; this checks the probe is safe.
    EXPECT_NO_THROW(enumerator->enumerate());
}
