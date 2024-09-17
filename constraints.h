#pragma once

// Module
#define DEFAULT_SOCKET_PATH "/tmp/viam.csi.sock"
#define RESOURCE_TYPE "CSICamera"
#define API_NAMESPACE "viam"
#define API_TYPE "camera"
#define DEFAULT_API_SUBTYPE "csi"

// GST
#define GST_GET_STATE_TIMEOUT 1 
#define GST_CHANGE_STATE_TIMEOUT 5

// Pipeline
#define DEFAULT_INPUT_SOURCE "libcamerasrc"
#define DEFAULT_INPUT_SENSOR "0"
#define DEFAULT_INPUT_FORMAT "video/x-raw"
#define DEFAULT_INPUT_WIDTH 1920
#define DEFAULT_INPUT_HEIGHT 1080
#define DEFAULT_INPUT_FRAMERATE 30
#define DEFAULT_VIDEO_CONVERTER "videoconvert"
#define DEFAULT_OUTPUT_ENCODER "nvjpegenc"
#define DEFAULT_OUTPUT_MIMETYPE "image/jpeg"
#define DEFAULT_FLIP "videoflip"
#define DEFAULT_FLIP_METHOD "none"

// Jetson
#define JETSON_API_SUBTYPE "csi"
#define JETSON_INPUT_SOURCE "nvarguscamerasrc"
#define JETSON_INPUT_FORMAT "video/x-raw(memory:NVMM)"
#define JETSON_VIDEO_CONVERTER "nvvidconv"
#define JETSON_OUTPUT_ENCODER "nvjpegenc"

// Pi
#define PI_API_SUBTYPE "csi-pi"
#define PI_INPUT_SOURCE "libcamerasrc"
#define PI_INPUT_FORMAT "video/x-raw"
#define PI_VIDEO_CONVERTER "videoconvert"
#define PI_OUTPUT_ENCODER "jpegenc"
