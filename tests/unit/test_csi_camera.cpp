#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <set>
#include <thread>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/common/proto_convert.hpp>
#include <viam/sdk/common/utils.hpp>
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

// Overrides VIAM_CSI_DEVICE for one test and restores it afterwards
class ScopedDevice {
   public:
    explicit ScopedDevice(const char* device) : prev_device(viam::sdk::get_env("VIAM_CSI_DEVICE")) {
        setenv("VIAM_CSI_DEVICE", device, 1);
    }
    ~ScopedDevice() {
        if (prev_device) {
            setenv("VIAM_CSI_DEVICE", prev_device->c_str(), 1);
        } else {
            unsetenv("VIAM_CSI_DEVICE");
        }
    }

   private:
    boost::optional<std::string> prev_device;
};

static void expect_jpeg(const Camera::image_collection& collection) {
    ASSERT_EQ(collection.images.size(), 1);
    const auto& bytes = collection.images[0].bytes;
    ASSERT_GE(bytes.size(), 2);
    EXPECT_EQ(bytes[0], 0xFF);
    EXPECT_EQ(bytes[1], 0xD8);
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
    ScopedDevice pi("pi");

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    CSICamera camera("test", attrs);
    for (int i = 0; i < 3; i++) {
        expect_jpeg(camera.get_images({}, ProtoStruct{}));
    }
    camera.stop_pipeline();
}

// Test that with encode_on_request a Pi encodes the newest raw frame when
// asked, and reports that frame's capture time
TEST(CSICamera, EncodeOnRequestReturnsJpeg) {
    ensure_runtime();
    ScopedDevice pi("pi");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    CSICamera camera("test", attrs);
    EXPECT_TRUE(camera.get_encode_on_request());
    std::set<int64_t> served;
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            // The test source runs at 5 fps, so each request sees a new frame
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        auto collection = camera.get_images({}, ProtoStruct{});
        expect_jpeg(collection);
        auto age =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - collection.metadata.captured_at);
        EXPECT_GE(age.count(), 0);
        EXPECT_LE(age.count(), camera.max_frame_age().count());
        served.insert(collection.metadata.captured_at.time_since_epoch().count());
    }
    EXPECT_EQ(served.size(), 3);
    EXPECT_EQ(camera.get_encode_count(), 3);
    camera.stop_pipeline();
}

// Test that concurrent requests for the same frame share one encode: every
// encode serves a frame, and no frame is encoded twice
TEST(CSICamera, EncodeOnRequestSharesEncodes) {
    ensure_runtime();
    ScopedDevice pi("pi");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    CSICamera camera("test", attrs);

    constexpr int num_consumers = 8;
    constexpr int images_per_consumer = 5;
    std::mutex served_mutex;
    std::set<int64_t> served;
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; i++) {
        consumers.emplace_back([&] {
            for (int j = 0; j < images_per_consumer; j++) {
                auto collection = camera.get_images({}, ProtoStruct{});
                expect_jpeg(collection);
                std::lock_guard<std::mutex> lock(served_mutex);
                served.insert(collection.metadata.captured_at.time_since_epoch().count());
            }
        });
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    EXPECT_EQ(camera.get_encode_count(), served.size());
    EXPECT_LT(served.size(), num_consumers * images_per_consumer);
    camera.stop_pipeline();
}

// Test that encode_on_request is ignored off a Pi
TEST(CSICamera, EncodeOnRequestIgnoredOffPi) {
    ensure_runtime();
    ScopedDevice jetson("jetson");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    CSICamera camera("test", attrs);
    expect_jpeg(camera.get_images({}, ProtoStruct{}));
    EXPECT_EQ(camera.get_encode_count(), 0);
    camera.stop_pipeline();
}

static ProtoStruct extra_with(const char* key) {
    ProtoStruct extra;
    extra.insert(std::make_pair(key, ProtoValue(true)));
    return extra;
}

static int64_t captured_ns(const Camera::image_collection& collection) {
    return collection.metadata.captured_at.time_since_epoch().count();
}

// Test that a last_served_frame request gets the last frame served to a fresh
// request, even after newer frames arrive, without encoding
TEST(CSICamera, LastServedFrameReusesServedFrame) {
    ensure_runtime();
    ScopedDevice pi("pi");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    CSICamera camera("test", attrs);

    auto fresh = camera.get_images({}, ProtoStruct{});
    expect_jpeg(fresh);
    EXPECT_EQ(camera.get_encode_count(), 1);
    // The test source runs at 5 fps, so newer frames arrive meanwhile
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (int i = 0; i < 3; i++) {
        auto served = camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY));
        expect_jpeg(served);
        EXPECT_EQ(served.images[0].bytes, fresh.images[0].bytes);
        EXPECT_EQ(captured_ns(served), captured_ns(fresh));
    }
    EXPECT_EQ(camera.get_encode_count(), 1);

    // A fresh request still gets a newer frame, which becomes the last served
    auto newer = camera.get_images({}, ProtoStruct{});
    EXPECT_GT(captured_ns(newer), captured_ns(fresh));
    EXPECT_EQ(captured_ns(camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY))), captured_ns(newer));
    EXPECT_EQ(camera.get_encode_count(), 2);

    // A stopped camera errors instead of serving its last frame
    camera.stop_pipeline();
    EXPECT_ANY_THROW(camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY)));
}

// Test that the first last_served_frame request encodes one frame, which later
// last_served_frame requests reuse
TEST(CSICamera, LastServedFrameEncodesFirstFrame) {
    ensure_runtime();
    ScopedDevice pi("pi");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    CSICamera camera("test", attrs);

    auto first = camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY));
    expect_jpeg(first);
    EXPECT_EQ(camera.get_encode_count(), 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(captured_ns(camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY))), captured_ns(first));
    EXPECT_EQ(camera.get_encode_count(), 1);
    camera.stop_pipeline();
}

// Test that viam-server's live-view polling gets the last served frame by
// default, and fresh frames with fresh_frames_for_stream
TEST(CSICamera, StreamServerGetsLastServedFrame) {
    ensure_runtime();
    ScopedDevice pi("pi");

    ProtoStruct attrs;
    attrs.insert(std::make_pair("encode_on_request", ProtoValue(true)));
    {
        CSICamera camera("test", attrs);
        EXPECT_FALSE(camera.get_fresh_frames_for_stream());
        auto fresh = camera.get_images({}, ProtoStruct{});
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        EXPECT_EQ(captured_ns(camera.get_images({}, extra_with(FROM_STREAM_SERVER_KEY))), captured_ns(fresh));
        EXPECT_EQ(camera.get_encode_count(), 1);
        camera.stop_pipeline();
    }

    attrs.insert(std::make_pair("fresh_frames_for_stream", ProtoValue(true)));
    CSICamera camera("test", attrs);
    EXPECT_TRUE(camera.get_fresh_frames_for_stream());
    auto fresh = camera.get_images({}, ProtoStruct{});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_GT(captured_ns(camera.get_images({}, extra_with(FROM_STREAM_SERVER_KEY))), captured_ns(fresh));
    EXPECT_EQ(camera.get_encode_count(), 2);
    camera.stop_pipeline();
}

// Test that last_served_frame behaves the same when every frame is encoded
TEST(CSICamera, LastServedFrameWithoutEncodeOnRequest) {
    ensure_runtime();

    ProtoStruct attrs = std::unordered_map<std::string, ProtoValue>();
    CSICamera camera("test", attrs);
    auto fresh = camera.get_images({}, ProtoStruct{});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto served = camera.get_images({}, extra_with(LAST_SERVED_FRAME_KEY));
    EXPECT_EQ(captured_ns(served), captured_ns(fresh));
    EXPECT_GT(captured_ns(camera.get_images({}, ProtoStruct{})), captured_ns(fresh));
    camera.stop_pipeline();
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
