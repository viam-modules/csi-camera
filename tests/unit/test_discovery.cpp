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

// Real rpicam-hello --list-cameras shape: a header line, then mode lines whose
// format token carries across continuation lines, each with a trailing crop.
// Real `rpicam-hello --list-cameras` capture from an OV5647 on a Pi 5.
static const char* kRpicamOutput =
    "Available cameras\n"
    "-----------------\n"
    "0 : ov5647 [2592x1944 10-bit GBRG] (/base/axi/pcie@1000120000/rp1/i2c@88000/ov5647@36)\n"
    "    Modes: 'SGBRG10_CSI2P' : 640x480 [62.50 fps - (16, 0)/2560x1920 crop]\n"
    "                             1296x972 [46.34 fps - (0, 0)/2592x1944 crop]\n"
    "                             1920x1080 [32.81 fps - (348, 434)/1928x1080 crop]\n"
    "                             2592x1944 [15.63 fps - (0, 0)/2592x1944 crop]\n";

TEST(CSIDiscovery, ParsesRpicamModes) {
    const auto modes = csi::parse_rpicam_list(kRpicamOutput);

    // Four real modes; the header's resolution and the crop sizes are not modes.
    ASSERT_EQ(modes.size(), 4u);
    EXPECT_EQ(modes[0].sensor_id, "0");
    EXPECT_EQ(modes[0].width_px, 640);
    EXPECT_EQ(modes[0].height_px, 480);
    EXPECT_EQ(modes[0].frame_rate, 63);  // 62.50 rounded
    EXPECT_EQ(modes[0].format, "SGBRG10_CSI2P");
    // Format persists onto a continuation line.
    EXPECT_EQ(modes[3].width_px, 2592);
    EXPECT_EQ(modes[3].frame_rate, 16);  // 15.63 rounded
    EXPECT_EQ(modes[3].format, "SGBRG10_CSI2P");
}

TEST(CSIDiscovery, ParsesNoCameras) {
    EXPECT_TRUE(csi::parse_rpicam_list("No cameras available!\n").empty());
    EXPECT_TRUE(csi::parse_rpicam_list("").empty());
}

TEST(CSIDiscovery, V4l2FourccToString) {
    // 'RG10' packed little-endian, as V4L2 reports pixelformat.
    const std::uint32_t rg10 = 'R' | ('G' << 8) | ('1' << 16) | ('0' << 24);
    EXPECT_EQ(csi::v4l2_fourcc_to_string(rg10), "RG10");
}

TEST(CSIDiscovery, V4l2IntervalToFps) {
    // Interval is seconds-per-frame; fps is its reciprocal, rounded.
    EXPECT_EQ(csi::v4l2_interval_to_fps(1, 30), 30);
    EXPECT_EQ(csi::v4l2_interval_to_fps(1001, 30000), 30);  // 29.97 -> 30
    EXPECT_EQ(csi::v4l2_interval_to_fps(0, 30), 0);         // zero numerator guarded
}

TEST(CSIDiscovery, StubEnumeratorForTestDevice) {
    ensure_runtime();
    const csi::device_type test_dev(csi::device_type::test, "test");
    csi::CSIDiscovery svc("disco", csi::make_enumerator(test_dev));
    EXPECT_FALSE(svc.discover_resources({}).empty());
}

TEST(CSIDiscovery, NullEnumeratorYieldsNoConfigs) {
    ensure_runtime();
    // An unknown device has no backend -> null enumerator -> no configs.
    const csi::device_type unknown_dev(csi::device_type::unknown, "unknown");
    csi::CSIDiscovery svc("disco", csi::make_enumerator(unknown_dev));
    EXPECT_TRUE(svc.discover_resources({}).empty());
}
