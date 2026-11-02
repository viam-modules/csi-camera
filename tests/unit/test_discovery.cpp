#include <gtest/gtest.h>

#include <viam/sdk/common/instance.hpp>

#include "../../constraints.h"
#include "../../discovery.cpp"
#include "../../utils.cpp"

using namespace viam::sdk;
using namespace csi;

static void ensure_runtime() {
    static Instance inst;
}

TEST(CSIDiscovery, BuildConfigsOnePerMode) {
    ensure_runtime();
    std::vector<sensor_mode> modes = {
        {"0", 1920, 1080, 30},
        {"0", 1280, 720, 60},
    };

    const auto configs = build_configs(modes);

    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].name(), "csi-0-1920x1080-30fps");
    EXPECT_EQ(configs[0].type(), "camera");
    EXPECT_EQ(configs[0].api().to_string(), "rdk:component:camera");
    // Discovered configs must use the module's (unified) camera model.
    EXPECT_EQ(configs[0].model().to_string(), std::string(API_NAMESPACE) + ":" + API_TYPE + ":" + API_SUBTYPE);
}

TEST(CSIDiscovery, AttributesArePasteReady) {
    ensure_runtime();
    std::vector<sensor_mode> modes = {{"1", 640, 480, 30}};

    const auto configs = build_configs(modes);

    ASSERT_EQ(configs.size(), 1u);
    const auto& attrs = configs[0].attributes();
    EXPECT_EQ(*attrs.at("video_path").get<std::string>(), "1");
    EXPECT_EQ(*attrs.at("width_px").get<double>(), 640);
    EXPECT_EQ(*attrs.at("height_px").get<double>(), 480);
    EXPECT_EQ(*attrs.at("frame_rate").get<double>(), 30);
}

TEST(CSIDiscovery, NoSensorIdOmitsVideoPath) {
    ensure_runtime();
    // pi modes carry no sensor id: the pi pipeline ignores video_path.
    const auto configs = build_configs({{"", 1920, 1080, 32}});

    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].name(), "csi-1920x1080-32fps");
    EXPECT_EQ(configs[0].attributes().count("video_path"), 0u);
}

TEST(CSIDiscovery, SkipsModesWithoutFrameRate) {
    ensure_runtime();
    // The camera rejects frame_rate 0, so such a config would not be paste-ready.
    const auto configs = build_configs({{"0", 1920, 1080, 0}, {"0", 1280, 720, 60}});

    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].name(), "csi-0-1280x720-60fps");
}

TEST(CSIDiscovery, SizeKeepsLowestMeasuredRate) {
    rate_map rates;
    keep_lowest(rates, 640, 480, 60);  // e.g. an 8-bit format
    keep_lowest(rates, 640, 480, 30);  // the 10-bit format the pipeline may pick
    keep_lowest(rates, 640, 480, 0);   // not measured
    keep_lowest(rates, 1920, 1080, 0);

    ASSERT_EQ(rates.size(), 1u);
    EXPECT_EQ(rates.at({640, 480}), 30);
}

TEST(CSIDiscovery, IntervalFpsFloors) {
    EXPECT_EQ(interval_fps({1, 30}), 30);        // an exact rate stays put
    EXPECT_EQ(interval_fps({1001, 30000}), 29);  // 29.97 must not round up past the sensor's max
    EXPECT_EQ(interval_fps({0, 30}), 0);         // invalid interval
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

// The hardware backends are not exercised here: run on a Pi or Jetson, they
// would query (and on pi, change the format of) the real sensor. The pi
// enumerator probes on construction, so it is not built at all.
TEST(CSIDiscovery, JetsonHasNativeEnumerator) {
    ensure_runtime();
    EXPECT_NE(make_enumerator(device_type(device_type::jetson, "jetson")), nullptr);
}
