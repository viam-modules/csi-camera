#include <iostream>
#include <fstream>
#include <cstring>
#include <sstream>
#include <chrono>
#include <thread>

#include "constraints.h"
#include "csi_camera.h"

using namespace viam::sdk;

CSICamera::CSICamera(const std::string name, const AttributeMap attrs) : Camera(std::move(name)) {
    device = get_device_type();
    std::cout << "Creating CSICamera with name: " << name << std::endl;
    std::cout << "Device type: " << device.name << std::endl;
    init(attrs);
}

CSICamera::~CSICamera() {
    std::cout << "Destroying CSICamera" << std::endl;
    stop_pipeline();
}

void CSICamera::init(const AttributeMap attrs) {
    validate_attrs(attrs);
    auto pipeline_args = create_pipeline();
    if (debug) {
        std::cout << "pipeline_args: " << pipeline_args << std::endl;
    }
    init_csi(pipeline_args);
}

void CSICamera::validate_attrs(const AttributeMap attrs) {
    set_attr<int>(attrs, "width_px", &CSICamera::width_px, DEFAULT_INPUT_WIDTH);
    set_attr<int>(attrs, "height_px", &CSICamera::height_px, DEFAULT_INPUT_HEIGHT);
    set_attr<int>(attrs, "frame_rate", &CSICamera::frame_rate, DEFAULT_INPUT_FRAMERATE);
    set_attr<std::string>(attrs, "video_path", &CSICamera::video_path, DEFAULT_INPUT_SENSOR);
    set_attr<std::string>(attrs, "flip_method", &CSICamera::flip_method, DEFAULT_FLIP_METHOD);
    set_attr<bool>(attrs, "debug", &CSICamera::debug, false);
}

template <typename T>
void CSICamera::set_attr(const AttributeMap& attrs, const std::string& name, T CSICamera::* member, T de) {
    if (attrs->count(name) == 1) {
        std::shared_ptr<ProtoType> proto = attrs->at(name);
        auto val = proto->proto_value();

        if constexpr (std::is_same<T, int>::value) {
            this->*member = val.number_value();
        } else if constexpr (std::is_same<T, std::string>::value) {
            this->*member = val.string_value();
        } else if constexpr (std::is_same<T, bool>::value) {
            this->*member = val.bool_value();
        }
    } else {
        this->*member = de; // Set the default value if the attribute is not found
    }
}

void CSICamera::reconfigure(const Dependencies deps, const ResourceConfig cfg) {
    if (debug) {
        std::cout << "Reconfiguring CSI Camera module" << std::endl;
    }
    stop_pipeline();
    auto attrs = cfg.attributes();
    init(attrs);
}

Camera::raw_image CSICamera::get_image(const std::string mime_type, const AttributeMap& extra) {
    if (debug) {
        std::cout << "hit get_image. expecting mime_type " << mime_type << std::endl;
    }
    raw_image image;
    image.mime_type = DEFAULT_OUTPUT_MIMETYPE;
    image.bytes = get_csi_image();
    if (image.bytes.empty()) {
        std::cerr << "ERROR: no bytes retrieved" << std::endl;
    }

    return image;
}

Camera::image_collection CSICamera::get_images() {
    std::cerr << "get_images not implemented" << std::endl;
    return image_collection{};
}

AttributeMap CSICamera::do_command(const AttributeMap command) {
    std::cerr << "do_command not implemented" << std::endl;
    return 0;
}

Camera::point_cloud CSICamera::get_point_cloud(const std::string mime_type, const AttributeMap& extra) {
    std::cerr << "get_point_cloud not implemented" << std::endl;
    return point_cloud{};
}

std::vector<GeometryConfig> CSICamera::get_geometries(const AttributeMap& extra) {
    std::cerr << "get_geometries not implemented" << std::endl;
    return std::vector<GeometryConfig>{};
}

Camera::properties CSICamera::get_properties() {
    std::cerr << "get_properties not implemented" << std::endl;
    return properties{};
}

void CSICamera::init_csi(const std::string pipeline_args) {
    // Build gst pipeline
    GError *error = nullptr;
    pipeline = gst_parse_launch(
        pipeline_args.c_str(),
        &error
    );
    if (!pipeline) {
        std::cerr << "Failed to create the pipeline" << std::endl;
        g_print("Error: %s\n", error->message);
        g_error_free(error);
        std::exit(EXIT_FAILURE);
    }

    // Print pipeline structure
    if (debug) {
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline_structure");
    }
    
    // Fetch the appsink element
    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink0");
    if (!appsink) {
        std::cerr << "Failed to get the appsink element" << std::endl;
        gst_object_unref(pipeline);
        std::exit(EXIT_FAILURE);
    }

    // Start the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start the pipeline" << std::endl;
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        std::exit(EXIT_FAILURE);
    }

    // Handle async pipeline creation
    wait_pipeline();

    // Run the main loop
    bus = gst_element_get_bus(pipeline);
    if (!bus) {
        std::cerr << "Failed to get the bus for the pipeline" << std::endl;
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        std::exit(EXIT_FAILURE);
    }
}

// Handles async GST state change
void CSICamera::wait_pipeline() {
    GstState state, pending;
    GstStateChangeReturn ret;

    // Set timeout for state change
    const int timeout_microseconds = GST_CHANGE_STATE_TIMEOUT * 1000000; // Convert seconds to microseconds
    auto start_time = std::chrono::high_resolution_clock::now();

    // Wait for state change to complete
    while ((ret = gst_element_get_state(pipeline, &state, &pending, GST_GET_STATE_TIMEOUT * GST_SECOND)) == GST_STATE_CHANGE_ASYNC) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - start_time).count();

        if (elapsed_time >= timeout_microseconds) {
            std::cerr << "Timeout: GST pipeline state change did not complete within timeout limit" << std::endl;
            std::exit(EXIT_FAILURE);
        }

        // Wait for a short duration to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (ret == GST_STATE_CHANGE_SUCCESS) {
        std::cout << "GST pipeline state change success" << std::endl;
    } else if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "GST pipeline failed to change state" << std::endl;
        std::exit(EXIT_FAILURE);
    } else if (ret = GST_STATE_CHANGE_NO_PREROLL) {
        std::cout << "GST pipeline changed but not enough data for preroll" << std::endl;
    } else {
        std::cerr << "GST pipeline failed to change state" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void CSICamera::stop_pipeline() {
    if (debug) {
        std::cout << "Stopping GST pipeline" << std::endl;
    }

    // Check if pipeline is defined
    if (pipeline == nullptr) {
        std::cout << "Pipeline is not defined" << std::endl;
        return;
    }

    // Stop the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to stop the pipeline" << std::endl;
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        std::exit(EXIT_FAILURE);
    }

    // Wait for async state change
    wait_pipeline();

    // Free resources
    gst_object_unref(appsink);
    gst_object_unref(pipeline);
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
            std::cerr << "Error: " << error->message << std::endl;
            std::cerr << "Debug Info: " << debugInfo << std::endl;
            stop_pipeline();
            std::exit(EXIT_FAILURE);
            break;
        case GST_MESSAGE_EOS:
            std::cout << "End of stream received" << std::endl;
            stop_pipeline();
            std::exit(EXIT_SUCCESS);
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(msg, &error, &debugInfo);
            if (debug) {
                std::cout << "Warning: " << error->message << std::endl;
                std::cout << "Debug Info: " << debugInfo << std::endl;
            }
            break;
        case GST_MESSAGE_INFO:
            gst_message_parse_info(msg, &error, &debugInfo);
            if (debug) {
                std::cout << "Info: " << error->message << std::endl;
                std::cout << "Debug Info: " << debugInfo << std::endl;
            }
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
    auto device_params = get_device_params(device);
    std::string input_sensor = (device.value == device_type::jetson) ? (" sensor-id="+video_path) : "";

    std::ostringstream oss;
    oss << device_params.input_source
        << input_sensor
        << " ! " << device_params.input_format
        << ",width=" << std::to_string(width_px)
        << ",height=" << std::to_string(height_px)
        << ",framerate=" << std::to_string(frame_rate) << "/1"
        << " ! " << device_params.flipper << " method=" << flip_method
        << " ! " << device_params.video_converter
        << " ! " << device_params.output_encoder
        << " ! " << "image/jpeg"
        << " ! appsink name=appsink0 sync=false max-buffers=1 drop=true";

    return oss.str();
}

std::vector<unsigned char> CSICamera::buff_to_vec(GstBuffer *buff) {
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

bool CSICamera::is_debug() const {
    return debug;
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
