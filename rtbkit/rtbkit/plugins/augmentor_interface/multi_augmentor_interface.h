/* multi_augmentor_interface.h
   Mathieu Stefani, 08 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   An augmentor interface that can be used to dispatch requests
   to different types of augmentor interfaces
*/

#pragma once

#include "rtbkit/common/augmentor_interface.h"

namespace RTBKIT {

class MultiAugmentorInterface : public AugmentorInterface {
public:
    MultiAugmentorInterface(
            std::shared_ptr<Datacratic::ServiceProxies> proxies,
            const std::string& serviceName = "multiAugmentor",
            const Json::Value& config = Json::Value());

    void init();
    void bindTcp(const Datacratic::PortRange& range);

protected:
    void doSendAugmentMessage(
            const std::shared_ptr<AugmentorInstanceInfo>& instance,
            const std::string& augmentorName,
            const std::shared_ptr<Auction>& auction,
            const std::set<std::string>& agents,
            const std::map<std::string, std::vector<AugmentationConfig>>& configs,
            Datacratic::Date date);

private:
    std::unordered_map<std::string, std::shared_ptr<AugmentorInterface>> interfaces;
};

} // namespace RTBKIT
