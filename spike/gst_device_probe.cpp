// Probe tool: enumerate Video/Source devices and their caps via GstDeviceMonitor.
// Useful for checking, on a given board, whether cameras surface through the
// gstreamer device monitor, whether their advertised caps are discrete
// resolutions or ranges, and which device properties could serve as a stable id.

#include <gst/gst.h>

#include <iostream>
#include <string>

#include "../discovery_caps.h"

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);

    GstDeviceMonitor* monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);

    if (!gst_device_monitor_start(monitor)) {
        std::cerr << "ERROR: failed to start GstDeviceMonitor\n";
        gst_object_unref(monitor);
        return 1;
    }

    GList* devices = gst_device_monitor_get_devices(monitor);
    if (!devices) {
        std::cout << "No Video/Source devices found via GstDeviceMonitor.\n"
                  << "(A source element that exposes no device provider will not "
                     "appear here.)\n";
    }

    int idx = 0;
    for (GList* it = devices; it != nullptr; it = it->next, idx++) {
        GstDevice* dev = GST_DEVICE(it->data);

        gchar* name = gst_device_get_display_name(dev);
        gchar* klass = gst_device_get_device_class(dev);
        std::cout << "=== device " << idx << ": " << (name ? name : "?") << " [" << (klass ? klass : "?")
                  << "] ===\n";
        g_free(name);
        g_free(klass);

        // Properties carry device-specific stable ids (e.g. object.path,
        // device.path, api.* fields) worth inspecting as an identifier.
        if (GstStructure* props = gst_device_get_properties(dev)) {
            gchar* ps = gst_structure_to_string(props);
            std::cout << "  properties: " << (ps ? ps : "") << "\n";
            g_free(ps);
            gst_structure_free(props);
        }

        if (GstCaps* caps = gst_device_get_caps(dev)) {
            gchar* cs = gst_caps_to_string(caps);
            std::cout << "  raw caps: " << (cs ? cs : "") << "\n";
            g_free(cs);

            int skipped = 0;
            const auto res = parse_caps(caps, &skipped);
            std::cout << "  discrete resolutions: " << res.size()
                      << " (skipped non-fixed structures: " << skipped << ")\n";
            for (const auto& r : res) {
                std::cout << "    " << r.width << "x" << r.height << " @ " << r.frame_rate << "fps"
                          << (r.format.empty() ? "" : (" " + r.format)) << "\n";
            }
            gst_caps_unref(caps);
        }
        gst_object_unref(dev);
    }
    g_list_free(devices);

    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);
    return 0;
}
