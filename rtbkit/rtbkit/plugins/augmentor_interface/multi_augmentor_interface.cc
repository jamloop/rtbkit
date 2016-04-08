/* multi_augmentor_intercace.cc
   Mathieu Stefani, 08 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the multi augmentor interface
*/

#include "multi_augmentor_interface.h"

using namespace Datacratic;

namespace RTBKIT {

MultiAugmentorInterface::MultiAugmentorInterface(
        std::shared_ptr<ServiceProxies> proxies,
        const std::string& serviceName,
        const Json::Value& config)
    : AugmentorInterface(std::move(proxies), serviceName)
{
    auto interfacesConfig = config["interfaces"];
    for (auto it = interfacesConfig.begin(), end = interfacesConfig.end(); it != end; ++it) {
         const std::string interfaceName = it.memberName();
         std::shared_ptr<AugmentorInterface> interface(AugmentorInterface::create(interfaceName + ".augmentor", getServices(), *it));
         interfaces.insert(
                 std::make_pair(std::move(interfaceName), std::move(interface)));
    }
}

void
MultiAugmentorInterface::init() {
    AugmentorInterface::init();

    for (const auto& interface: interfaces) {
        interface.second->init();
    }
}

void
MultiAugmentorInterface::bindTcp(const Datacratic::PortRange& range) {
    for (const auto& interface: interfaces) {
        interface.second->bindTcp(range);
    }
}

void
MultiAugmentorInterface::doSendAugmentMessage(
        const std::shared_ptr<AugmentorInstanceInfo>& instance,
        const std::string& augmentorName,
        const std::shared_ptr<Auction>& auction,
        const std::set<std::string>& agents,
        const std::map<std::string, std::vector<AugmentationConfig>>& configs,
        Datacratic::Date date)
{
    typedef std::unordered_map<std::shared_ptr<AugmentorInterface>, std::set<std::string>> Aggregate;
    Aggregate aggregate;

    for (const auto& agent: agents) {
        auto augIt = configs.find(agent);
        if (augIt == std::end(configs))
            throw ML::Exception("No augmentation config found for agent '%s'", agent.c_str());

        const auto& augConfigs = augIt->second;
        for (const auto& augConfig: augConfigs) {
            auto interface = augConfig.interface;
            auto it = interfaces.find(interface);
            if (it == std::end(interfaces)) {
                throw ML::Exception("Unknown augmentor interface '%s' for agent '%s' (augmentor '%s')", interface.c_str(), agent.c_str(), augConfig.name.c_str());
            }

            aggregate[it->second].insert(agent);
        }
    }

    for (const auto& iface: aggregate) {
        iface.first->sendAugmentMessage(instance, augmentorName, auction, iface.second, configs, date);
    }
}


} // namespace RTBKIT
