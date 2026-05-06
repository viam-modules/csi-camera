#include "csi_discovery.h"

#include <viam/sdk/log/logging.hpp>

using namespace viam::sdk;

CSIDiscovery::CSIDiscovery(std::string name) : Discovery(std::move(name)) {
    VIAM_RESOURCE_LOG(debug) << "Creating CSIDiscovery";
}

std::vector<ResourceConfig> CSIDiscovery::discover_resources(const ProtoStruct& /* extra */) {
    VIAM_RESOURCE_LOG(debug) << "discover_resources called";
    return {};
}

ProtoStruct CSIDiscovery::do_command(const ProtoStruct& /* command */) {
    VIAM_RESOURCE_LOG(warn) << "do_command not implemented";
    return ProtoStruct{};
}

ProtoStruct CSIDiscovery::get_status() {
    return ProtoStruct{};
}
