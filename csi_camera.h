#pragma once

#include <vector>
#include <string>
#include <memory>
#include <map>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <viam/sdk/components/camera/camera.hpp>
#include <viam/api/component/camera/v1/camera.grpc.pb.h>

#include "utils.h"

using namespace viam::sdk;

class CSICamera : public Camera {
private:
    // Device
    device_type device;

    // Camera
    bool debug;
    int width_px = 0;
    int height_px = 0;
    int frame_rate = 0;
    std::string video_path;
    std::string flip_method;

    // GST
    GstElement *pipeline = nullptr;
    GstBus *bus = nullptr;
    GstMessage *msg = nullptr;
    GstSample *sample = nullptr;
    GstBuffer *buffer = nullptr;
    GstElement* appsink = nullptr;

public:
    // Module
    explicit CSICamera(const std::string name, const AttributeMap attrs);
    ~CSICamera();
    void init(const AttributeMap attrs);
    void init_csi(const std::string pipeline_args);
    void validate_attrs(const AttributeMap attrs);
    template <typename T>
    void set_attr(const AttributeMap& attrs, const std::string& name, T CSICamera::* member, T de);

    // Camera
    // overrides camera component interface
    void reconfigure(const Dependencies deps, const ResourceConfig cfg) override;
    raw_image get_image(const std::string mime_type, const AttributeMap& extra) override;
    image_collection get_images() override;
    AttributeMap do_command(const AttributeMap command) override;
    point_cloud get_point_cloud(const std::string mime_type, const AttributeMap& extra) override;
    std::vector<GeometryConfig> get_geometries(const AttributeMap& extra) override;
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
    std::vector<unsigned char> buff_to_vec(GstBuffer *buff);

    // Getters
    std::string get_name() const;
    bool is_debug() const;
    int get_width_px() const;
    int get_height_px() const;
    int get_frame_rate() const;
    std::string get_video_path() const;
    GstBus* get_bus() const;
    GstElement* get_appsink() const;
    GstElement* get_pipeline() const;
};
