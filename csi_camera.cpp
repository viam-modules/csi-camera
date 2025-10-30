#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "constraints.h"
#include "csi_camera.h"

using namespace viam::sdk;

CSICamera::CSICamera(const std::string name, const ProtoStruct& attrs) : Camera(std::move(name)) {
    device = get_device_type();
    VIAM_SDK_LOG(debug) << "Creating CSICamera with name: " << name;
    VIAM_SDK_LOG(debug) << "Device type: " << device.name;
    init(attrs);
}

CSICamera::~CSICamera() {
    VIAM_SDK_LOG(debug) << "Destroying CSICamera";
    stop_pipeline();
}

void CSICamera::init(const ProtoStruct& attrs) {
    validate_attrs(attrs);
    auto pipeline_args = create_pipeline();
    VIAM_SDK_LOG(debug) << "pipeline_args: " << pipeline_args;
    init_csi(pipeline_args);
}

void CSICamera::validate_attrs(const ProtoStruct& attrs) {
    set_attr<int>(attrs, "width_px", &CSICamera::width_px, DEFAULT_INPUT_WIDTH);
    set_attr<int>(attrs, "height_px", &CSICamera::height_px, DEFAULT_INPUT_HEIGHT);
    set_attr<int>(attrs, "frame_rate", &CSICamera::frame_rate, DEFAULT_INPUT_FRAMERATE);
    set_attr<std::string>(attrs, "video_path", &CSICamera::video_path, DEFAULT_INPUT_SENSOR);
}

template <typename T>
void CSICamera::set_attr(const ProtoStruct& attrs, const std::string& name, T CSICamera::* member, T de) {
    if (attrs.count(name) == 1) {
        const ProtoValue& val = attrs.at(name);
        if constexpr (std::is_same<T, int>::value) {
            this->*member = static_cast<int>(val.get_unchecked<double>());
        } else if constexpr (std::is_same<T, std::string>::value) {
            this->*member = val.get_unchecked<std::string>();
        } else if constexpr (std::is_same<T, bool>::value) {
            this->*member = val.get_unchecked<bool>();
        }
    } else {
        this->*member = de;  // Set the default value if the attribute is not found
    }
}

void CSICamera::reconfigure(const Dependencies& deps, const ResourceConfig& cfg) {
    VIAM_SDK_LOG(debug) << "Reconfiguring CSI Camera module";
    stop_pipeline();
    auto attrs = cfg.attributes();
    init(attrs);
}

Camera::raw_image CSICamera::get_image(const std::string mime_type, const ProtoStruct& extra) {
    raw_image image;
    image.mime_type = DEFAULT_OUTPUT_MIMETYPE;
    image.bytes = get_csi_image();
    if (image.bytes.empty()) {
        throw Exception("no bytes retrieved from get_csi_image");
    }

    return image;
}

Camera::image_collection CSICamera::get_images(std::vector<std::string> /* filter_source_names */, const ProtoStruct& /* extra */) {
    // filter_source_names and extra are unused because this camera provides a single image source
    // and doesn't include any extra support for get_images
    ProtoStruct empty_extra;
    // If image is not available, an exception will be thrown
    raw_image image = get_image(DEFAULT_OUTPUT_MIMETYPE, empty_extra);
    image.source_name = "";  // empty string because we don't have multiple sources to differentiate

    image_collection collection;
    collection.images = std::vector<raw_image>{std::move(image)};
    auto now = std::chrono::system_clock::now();
    auto duration_since_epoch = now.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch);
    collection.metadata.captured_at = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>(nanoseconds);

    return collection;
}

ProtoStruct CSICamera::do_command(const ProtoStruct& command) {
    VIAM_SDK_LOG(warn) << "do_command not implemented";
    return ProtoStruct{};
}

Camera::point_cloud CSICamera::get_point_cloud(const std::string mime_type, const ProtoStruct& extra) {
    VIAM_SDK_LOG(warn) << "get_point_cloud not implemented";
    return point_cloud{};
}

std::vector<GeometryConfig> CSICamera::get_geometries(const ProtoStruct& extra) {
    VIAM_SDK_LOG(error) << "get_geometries not implemented";
    return std::vector<GeometryConfig>{};
}

Camera::properties CSICamera::get_properties() {
    Camera::properties p{};
    p.supports_pcd = false;
    p.intrinsic_parameters.width_px = width_px;
    p.intrinsic_parameters.height_px = height_px;
    return p;
}

void CSICamera::init_csi(const std::string pipeline_args) {
    // Build gst pipeline
    GError* error = nullptr;
    pipeline = gst_parse_launch(pipeline_args.c_str(), &error);
    if (!pipeline) {
        std::cerr << "Failed to create the pipeline" << std::endl;
        g_print("Error: %s\n", error->message);
        g_error_free(error);
        throw Exception("Failed to create the pipeline");
    }

    // Fetch the appsink element
    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink0");
    if (!appsink) {
        gst_object_unref(pipeline);
        throw Exception("Failed to get the appsink element");
    }

    // Start the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        throw Exception("Failed to start the pipeline");
    }

    // Handle async pipeline creation
    wait_pipeline();

    // Run the main loop
    bus = gst_element_get_bus(pipeline);
    if (!bus) {
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        throw Exception("Failed to get the bus for the pipeline");
    }
}

// Handles async GST state change
void CSICamera::wait_pipeline() {
    GstState state, pending;
    GstStateChangeReturn ret;

    // Set timeout for state change
    const int timeout_microseconds = GST_CHANGE_STATE_TIMEOUT * 1000000;  // Convert seconds to microseconds
    auto start_time = std::chrono::high_resolution_clock::now();

    // Wait for state change to complete
    while ((ret = gst_element_get_state(pipeline, &state, &pending, GST_GET_STATE_TIMEOUT * GST_SECOND)) == GST_STATE_CHANGE_ASYNC) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - start_time).count();

        if (elapsed_time >= timeout_microseconds) {
            throw Exception("Timeout: GST pipeline state change did not complete within timeout limit");
        }

        // Wait for a short duration to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (ret == GST_STATE_CHANGE_SUCCESS) {
        std::cout << "GST pipeline state change success" << std::endl;
    } else if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "GST pipeline failed to change state" << std::endl;
        throw Exception("GST pipeline failed to change state");
    } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
        std::cout << "GST pipeline changed but not enough data for preroll" << std::endl;
    } else {
        std::cerr << "GST pipeline failed to change state" << std::endl;
        throw Exception("GST pipeline failed to change state");
    }
}

void CSICamera::stop_pipeline() {
    VIAM_SDK_LOG(debug) << "Stopping GST pipeline";

    // Check if pipeline is defined
    if (pipeline == nullptr) {
        VIAM_SDK_LOG(error) << "Pipeline is not defined";
        return;
    }

    // Stop the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        // Don't throw, continue cleanup
        VIAM_SDK_LOG(error) << "Failed to stop the pipeline";
    }

    // Wait for async state change
    try {
        wait_pipeline();
    } catch (const std::exception& e) {
        // Don't throw, continue cleanup
        VIAM_SDK_LOG(error) << "Exception during wait_pipeline: " << e.what();
    }

    // Free resources
    if (appsink)
        gst_object_unref(appsink);
    if (pipeline)
        gst_object_unref(pipeline);
    if (bus)
        gst_object_unref(bus);
    appsink = nullptr;
    pipeline = nullptr;
    bus = nullptr;
}

void CSICamera::catch_pipeline() {
    GError* error = nullptr;
    gchar* debugInfo = nullptr;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &error, &debugInfo);
            VIAM_SDK_LOG(debug) << "Debug Info: " << debugInfo;
            stop_pipeline();
            throw Exception("GST pipeline error: " + std::string(error->message));
            break;
        case GST_MESSAGE_EOS:
            VIAM_SDK_LOG(debug) << "End of stream received, stopping pipeline";
            stop_pipeline();
            throw Exception("End of stream received, pipeline stopped");
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(msg, &error, &debugInfo);
            VIAM_SDK_LOG(warn) << "Warning: " << error->message;
            VIAM_SDK_LOG(warn) << "Debug Info: " << debugInfo;
            break;
        case GST_MESSAGE_INFO:
            gst_message_parse_info(msg, &error, &debugInfo);
            VIAM_SDK_LOG(info) << "Info: " << error->message;
            VIAM_SDK_LOG(info) << "Debug Info: " << debugInfo;
            break;
        default:
            // Ignore other message types
            break;
    }

    // Cleanup
    if (error != nullptr)
        g_error_free(error);
    if (debugInfo != nullptr)
        g_free(debugInfo);
}

std::vector<unsigned char> CSICamera::get_csi_image() {
    // Pull sample from appsink
    std::vector<unsigned char> vec;
    sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (sample != nullptr) {
        // Retrieve buffer from the sample
        buffer = gst_sample_get_buffer(sample);

        // Process or handle the buffer as needed
        vec = buff_to_vec(buffer);

        // Release the sample
        gst_sample_unref(sample);
    }

    // Check bus for messages
    msg = gst_bus_pop(bus);
    if (msg != nullptr) {
        catch_pipeline();
        gst_message_unref(msg);
        msg = nullptr;
    }

    return vec;
}

std::string CSICamera::create_pipeline() const {
    const char* test_mode = std::getenv("VIAM_CSI_TEST_MODE");
    if (test_mode != nullptr && std::string(test_mode) == "1") {
        VIAM_SDK_LOG(warn) << "CI Test mode enabled";
        return TEST_GST_PIPELINE;
    }

    auto device_params = get_device_params(device);
    std::string input_sensor = (device.value == device_type::jetson) ? (" sensor-id=" + video_path) : "";

    std::ostringstream oss;
    oss << device_params.input_source << input_sensor << " ! " << device_params.input_format << ",width=" << std::to_string(width_px)
        << ",height=" << std::to_string(height_px) << ",framerate=" << std::to_string(frame_rate) << "/1 ! "
        << device_params.video_converter << " ! " << device_params.output_encoder << " ! "
        << "image/jpeg"
        << " ! appsink name=appsink0 sync=false max-buffers=1 drop=true";

    return oss.str();
}

std::vector<unsigned char> CSICamera::buff_to_vec(GstBuffer* buff) {
    // Get the size of the buffer
    size_t bufferSize = gst_buffer_get_size(buff);

    // Create a vector with the same size as the buffer
    std::vector<unsigned char> vec(bufferSize);

    // Copy the buffer data to the vector
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    memcpy(vec.data(), map.data, bufferSize);
    gst_buffer_unmap(buffer, &map);

    return vec;
}

std::string CSICamera::get_video_path() const {
    return video_path;
}

int CSICamera::get_width_px() const {
    return width_px;
}

int CSICamera::get_height_px() const {
    return height_px;
}

int CSICamera::get_frame_rate() const {
    return frame_rate;
}

GstElement* CSICamera::get_appsink() const {
    return appsink;
}

GstElement* CSICamera::get_pipeline() const {
    return pipeline;
}

GstBus* CSICamera::get_bus() const {
    return bus;
}
