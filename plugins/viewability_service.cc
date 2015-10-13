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

        options_description goView("Go Viewabilitty service options");
        goView.add_options()
        ("goview-url", value<string>(&goViewUrl),
            "Base URL of the go viewability service");

        options_description commonOptions("Common Options");
        commonOptions.add_options()
        ("threads", value<unsigned>(&augmentorThreads)->default_value(Default::AugmentorThreads),
              "Number of augmentation threads to use");


        options_description allOptions;
        allOptions
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
    ViewabilityService::init()
    {
        augmentor = make_shared<ViewabilityAugmentor>(*this);    
        augmentor->init(config.augmentorThreads);

        if (!config.goViewUrl.empty()) {
            augmentor->useGoView(config.goViewUrl);
        }

    }

    void
    ViewabilityService::bindTcp(PortRange zmqRange, PortRange httpRange)
    {
    }

    void
    ViewabilityService::start()
    {
        augmentor->start();
    }

    void
    ViewabilityService::shutdown()
    {
        augmentor->shutdown();
    }

} // namespace JamLoop
