#include <gtest/gtest.h>

#include <unordered_set>

#include <viam/sdk/common/instance.hpp>

#include "../../constraints.h"
#include "../../discovery.cpp"
#include "../../utils.cpp"

using namespace viam::sdk;

static void ensure_runtime() {
    static Instance inst;
}

TEST(CSIDiscovery, BuildConfigsOnePerMode) {
    ensure_runtime();
    std::vector<csi::sensor_mode> modes = {
        {"0", 1920, 1080, 30, "NV12"},
        {"0", 1280, 720, 60, "NV12"},
    };

    const auto configs = csi::build_configs(modes);

    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].type(), "camera");
    EXPECT_EQ(configs[0].api().to_string(), "rdk:component:camera");
    EXPECT_EQ(configs[0].model().to_string(), std::string(csi::API_NAMESPACE) + ":" + csi::API_TYPE + ":" + csi::API_SUBTYPE);
}

TEST(CSIDiscovery, AttributesArePasteReady) {
    ensure_runtime();
    std::vector<csi::sensor_mode> modes = {{"1", 640, 480, 30, "NV12"}};

    const auto configs = csi::build_configs(modes);

    ASSERT_EQ(configs.size(), 1u);
    const auto& attrs = configs[0].attributes();
    EXPECT_EQ(*attrs.at("video_path").get<std::string>(), "1");
    EXPECT_EQ(*attrs.at("width_px").get<double>(), 640);
    EXPECT_EQ(*attrs.at("height_px").get<double>(), 480);
    EXPECT_EQ(*attrs.at("frame_rate").get<double>(), 30);
}

// Unknown rate (0) must be omitted, not emitted as 0 (the camera rejects
// frame_rate <= 0, so a 0 would make the discovered config fail to instantiate).
TEST(CSIDiscovery, UnknownFrameRateOmitted) {
    ensure_runtime();
    std::vector<csi::sensor_mode> modes = {{"0", 2592, 1944, 0, "SBGGR10"}};

    const auto configs = csi::build_configs(modes);

    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].attributes().count("frame_rate"), 0u);
}

TEST(CSIDiscovery, NamesAreUnique) {
    ensure_runtime();
    // Same size+rate, different format -> names collide without disambiguation.
    std::vector<csi::sensor_mode> modes = {
        {"0", 1920, 1080, 30, "NV12"},
        {"0", 1920, 1080, 30, "YUYV"},
    };

    const auto configs = csi::build_configs(modes);

    ASSERT_EQ(configs.size(), 2u);
    std::unordered_set<std::string> names{configs[0].name(), configs[1].name()};
    EXPECT_EQ(names.size(), 2u);
}

TEST(CSIDiscovery, EmptyModesEmptyConfigs) {
    ensure_runtime();
    EXPECT_TRUE(csi::build_configs({}).empty());
}

TEST(CSIDiscovery, StubEnumeratorForTestDevice) {
    ensure_runtime();
    const csi::device_type test_dev(csi::device_type::test, "test");
    csi::CSIDiscovery svc("disco", csi::make_enumerator(test_dev));
    EXPECT_FALSE(svc.discover_resources({}).empty());
}

TEST(CSIDiscovery, NullEnumeratorYieldsNoConfigs) {
    ensure_runtime();
    // jetson native backend not implemented yet -> null enumerator -> no configs.
    const csi::device_type jetson_dev(csi::device_type::jetson, "jetson");
    csi::CSIDiscovery svc("disco", csi::make_enumerator(jetson_dev));
    EXPECT_TRUE(svc.discover_resources({}).empty());
}
