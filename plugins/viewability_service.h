/* viewability_service.h
   Mathieu Stefani, 24 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   The Viewability service
*/

#include <boost/program_options/options_description.hpp>
#include "soa/service/service_base.h"
#include "rtbkit/core/agent_configuration/agent_configuration_listener.h"
#include "viewability_augmentor.h"

namespace JamLoop {

class ViewabilityService : public Datacratic::ServiceBase {
public:

    ViewabilityService(std::shared_ptr<Datacratic::ServiceProxies> proxies, std::string serviceName);
    ViewabilityService(Datacratic::ServiceBase& parent, std::string serviceName);

    struct Config {
        boost::program_options::options_description
        makeProgramOptions();

        unsigned augmentorThreads;
        std::string goViewUrl;

    };

    void setConfig(const Config& config);

    void init();
    void bindTcp(Datacratic::PortRange zmqRange, Datacratic::PortRange httpRange);
    void start();
    void shutdown();

private:
    std::shared_ptr<ViewabilityAugmentor> augmentor;

    Config config;
};

} // namespace JamLoop
