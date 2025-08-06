#include <iostream>
#include <gst/gst.h>

#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/module/module.hpp>
#include <viam/sdk/module/service.hpp>
#include <viam/sdk/resource/resource.hpp>
#include <viam/sdk/registry/registry.hpp>
#include <viam/sdk/components/camera.hpp>

#include "constraints.h"
#include "csi_camera.h"
#include "utils.h"

using namespace viam::sdk;

int main(int argc, char *argv[]) {
    std::cout << "### STARTING VIAM CSI CAMERA MODULE" << std::endl;

    // Gstreamer initialization
    gst_init(&argc, &argv);

    // Device type and params
    auto device = get_device_type();
    auto api_params = get_api_params(device);

    std::cout << "Device type: " << device.name << std::endl;
    std::cout << "API Namespace: " << api_params.api_namespace << std::endl;

    // Every Viam C++ SDK program must have one and only one Instance object
    // which is created before any other C++ SDK objects and stays alive until
    // all Viam C++ SDK objects are destroyed.
    Instance inst;

    auto module_registration = std::make_shared<ModelRegistration>(
    API::get<Camera>(),
    Model{api_params.api_namespace, api_params.api_type, api_params.api_subtype},
    [](Dependencies, ResourceConfig resource_config) -> std::shared_ptr<Resource> {
        return std::make_shared<CSICamera>(resource_config.name(), resource_config.attributes());
    });

    std::vector<std::shared_ptr<ModelRegistration>> mrs = { module_registration };
    auto module_service = std::make_shared<ModuleService>(argc, argv, mrs);

    module_service->serve();

    return EXIT_SUCCESS;
}
