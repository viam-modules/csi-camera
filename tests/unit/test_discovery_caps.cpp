#include <gtest/gtest.h>
#include <gst/gst.h>

#include "../../discovery_caps.h"

static void ensure_gst() {
    static bool inited = false;
    if (!inited) {
        gst_init(nullptr, nullptr);
        inited = true;
    }
}

TEST(DiscoveryCaps, FixedResolution) {
    ensure_gst();
    GstCaps* caps = gst_caps_from_string("video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1");
    int skipped = -1;
    const auto res = parse_caps(caps, &skipped);
    gst_caps_unref(caps);

    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].width, 1920);
    EXPECT_EQ(res[0].height, 1080);
    EXPECT_DOUBLE_EQ(res[0].frame_rate, 30.0);
    EXPECT_EQ(res[0].format, "NV12");
    EXPECT_EQ(skipped, 0);
}

TEST(DiscoveryCaps, MultipleStructuresDeduped) {
    ensure_gst();
    GstCaps* caps = gst_caps_from_string(
        "video/x-raw,width=640,height=480,framerate=30/1;"
        "video/x-raw,width=1280,height=720,framerate=60/1;"
        "video/x-raw,width=640,height=480,framerate=30/1");
    const auto res = parse_caps(caps);
    gst_caps_unref(caps);

    ASSERT_EQ(res.size(), 2u);  // duplicate 640x480 collapsed
    EXPECT_EQ(res[0].width, 640);
    EXPECT_EQ(res[1].width, 1280);
}

TEST(DiscoveryCaps, FramerateListPicksMax) {
    ensure_gst();
    GstCaps* caps = gst_caps_from_string("video/x-raw,width=800,height=600,framerate={ 15/1, 30/1, 60/1 }");
    const auto res = parse_caps(caps);
    gst_caps_unref(caps);

    ASSERT_EQ(res.size(), 1u);
    EXPECT_DOUBLE_EQ(res[0].frame_rate, 60.0);
}

// libcamerasrc-style ranged caps are not concrete enough for a component
// config, so they must be skipped rather than silently turned into a bogus size.
TEST(DiscoveryCaps, RangedDimensionsSkipped) {
    ensure_gst();
    GstCaps* caps =
        gst_caps_from_string("video/x-raw,width=[ 16, 4608 ],height=[ 16, 2592 ],framerate=[ 0/1, 100/1 ]");
    int skipped = -1;
    const auto res = parse_caps(caps, &skipped);
    gst_caps_unref(caps);

    EXPECT_EQ(res.size(), 0u);
    EXPECT_EQ(skipped, 1);
}

TEST(DiscoveryCaps, NullCaps) {
    ensure_gst();
    int skipped = -1;
    const auto res = parse_caps(nullptr, &skipped);
    EXPECT_TRUE(res.empty());
    EXPECT_EQ(skipped, 0);
}
