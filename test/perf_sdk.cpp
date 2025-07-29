#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <viam/sdk/components/camera.hpp>
// #include <viam/sdk/components/camera/client.hpp>
#include <viam/sdk/robot/client.hpp>
#include <viam/sdk/robot/service.hpp>
#include <viam/sdk/rpc/dial.hpp>

namespace vs = ::viam::sdk;

int main() {
    // connect to robot
    std::string robot_address("localhost:8080");
    vs::Credentials credentials("", "");
    vs::DialOptions dial_options;
    dial_options.set_allow_insecure_downgrade(
            (credentials.type().empty() && credentials.payload().empty()));
    vs::Options options = vs::Options(1, dial_options);

    std::shared_ptr<vs::RobotClient> robot;
    try {
        robot = vs::RobotClient::at_address(robot_address, options);
        std::cout << "Successfully connected to the robot" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to the robot. Exiting." << std::endl;
        throw;
    }

    // fetch camera
    // ... WIP
    return EXIT_SUCCESS;
}
