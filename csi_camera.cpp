#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "constraints.h"
#include "csi_camera.h"

using namespace viam::sdk;

CSICamera::CSICamera(const std::string name, const ProtoStruct& attrs) : Camera(std::move(name)) {
    device = get_device_type();
    VIAM_RESOURCE_LOG(debug) << "Creating CSICamera";
    VIAM_RESOURCE_LOG(debug) << "Device type: " << device.name;
    init(attrs);
}

CSICamera::~CSICamera() {
    VIAM_RESOURCE_LOG(debug) << "Destroying CSICamera";
    stop_pipeline();
}

void CSICamera::init(const ProtoStruct& attrs) {
    validate_attrs(attrs);
    auto pipeline_args = create_pipeline();
    VIAM_RESOURCE_LOG(debug) << "pipeline_args: " << pipeline_args;
    init_csi(pipeline_args);
}

void CSICamera::validate_attrs(const ProtoStruct& attrs) {
    set_attr<int>(attrs, "width_px", &CSICamera::width_px, DEFAULT_INPUT_WIDTH);
    set_attr<int>(attrs, "height_px", &CSICamera::height_px, DEFAULT_INPUT_HEIGHT);
    set_attr<int>(attrs, "frame_rate", &CSICamera::frame_rate, DEFAULT_INPUT_FRAMERATE);
    set_attr<std::string>(attrs, "video_path", &CSICamera::video_path, DEFAULT_INPUT_SENSOR);

    auto require_positive = [](const std::string& name, int value) {
        if (value <= 0) {
            throw Exception("attribute \"" + name + "\" must be a positive integer, got " + std::to_string(value));
        }
    };
    require_positive("width_px", width_px);
    require_positive("height_px", height_px);
    require_positive("frame_rate", frame_rate);
}

namespace {

template <typename T>
struct get_as_type {
    using type = T;
};

template <>
struct get_as_type<int> {
    using type = double;
};

struct gst_object_deleter {
    void operator()(gpointer p) const {
        gst_object_unref(p);
    }
};

struct gst_message_deleter {
    void operator()(GstMessage* msg) const {
        gst_message_unref(msg);
    }
};

using gst_bus_ptr = std::unique_ptr<GstBus, gst_object_deleter>;
using gst_message_ptr = std::unique_ptr<GstMessage, gst_message_deleter>;

}  // namespace

template <typename T>
void CSICamera::set_attr(const ProtoStruct& attrs, const std::string& name, T CSICamera::* member, T de) {
    if (attrs.count(name) != 1) {
        this->*member = de;
        return;
    }
    if (const auto* val = attrs.at(name).get<typename get_as_type<T>::type>()) {
        this->*member = static_cast<T>(*val);
    } else {
        throw Exception("unexpected value type for attribute " + name);
    }
}

Camera::image_collection CSICamera::get_images(std::vector<std::string> /* filter_source_names */, const ProtoStruct& /* extra */) {
    auto frame = get_latest_frame();

    raw_image image;
    image.mime_type = DEFAULT_OUTPUT_MIMETYPE;
    image.bytes = *frame.bytes;
    if (image.bytes.empty()) {
        throw Exception("no bytes retrieved from latest frame");
    }
    image.source_name = "";

    image_collection collection;
    collection.images = std::vector<raw_image>{std::move(image)};
    auto duration_since_epoch = frame.captured_at.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch);
    collection.metadata.captured_at = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>(nanoseconds);

    return collection;
}

ProtoStruct CSICamera::do_command(const ProtoStruct& command) {
    VIAM_RESOURCE_LOG(warn) << "do_command not implemented";
    return ProtoStruct{};
}

Camera::point_cloud CSICamera::get_point_cloud(const std::string mime_type, const ProtoStruct& extra) {
    VIAM_RESOURCE_LOG(warn) << "get_point_cloud not implemented";
    return point_cloud{};
}

std::vector<GeometryConfig> CSICamera::get_geometries(const ProtoStruct& extra) {
    VIAM_RESOURCE_LOG(warn) << "get_geometries not implemented";
    return std::vector<GeometryConfig>{};
}

Camera::properties CSICamera::get_properties() {
    Camera::properties p{};
    p.supports_pcd = false;
    p.intrinsic_parameters.width_px = width_px;
    p.intrinsic_parameters.height_px = height_px;
    return p;
}

ProtoStruct CSICamera::get_status() {
    return ProtoStruct{};
}

void CSICamera::init_csi(const std::string pipeline_args) {
    // Build gst pipeline
    GError* error = nullptr;
    pipeline = gst_parse_launch(pipeline_args.c_str(), &error);
    if (!pipeline) {
        VIAM_RESOURCE_LOG(error) << "Failed to create the pipeline: " << error->message;
        g_error_free(error);
        throw Exception("Failed to create the pipeline");
    }

    // Fetch the appsink element
    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "appsink0");
    if (!appsink) {
        fail_pipeline("Failed to get the appsink element");
    }

    // Store every frame in the latest-frame cache as it arrives, so that
    // concurrent consumers read the cache instead of competing for samples
    g_object_set(G_OBJECT(appsink), "emit-signals", TRUE, nullptr);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(&CSICamera::on_new_sample), this);

    // Start the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        fail_pipeline("Failed to start the pipeline");
    }

    // Handle async pipeline creation
    try {
        wait_pipeline();
    } catch (const std::exception& e) {
        fail_pipeline(e.what());
    }

    bus = gst_element_get_bus(pipeline);
    if (!bus) {
        fail_pipeline("Failed to get the bus for the pipeline");
    }
}

std::string CSICamera::mode_hint() const {
    std::string hint = "configured mode " + std::to_string(width_px) + "x" + std::to_string(height_px) + "@" + std::to_string(frame_rate) +
                       "fps on " + device.name;
    const auto modes_hint = get_device_params(device).modes_hint;
    if (!modes_hint.empty()) {
        hint += "; " + modes_hint;
    }
    return hint;
}

std::string CSICamera::drain_bus_errors() {
    std::string errors;
    if (pipeline == nullptr) {
        return errors;
    }
    gst_bus_ptr pipeline_bus(gst_element_get_bus(pipeline));
    if (pipeline_bus == nullptr) {
        return errors;
    }
    while (gst_message_ptr msg{gst_bus_pop_filtered(pipeline_bus.get(), GST_MESSAGE_ERROR)}) {
        GError* error = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_error(msg.get(), &error, &debug_info);
        VIAM_RESOURCE_LOG(debug) << "Debug Info: " << (debug_info ? debug_info : "");
        if (!errors.empty()) {
            errors += "; ";
        }
        errors += std::string(GST_MESSAGE_SRC_NAME(msg.get())) + ": " + error->message;
        g_error_free(error);
        g_free(debug_info);
    }
    return errors;
}

void CSICamera::fail_pipeline(const std::string& what) {
    std::string msg = what;
    const auto errors = drain_bus_errors();
    if (!errors.empty()) {
        msg += ": " + errors;
    }
    msg += " (" + mode_hint() + ")";
    stop_pipeline();
    throw Exception(msg);
}

// Handles async GST state change. Throws std exception since it will be caught
// and rethrown as sdk::Exception
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
            throw std::runtime_error("Timeout: GST pipeline state change did not complete within timeout limit");
        }

        // Wait for a short duration to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (ret == GST_STATE_CHANGE_SUCCESS) {
        VIAM_RESOURCE_LOG(debug) << "GST pipeline state change success";
    } else if (ret == GST_STATE_CHANGE_FAILURE) {
        VIAM_RESOURCE_LOG(error) << "GST pipeline failed to change state";
        throw std::runtime_error("GST pipeline failed to change state");
    } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
        VIAM_RESOURCE_LOG(warn) << "GST pipeline changed but not enough data for preroll";
    } else {
        VIAM_RESOURCE_LOG(error) << "GST pipeline failed to change state";
        throw std::runtime_error("GST pipeline failed to change state");
    }
}

void CSICamera::stop_pipeline() {
    VIAM_RESOURCE_LOG(debug) << "Stopping GST pipeline";

    // Check if pipeline is defined
    if (pipeline == nullptr) {
        VIAM_RESOURCE_LOG(error) << "Pipeline is not defined";
        return;
    }

    // Stop the pipeline
    if (gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        // Don't throw, continue cleanup
        VIAM_RESOURCE_LOG(error) << "Failed to stop the pipeline";
    }

    // Wait for async state change
    try {
        wait_pipeline();
    } catch (const std::exception& e) {
        // Don't throw, continue cleanup
        VIAM_RESOURCE_LOG(error) << "Exception during wait_pipeline: " << e.what();
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

// Handles every message queued on the bus; errors come after any warnings
// the source posted first, so one pop per call would delay reporting them
void CSICamera::check_bus() {
    while (gst_message_ptr msg{gst_bus_pop(bus)}) {
        catch_pipeline(msg.get());
    }
}

void CSICamera::catch_pipeline(GstMessage* msg) {
    if (msg == nullptr) {
        VIAM_RESOURCE_LOG(debug) << "catch_pipeline called with null message";
        return;
    }

    GError* error = nullptr;
    gchar* debugInfo = nullptr;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            gst_message_parse_error(msg, &error, &debugInfo);
            VIAM_RESOURCE_LOG(debug) << "Debug Info: " << debugInfo;
            std::string err_msg = std::string(GST_MESSAGE_SRC_NAME(msg)) + ": " + error->message;
            g_error_free(error);
            g_free(debugInfo);
            stop_pipeline();
            throw Exception("GST pipeline error from " + err_msg + " (" + mode_hint() + ")");
        }
        case GST_MESSAGE_EOS:
            VIAM_RESOURCE_LOG(debug) << "End of stream received, stopping pipeline";
            stop_pipeline();
            throw Exception("End of stream received, pipeline stopped");
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(msg, &error, &debugInfo);
            VIAM_RESOURCE_LOG(warn) << "Warning: " << error->message;
            VIAM_RESOURCE_LOG(warn) << "Debug Info: " << debugInfo;
            break;
        case GST_MESSAGE_INFO:
            gst_message_parse_info(msg, &error, &debugInfo);
            VIAM_RESOURCE_LOG(info) << "Info: " << error->message;
            VIAM_RESOURCE_LOG(info) << "Debug Info: " << debugInfo;
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

GstFlowReturn CSICamera::on_new_sample(GstAppSink* /* sink */, gpointer user_data) {
    return static_cast<CSICamera*>(user_data)->handle_new_sample();
}

GstFlowReturn CSICamera::handle_new_sample() {
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (sample == nullptr) {
        // appsink is flushing or reached EOS
        return GST_FLOW_OK;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (buffer != nullptr) {
        // Must not throw across the GStreamer C callback boundary
        try {
            auto bytes = std::make_shared<const std::vector<unsigned char>>(buff_to_vec(buffer));
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                latest_frame = std::move(bytes);
                latest_frame_time = std::chrono::system_clock::now();
            }
            frame_cv.notify_all();
        } catch (const std::exception& e) {
            VIAM_RESOURCE_LOG(error) << "Failed to cache frame from appsink: " << e.what();
        }
    } else {
        VIAM_RESOURCE_LOG(warn) << "Failed to get buffer from sample";
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

std::chrono::milliseconds CSICamera::max_frame_age() const {
    // Frames are expected every 1000/frame_rate ms; tolerate a few missed
    // intervals before declaring the pipeline stalled, but never less than 1s
    const int interval_ms = (frame_rate > 0) ? (1000 / frame_rate) : 1000;
    return std::chrono::milliseconds(std::max(3 * interval_ms, 1000));
}

CSICamera::cached_frame CSICamera::get_latest_frame() {
    if (pipeline == nullptr || bus == nullptr) {
        throw Exception("GST pipeline is not running");
    }

    check_bus();

    const auto max_age = max_frame_age();
    std::unique_lock<std::mutex> lock(frame_mutex);
    if (latest_frame == nullptr) {
        // No frame has arrived since the pipeline started; give the source
        // extra time to deliver its first frame
        const auto first_frame_timeout = std::max(max_age, std::chrono::milliseconds(GST_CHANGE_STATE_TIMEOUT * 1000));
        if (!frame_cv.wait_for(lock, first_frame_timeout, [this] { return latest_frame != nullptr; })) {
            // A source that rejected the configured mode (e.g. nvarguscamerasrc) reports it on the bus
            // during the wait, so prefer that error. Unlock first: check_bus may stop the pipeline,
            // which joins the streaming thread that takes frame_mutex.
            lock.unlock();
            check_bus();
            throw Exception("timed out waiting for first frame from GST pipeline (" + mode_hint() + ")");
        }
    }

    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - latest_frame_time);
    if (age > max_age) {
        throw Exception("latest frame is stale (" + std::to_string(age.count()) + "ms old): GST pipeline may have stalled");
    }

    return cached_frame{latest_frame, latest_frame_time};
}

std::vector<unsigned char> CSICamera::get_csi_image() {
    return *get_latest_frame().bytes;
}

std::string CSICamera::create_pipeline() const {
    const char* test_mode = std::getenv("VIAM_CSI_TEST_MODE");
    if (test_mode != nullptr && std::string(test_mode) == "1") {
        VIAM_RESOURCE_LOG(warn) << "CI Test mode enabled";
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
    if (buff == nullptr) {
        throw Exception("Cannot convert null buffer to vector");
    }

    // Get the size of the buffer
    size_t bufferSize = gst_buffer_get_size(buff);

    // Create a vector with the same size as the buffer
    std::vector<unsigned char> vec(bufferSize);

    // Copy the buffer data to the vector
    GstMapInfo map;
    gst_buffer_map(buff, &map, GST_MAP_READ);
    memcpy(vec.data(), map.data, bufferSize);
    gst_buffer_unmap(buff, &map);

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
