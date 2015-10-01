#include <boost/program_options/options_description.hpp>
#include "viewability_service.h"

using namespace Datacratic;
using namespace RTBKIT;
using namespace std;

namespace JamLoop {

    namespace Default {
        static constexpr int AugmentorThreads = 4;
    }

    boost::program_options::options_description
    ViewabilityService::Config::makeProgramOptions()
    {
        using namespace boost::program_options;
        options_description redisOptions("Backend Redis Options");

        redisOptions.add_options()
        ("redis-host", value<string>(&redisHost),
                "location of the redis server")
        ("redis-port", value<uint16_t>(&redisPort),
                "port of the redis server");

        options_description goView("Go Viewabilitty service options");
        goView.add_options()
        ("goview-url", value<string>(&goViewUrl),
            "Base URL of the go viewability service");

        options_description commonOptions("Common Options");
        commonOptions.add_options()
        ("data-file", value<string>(&dataFile),
              "location of the MOAT data file");


        options_description allOptions;
        allOptions
            .add(redisOptions)
            .add(goView)
            .add(commonOptions);

        return allOptions;
    }


    ViewabilityService::
    ViewabilityService(shared_ptr<ServiceProxies> proxies, std::string serviceName)
       : ServiceBase(std::move(serviceName), std::move(proxies))
    {
    }

    ViewabilityService::
    ViewabilityService(ServiceBase& parent, std::string serviceName)
        : ServiceBase(std::move(serviceName), parent)
    {
    }

    void
    ViewabilityService::setConfig(const Config& config) {
        this->config = config;
    }

    void
    ViewabilityService::setBackend(const shared_ptr<ViewabilityBackend>& backend)
    {
        this->backend = backend;
    }

    void
    ViewabilityService::init()
    {
        agentConfig = make_shared<AgentConfigurationListener>(getZmqContext());
        agentConfig->init(getServices()->config);
        addSource("ViewabilityService::agentConfig", agentConfig);

        augmentor = make_shared<ViewabilityAugmentor>(*this);    
        augmentor->init(Default::AugmentorThreads);

        if (!config.goViewUrl.empty()) {
            augmentor->useGoView(config.goViewUrl);
        }

#if 0
        moat = make_shared<Utils::MoatDataParser>(config.dataFile,
                [this](std::vector<Utils::MoatDataParser::Line>&& data) {
                    this->handleMoatData(std::move(data));
                });
        addSource("ViewabilityService::moatData", moat);
#endif

    }

    void
    ViewabilityService::bindTcp(PortRange zmqRange, PortRange httpRange)
    {
    }

    void
    ViewabilityService::start()
    {
        MessageLoop::start();
        augmentor->start();
    }

    void
    ViewabilityService::shutdown()
    {
        MessageLoop::shutdown();
        augmentor->shutdown();
    }

    void
    ViewabilityService::handleMoatData(std::vector<Utils::MoatDataParser::Line>&& data)
    {
    }
} // namespace JamLoop
