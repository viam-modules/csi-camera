#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/common/proto_convert.hpp>
#include <viam/sdk/components/camera.hpp>

#include "../../constraints.h"
#include "../../csi_camera.cpp"
#include "../../utils.cpp"

using namespace viam::sdk;

// One-time runtime bootstrap to initialize Viam SDK Instance and GStreamer
static void ensure_runtime() {
    static bool inited = false;
    if (inited)
        return;
    static Instance inst;
    gst_init(nullptr, nullptr);
    inited = true;
}

// Test that the camera can be created with default values
TEST(CSICamera, CreateDefault) {
    ensure_runtime();

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();

    CSICamera camera("test", attrs);

    EXPECT_EQ(camera.get_width_px(), DEFAULT_INPUT_WIDTH);
    EXPECT_EQ(camera.get_height_px(), DEFAULT_INPUT_HEIGHT);
    EXPECT_EQ(camera.get_frame_rate(), DEFAULT_INPUT_FRAMERATE);
    EXPECT_EQ(camera.get_video_path(), DEFAULT_INPUT_SENSOR);

    camera.stop_pipeline();
}

// Test that the camera can be created with custom values
TEST(CSICamera, CreateCustom) {
    ensure_runtime();

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    attrs.insert(std::make_pair("width_px", ProtoValue(640)));
    attrs.insert(std::make_pair("height_px", ProtoValue(480)));
    attrs.insert(std::make_pair("frame_rate", ProtoValue(60)));
    attrs.insert(std::make_pair("video_path", ProtoValue(std::string("1"))));

    CSICamera camera("test", attrs);

    EXPECT_EQ(camera.get_width_px(), 640);
    EXPECT_EQ(camera.get_height_px(), 480);
    EXPECT_EQ(camera.get_frame_rate(), 60);
    EXPECT_EQ(camera.get_video_path(), "1");

    camera.stop_pipeline();
}

// Test that non-positive dimensions are rejected before any pipeline is built
TEST(CSICamera, RejectsNonPositiveResolution) {
    ensure_runtime();

    for (const auto& name : {"width_px", "height_px", "frame_rate"}) {
        ProtoStruct attrs;
        attrs.insert(std::make_pair(name, ProtoValue(0)));
        try {
            CSICamera camera("test", attrs);
            FAIL() << name << "=0 did not throw";
        } catch (const Exception& e) {
            EXPECT_NE(std::string(e.what()).find(name), std::string::npos) << e.what();
        }
    }
}

// Test that a wrongly typed attribute names the attribute in the error
TEST(CSICamera, RejectsWrongAttrType) {
    ensure_runtime();

    ProtoStruct attrs;
    attrs.insert(std::make_pair("width_px", ProtoValue(std::string("1920"))));
    try {
        CSICamera camera("test", attrs);
        FAIL() << "string width_px did not throw";
    } catch (const Exception& e) {
        EXPECT_NE(std::string(e.what()).find("unexpected value type for attribute width_px"), std::string::npos) << e.what();
    }
}

// Test that a pipeline that fails to negotiate reports the GStreamer error,
// the failing element and the configured mode, and leaves no pipeline behind
TEST(CSICamera, InitFailureReportsBusError) {
    ensure_runtime();

    ProtoStruct attrs;
    CSICamera camera("test", attrs);
    camera.stop_pipeline();

    try {
        camera.init_csi("videotestsrc ! video/x-raw,format=NV12 ! video/x-raw,format=RGB ! appsink name=appsink0");
        FAIL() << "unnegotiable pipeline did not throw";
    } catch (const Exception& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("videotestsrc"), std::string::npos) << what;
        EXPECT_NE(what.find("Internal data stream error"), std::string::npos) << what;
        EXPECT_NE(what.find("configured mode 1920x1080@30fps"), std::string::npos) << what;
    }
    EXPECT_EQ(camera.get_pipeline(), nullptr);
    EXPECT_EQ(camera.get_appsink(), nullptr);
}

// Test that many concurrent consumers can each get frames: consumers read a
// shared latest-frame cache, so one client's request does not consume the
// frame another client is waiting on
TEST(CSICamera, ConcurrentConsumers) {
    ensure_runtime();

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    CSICamera camera("test", attrs);

    constexpr int num_consumers = 8;
    constexpr int images_per_consumer = 5;
    std::atomic<int> successes{0};

    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; i++) {
        consumers.emplace_back([&camera, &successes] {
            for (int j = 0; j < images_per_consumer; j++) {
                auto collection = camera.get_images({}, ProtoStruct{});
                ASSERT_EQ(collection.images.size(), 1);
                ASSERT_FALSE(collection.images[0].bytes.empty());
                successes++;
            }
        });
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    EXPECT_EQ(successes.load(), num_consumers * images_per_consumer);

    camera.stop_pipeline();
}

// Test that get_images reports the frame capture time, not the request time
TEST(CSICamera, CapturedAtIsRecent) {
    ensure_runtime();

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    CSICamera camera("test", attrs);

    auto collection = camera.get_images({}, ProtoStruct{});
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - collection.metadata.captured_at);
    EXPECT_GE(age.count(), 0);
    EXPECT_LE(age.count(), camera.max_frame_age().count());

    camera.stop_pipeline();
}

// Test that on a Pi the encoder still produces JPEGs when it reads each frame
// from a copy in system memory
TEST(CSICamera, PiEncodesCopiedFrames) {
    ensure_runtime();

    const char* prev = std::getenv("VIAM_CSI_DEVICE");
    const std::string prev_device = prev ? prev : "";
    setenv("VIAM_CSI_DEVICE", "pi", 1);

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    CSICamera camera("test", attrs);
    for (int i = 0; i < 3; i++) {
        auto collection = camera.get_images({}, ProtoStruct{});
        ASSERT_EQ(collection.images.size(), 1);
        const auto& bytes = collection.images[0].bytes;
        ASSERT_GE(bytes.size(), 2);
        EXPECT_EQ(bytes[0], 0xFF);
        EXPECT_EQ(bytes[1], 0xD8);
    }
    camera.stop_pipeline();

    if (prev) {
        setenv("VIAM_CSI_DEVICE", prev_device.c_str(), 1);
    } else {
        unsetenv("VIAM_CSI_DEVICE");
    }
}

// Test that GST pipeline can be started and stopped
TEST(CSICamera, StartStopPipeline) {
    gst_init(nullptr, nullptr);

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();

    CSICamera camera("test", attrs);

    auto pipeline = camera.get_pipeline();
    auto appsink = camera.get_appsink();
    EXPECT_EQ(GST_STATE(pipeline), GST_STATE_PLAYING);
    EXPECT_EQ(GST_STATE(appsink), GST_STATE_PLAYING);

    camera.stop_pipeline();

    EXPECT_EQ(camera.get_pipeline(), nullptr);
    EXPECT_EQ(camera.get_appsink(), nullptr);
}
