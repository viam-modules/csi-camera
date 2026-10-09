#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/components/camera.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/log/logging.hpp>

#include "utils.h"

// Owning pointers for GStreamer references: gst_ptr<T, unref_fn> releases its
// reference with unref_fn
template <typename T, auto unref_fn>
struct gst_deleter {
    void operator()(T* p) const {
        unref_fn(p);
    }
};

template <typename T, auto unref_fn>
using gst_ptr = std::unique_ptr<T, gst_deleter<T, unref_fn>>;

// Older GStreamer (e.g. 1.16) defines the mini-object unrefs, such as
// gst_sample_unref, as static inline functions; a class member whose type
// names one gets different types in different translation units, so these go
// through the exported gst_mini_object_unref
template <typename T>
inline void gst_mini_object_release(T* p) {
    gst_mini_object_unref(GST_MINI_OBJECT_CAST(p));
}

using gst_buffer_ptr = gst_ptr<GstBuffer, gst_mini_object_release<GstBuffer>>;
using gst_bus_ptr = gst_ptr<GstBus, gst_object_unref>;
using gst_caps_ptr = gst_ptr<GstCaps, gst_mini_object_release<GstCaps>>;
using gst_element_ptr = gst_ptr<GstElement, gst_object_unref>;
using gst_message_ptr = gst_ptr<GstMessage, gst_mini_object_release<GstMessage>>;
using gst_pad_ptr = gst_ptr<GstPad, gst_object_unref>;
using gst_sample_ptr = gst_ptr<GstSample, gst_mini_object_release<GstSample>>;

// Stops a pipeline before releasing it, so it never outlives its owner running
inline void gst_pipeline_stop_and_unref(GstElement* pipeline) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}
using gst_pipeline_ptr = gst_ptr<GstElement, gst_pipeline_stop_and_unref>;

class CSICamera : public viam::sdk::Camera {
   private:
    // Device
    device_type device;

    // Camera
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;
    std::string video_path;
    bool encode_on_request = false;
    // Pi with encode_on_request: the camera pipeline ends at raw frames and
    // get_images encodes the newest one
    bool raw_frames = false;

    // GST
    GstElement* pipeline = nullptr;
    GstBus* bus = nullptr;
    GstElement* appsink = nullptr;

    // Latest-frame cache: written by the GStreamer streaming thread via the
    // appsink new-sample callback, read concurrently by any number of
    // get_images callers. With raw_frames it holds the newest raw sample
    // instead of a JPEG.
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    std::shared_ptr<const std::vector<unsigned char>> latest_frame;
    gst_sample_ptr latest_sample;
    uint64_t latest_frame_seq = 0;
    std::chrono::system_clock::time_point latest_frame_time;

    // On-request encoding: owns the encode pipeline and the last encoded
    // frame; set only with raw_frames (defined in csi_camera.cpp)
    class frame_encoder;
    std::unique_ptr<frame_encoder> encoder;

    // Pipeline failure reporting
    void check_bus();
    std::string drain_bus_errors();
    [[noreturn]] void fail_pipeline(const std::string& what);
    // Configured mode plus a device-specific pointer to the supported modes,
    // appended to errors that are usually caused by an unsupported resolution
    std::string mode_hint() const;

   public:
    // Module
    explicit CSICamera(const std::string name, const viam::sdk::ProtoStruct& attrs);
    ~CSICamera();
    void init(const viam::sdk::ProtoStruct& attrs);
    void init_csi(const std::string pipeline_args);
    void validate_attrs(const viam::sdk::ProtoStruct& attrs);
    template <typename T>
    void set_attr(const viam::sdk::ProtoStruct& attrs, const std::string& name, T CSICamera::* member, T de);

    // Camera
    // overrides camera component interface
    image_collection get_images(std::vector<std::string> /* filter_source_names */, const viam::sdk::ProtoStruct& /* extra */) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    point_cloud get_point_cloud(const std::string mime_type, const viam::sdk::ProtoStruct& extra) override;
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra) override;
    properties get_properties() override;
    viam::sdk::ProtoStruct get_status() override;

    // GST
    // helpers to manage GStreamer pipeline lifecycle
    std::string create_pipeline() const;
    void wait_pipeline();
    void stop_pipeline();
    void catch_pipeline(GstMessage* msg);

    // Image
    // helpers to read frames from the latest-frame cache
    struct cached_frame {
        std::shared_ptr<const std::vector<unsigned char>> bytes;
        std::chrono::system_clock::time_point captured_at;
    };
    cached_frame get_latest_frame();
    void wait_for_frame(std::unique_lock<std::mutex>& lock);
    std::chrono::milliseconds max_frame_age() const;

    std::vector<unsigned char> get_csi_image();
    static std::vector<unsigned char> buff_to_vec(GstBuffer* buff);

    // Pi only: makes the encoder read each frame from a copy in system memory
    void copy_encoder_input();
    static GstPadProbeReturn on_encoder_input(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    // Appsink new-sample callback: invoked on the GStreamer streaming thread
    // for every frame, stores it in the latest-frame cache
    static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer user_data);
    GstFlowReturn handle_new_sample();

    // Getters
    int get_width_px() const;
    int get_height_px() const;
    int get_frame_rate() const;
    bool get_encode_on_request() const;
    uint64_t get_encode_count() const;
    std::string get_video_path() const;
    GstBus* get_bus() const;
    GstElement* get_appsink() const;
    GstElement* get_pipeline() const;
};
