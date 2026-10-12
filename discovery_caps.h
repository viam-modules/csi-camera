#pragma once

#include <gst/gst.h>

#include <string>
#include <vector>

// One discrete, concrete capture mode advertised by a Video/Source device.
struct CSIResolution {
    int width = 0;
    int height = 0;
    double frame_rate = 0.0;  // best (max) rate advertised for this size
    std::string format;       // raw format token, e.g. "NV12"; empty if unspecified

    bool operator==(const CSIResolution& o) const {
        return width == o.width && height == o.height && frame_rate == o.frame_rate && format == o.format;
    }
};

// Extract discrete resolutions (fixed width + height) from a device's caps.
// Structures with ranged/list dimensions are skipped (not concrete enough to
// become a component config); if skipped_out is non-null it receives that count.
std::vector<CSIResolution> parse_caps(const GstCaps* caps, int* skipped_out = nullptr);
