#pragma once

#include <string>
#include <vector>

#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>

class CSIDiscovery : public viam::sdk::Discovery {
   public:
    explicit CSIDiscovery(std::string name);

    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    viam::sdk::ProtoStruct get_status() override;
};
