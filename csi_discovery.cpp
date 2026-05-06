#include "csi_discovery.h"

#include <string>

#include <gst/gst.h>

#include <viam/sdk/log/logging.hpp>

#include "constraints.h"
#include "utils.h"

using namespace viam::sdk;

CSIDiscovery::CSIDiscovery(std::string name) : Discovery(std::move(name)) {
    VIAM_RESOURCE_LOG(debug) << "Creating CSIDiscovery";
}

std::vector<ResourceConfig> CSIDiscovery::discover_resources(const ProtoStruct& /* extra */) {
    VIAM_RESOURCE_LOG(debug) << "discover_resources called";

    std::vector<ResourceConfig> out;

    auto board = get_device_type();
    const std::string subtype = (board.value == device_type::pi) ? PI_API_SUBTYPE : JETSON_API_SUBTYPE;

    GstDeviceMonitor* monitor = gst_device_monitor_new();
    GstCaps* raw = gst_caps_new_empty_simple("video/x-raw");
    gst_device_monitor_add_filter(monitor, "Video/Source", raw);
    gst_caps_unref(raw);
    GstCaps* nvmm = gst_caps_from_string("video/x-raw(memory:NVMM)");
    gst_device_monitor_add_filter(monitor, "Video/Source", nvmm);
    gst_caps_unref(nvmm);

    if (!gst_device_monitor_start(monitor)) {
        VIAM_RESOURCE_LOG(error) << "GstDeviceMonitor failed to start; returning no configs";
        gst_object_unref(monitor);
        return out;
    }

    GList* devices = gst_device_monitor_get_devices(monitor);
    int idx = 0;
    for (GList* it = devices; it != nullptr; it = it->next, idx++) {
        GstDevice* dev = GST_DEVICE(it->data);
        gchar* name = gst_device_get_display_name(dev);
        GstCaps* caps = gst_device_get_caps(dev);

        int width = DEFAULT_INPUT_WIDTH;
        int height = DEFAULT_INPUT_HEIGHT;
        int fps = DEFAULT_INPUT_FRAMERATE;

        if (caps != nullptr && gst_caps_get_size(caps) > 0) {
            GstStructure* s = gst_caps_get_structure(caps, 0);
            gst_structure_get_int(s, "width", &width);
            gst_structure_get_int(s, "height", &height);
            int fps_n = 0, fps_d = 0;
            if (gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) && fps_d > 0) {
                fps = fps_n / fps_d;
            }
        }

        VIAM_RESOURCE_LOG(info) << "Discovered camera " << idx << " (" << (name ? name : "?") << "): " << width << "x" << height << "@"
                                << fps;

        ProtoStruct attrs{
            {"width_px", static_cast<double>(width)},
            {"height_px", static_cast<double>(height)},
            {"frame_rate", static_cast<double>(fps)},
        };
        if (board.value == device_type::jetson) {
            attrs["video_path"] = std::to_string(idx);
        }

        const std::string config_name =
            "csi" + std::to_string(idx) + "_" + std::to_string(width) + "x" + std::to_string(height) + "_" + std::to_string(fps);

        out.emplace_back("camera", config_name, "rdk", attrs, "rdk:component:camera", Model("viam", "camera", subtype));

        if (caps != nullptr)
            gst_caps_unref(caps);
        if (name != nullptr)
            g_free(name);
    }

    g_list_free_full(devices, gst_object_unref);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    return out;
}

ProtoStruct CSIDiscovery::do_command(const ProtoStruct& /* command */) {
    VIAM_RESOURCE_LOG(warn) << "do_command not implemented";
    return ProtoStruct{};
}

ProtoStruct CSIDiscovery::get_status() {
    return ProtoStruct{};
}
