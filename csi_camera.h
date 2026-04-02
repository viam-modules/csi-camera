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

#include "utils.h"

class CSICamera : public viam::sdk::Camera {
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
    GstElement* appsink = nullptr;

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
    // helpers to pull and process images from appsink
    std::vector<unsigned char> get_csi_image();
    std::vector<unsigned char> buff_to_vec(GstBuffer* buff);

    // Getters
    int get_width_px() const;
    int get_height_px() const;
    int get_frame_rate() const;
    std::string get_video_path() const;
    GstBus* get_bus() const;
    GstElement* get_appsink() const;
    GstElement* get_pipeline() const;
};
