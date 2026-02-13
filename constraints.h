#pragma once

#include <string>

// Module
constexpr const char* DEFAULT_SOCKET_PATH = "/tmp/viam.csi.sock";
constexpr const char* RESOURCE_TYPE = "CSICamera";
constexpr const char* API_NAMESPACE = "viam";
constexpr const char* API_TYPE = "camera";
constexpr const char* DEFAULT_API_SUBTYPE = "csi";

// GST
constexpr int GST_GET_STATE_TIMEOUT = 1;
constexpr int GST_CHANGE_STATE_TIMEOUT = 5;

// Pipeline
constexpr const char* DEFAULT_INPUT_SOURCE = "libcamerasrc";
constexpr const char* DEFAULT_INPUT_SENSOR = "0";
constexpr const char* DEFAULT_INPUT_FORMAT = "video/x-raw";
constexpr int DEFAULT_INPUT_WIDTH = 1920;
constexpr int DEFAULT_INPUT_HEIGHT = 1080;
constexpr int DEFAULT_INPUT_FRAMERATE = 30;
constexpr const char* DEFAULT_VIDEO_CONVERTER = "videoconvert";
constexpr const char* DEFAULT_OUTPUT_ENCODER = "nvjpegenc";
constexpr const char* DEFAULT_OUTPUT_MIMETYPE = "image/jpeg";

// Jetson
constexpr const char* JETSON_API_SUBTYPE = "csi";
constexpr const char* JETSON_INPUT_SOURCE = "nvarguscamerasrc";
constexpr const char* JETSON_INPUT_FORMAT = "video/x-raw(memory:NVMM)";
constexpr const char* JETSON_VIDEO_CONVERTER = "nvvidconv";
constexpr const char* JETSON_OUTPUT_ENCODER = "nvjpegenc";

// Pi
constexpr const char* PI_API_SUBTYPE = "csi-pi";
constexpr const char* PI_INPUT_SOURCE = "libcamerasrc";
constexpr const char* PI_INPUT_FORMAT = "video/x-raw,format=NV12";
constexpr const char* PI_VIDEO_CONVERTER = "videoconvert";
constexpr const char* PI_OUTPUT_ENCODER = "jpegenc";

// Integration Tests
inline const std::string TEST_GST_PIPELINE =
    "videotestsrc ! video/x-raw ! videoconvert ! jpegenc ! image/jpeg ! appsink name=appsink0 sync=false max-buffers=1 drop=true";
