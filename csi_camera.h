#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/components/camera.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/log/logging.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>

#include "utils.h"

using namespace viam::sdk;

class CSICamera : public Camera, public Reconfigurable {
   private:
    // Device
    device_type device;

    // Camera
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;
    std::string video_path;

    // GST
    GstElement* pipeline = nullptr;
    GstBus* bus = nullptr;
    GstMessage* msg = nullptr;
    GstSample* sample = nullptr;
    GstBuffer* buffer = nullptr;
    GstElement* appsink = nullptr;

   public:
    // Module
    explicit CSICamera(const std::string name, const ProtoStruct& attrs);
    ~CSICamera();
    void init(const ProtoStruct& attrs);
    void init_csi(const std::string pipeline_args);
    void validate_attrs(const ProtoStruct& attrs);
    template <typename T>
    void set_attr(const ProtoStruct& attrs, const std::string& name, T CSICamera::* member, T de);

    // Camera
    // overrides camera component interface
    void reconfigure(const Dependencies& deps, const ResourceConfig& cfg) override;
    raw_image get_image(const std::string mime_type, const ProtoStruct& extra) override;
    image_collection get_images() override;
    ProtoStruct do_command(const ProtoStruct& command) override;
    point_cloud get_point_cloud(const std::string mime_type, const ProtoStruct& extra) override;
    std::vector<GeometryConfig> get_geometries(const ProtoStruct& extra) override;
    properties get_properties() override;

    // GST
    // helpers to manage GStreamer pipeline lifecycle
    std::string create_pipeline() const;
    void wait_pipeline();
    void stop_pipeline();
    void catch_pipeline();

    // Image
    // helpers to pull and process images from appsink
    std::vector<unsigned char> get_csi_image();
    std::vector<unsigned char> buff_to_vec(GstBuffer* buff);

    // Getters
    std::string get_name() const;
    int get_width_px() const;
    int get_height_px() const;
    int get_frame_rate() const;
    std::string get_video_path() const;
    GstBus* get_bus() const;
    GstElement* get_appsink() const;
    GstElement* get_pipeline() const;
};
