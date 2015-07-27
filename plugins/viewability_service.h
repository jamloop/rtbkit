/* viewability_service.h
   Mathieu Stefani, 24 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   The Viewability service
*/

#include <boost/program_options/options_description.hpp>
#include "soa/service/service_base.h"
#include "rtbkit/core/agent_configuration/agent_configuration_listener.h"
#include "viewability_augmentor.h"
#include "redis_viewability_backend.h"
#include "moat_data_parser.h"

namespace JamLoop {

class ViewabilityService : public Datacratic::ServiceBase, public Datacratic::MessageLoop {
public:

    ViewabilityService(std::shared_ptr<Datacratic::ServiceProxies> proxies, std::string serviceName);
    ViewabilityService(Datacratic::ServiceBase& parent, std::string serviceName);

    struct Config {
        std::string redisHost;
        uint16_t redisPort;

        std::string dataFile;

        boost::program_options::options_description
        makeProgramOptions();

    };

    void setConfig(const Config& config);
    void setBackend(const std::shared_ptr<ViewabilityBackend>& backend);


    void init();
    void bindTcp(Datacratic::PortRange zmqRange, Datacratic::PortRange httpRange);
    void start();
    void shutdown();

private:
    std::shared_ptr<ViewabilityAugmentor> augmentor;
    std::shared_ptr<ViewabilityBackend> backend;
    std::shared_ptr<RTBKIT::AgentConfigurationListener> agentConfig;

    std::shared_ptr<Utils::MoatDataParser> moat;

    Config config;

    void handleMoatData(std::vector<Utils::MoatDataParser::Line>&& data);

};

} // namespace JamLoop
