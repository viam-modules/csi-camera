#include "discovery_caps.h"

#include <algorithm>

namespace {

// Best-effort max framerate from a "framerate" field that may be a fixed
// fraction, a fraction range, or a list of fractions. Returns 0 if absent.
double max_framerate(const GstStructure* s) {
    const GValue* v = gst_structure_get_value(s, "framerate");
    if (!v) {
        return 0.0;
    }
    const auto frac_to_d = [](const GValue* f) -> double {
        const int num = gst_value_get_fraction_numerator(f);
        const int den = gst_value_get_fraction_denominator(f);
        return den ? static_cast<double>(num) / den : 0.0;
    };
    if (GST_VALUE_HOLDS_FRACTION(v)) {
        return frac_to_d(v);
    }
    if (GST_VALUE_HOLDS_FRACTION_RANGE(v)) {
        return frac_to_d(gst_value_get_fraction_range_max(v));
    }
    if (GST_VALUE_HOLDS_LIST(v)) {
        double best = 0.0;
        for (guint i = 0; i < gst_value_list_get_size(v); i++) {
            const GValue* f = gst_value_list_get_value(v, i);
            if (GST_VALUE_HOLDS_FRACTION(f)) {
                best = std::max(best, frac_to_d(f));
            }
        }
        return best;
    }
    return 0.0;
}

std::string format_token(const GstStructure* s) {
    const GValue* v = gst_structure_get_value(s, "format");
    if (v && G_VALUE_HOLDS_STRING(v)) {
        return g_value_get_string(v);
    }
    // list-of-formats or absent: left empty for the spike
    return "";
}

}  // namespace

std::vector<CSIResolution> parse_caps(const GstCaps* caps, int* skipped_out) {
    std::vector<CSIResolution> out;
    int skipped = 0;
    if (!caps) {
        if (skipped_out) {
            *skipped_out = 0;
        }
        return out;
    }

    const guint n = gst_caps_get_size(caps);
    for (guint i = 0; i < n; i++) {
        // gst_caps_get_structure takes a non-const GstCaps*; we only read it.
        const GstStructure* s = gst_caps_get_structure(const_cast<GstCaps*>(caps), i);
        int w = 0;
        int h = 0;
        // gst_structure_get_int returns FALSE for ranged/list dimensions, so
        // only genuinely fixed width+height survive -> a concrete resolution.
        if (!gst_structure_get_int(s, "width", &w) || !gst_structure_get_int(s, "height", &h)) {
            skipped++;
            continue;
        }
        CSIResolution r;
        r.width = w;
        r.height = h;
        r.frame_rate = max_framerate(s);
        r.format = format_token(s);
        out.push_back(r);
    }

    std::sort(out.begin(), out.end(), [](const CSIResolution& a, const CSIResolution& b) {
        if (a.width != b.width) return a.width < b.width;
        if (a.height != b.height) return a.height < b.height;
        if (a.frame_rate != b.frame_rate) return a.frame_rate < b.frame_rate;
        return a.format < b.format;
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());

    if (skipped_out) {
        *skipped_out = skipped;
    }
    return out;
}
