#include <algorithm>
#include <fstream>

#include "utils.h"

device_type get_device_type() {
    // Check for environment variable override
    const char* env_device = std::getenv("VIAM_CSI_DEVICE");
    if (env_device != nullptr) {
        std::string device_str(env_device);
        std::transform(device_str.begin(), device_str.end(), device_str.begin(), ::tolower);

        if (device_str == "jetson") {
            return device_type(device_type::jetson, "Jetson");
        } else if (device_str == "pi") {
            return device_type(device_type::pi, "Raspberry Pi");
        } else if (device_str == "test") {
            return device_type(device_type::test, "Test");
        }
    }

    std::ifstream device_name(DEVICE_PATH);
    if (device_name.is_open()) {
        std::string line;
        while (std::getline(device_name, line)) {
            std::string lowercase_line = line;
            std::transform(lowercase_line.begin(), lowercase_line.end(), lowercase_line.begin(), ::tolower);
            // Check for specific terms in a case-insensitive manner
            if (lowercase_line.find("nvidia") != std::string::npos &&
                (lowercase_line.find("orin") != std::string::npos || lowercase_line.find("nano") != std::string::npos ||
                 lowercase_line.find("agx") != std::string::npos || lowercase_line.find("jetson") != std::string::npos)) {
                return device_type(device_type::jetson, "Jetson");
            } else if (lowercase_line.find("raspberry") != std::string::npos && lowercase_line.find("pi") != std::string::npos) {
                return device_type(device_type::pi, "Raspberry Pi");
            }
        }
        device_name.close();
    }

    return device_type(device_type::unknown, "unkwnown");
}

device_params get_device_params(device_type device) {
    switch (device.value) {
        case device_type::jetson:
            return device_params{.input_source = JETSON_INPUT_SOURCE,
                                 .input_format = JETSON_INPUT_FORMAT,
                                 .video_converter = JETSON_VIDEO_CONVERTER,
                                 .output_encoder = JETSON_OUTPUT_ENCODER};
        case device_type::pi:
            return device_params{.input_source = PI_INPUT_SOURCE,
                                 .input_format = PI_INPUT_FORMAT,
                                 .video_converter = PI_VIDEO_CONVERTER,
                                 .output_encoder = PI_OUTPUT_ENCODER};
        case device_type::test:
            // Return empty params for test mode - pipeline will be overridden
            return device_params{.input_source = "", .input_format = "", .video_converter = "", .output_encoder = ""};
        default:
            return device_params{.input_source = DEFAULT_INPUT_SOURCE,
                                 .input_format = DEFAULT_INPUT_FORMAT,
                                 .video_converter = DEFAULT_VIDEO_CONVERTER,
                                 .output_encoder = DEFAULT_OUTPUT_ENCODER};
    }
}

api_params get_api_params(device_type device) {
    switch (device.value) {
        case device_type::jetson:
            return api_params{.api_namespace = API_NAMESPACE, .api_type = API_TYPE, .api_subtype = JETSON_API_SUBTYPE};
        case device_type::pi:
            return api_params{.api_namespace = API_NAMESPACE, .api_type = API_TYPE, .api_subtype = PI_API_SUBTYPE};
        default:
            return api_params{.api_namespace = API_NAMESPACE, .api_type = API_TYPE, .api_subtype = DEFAULT_API_SUBTYPE};
    }
}
