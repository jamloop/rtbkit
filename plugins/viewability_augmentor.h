/* viewability_augmentor.h
   Mathieu Stefani, 23 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Augmentor that uses data from MOAT to filter requests depending on a
   viewability treshold
*/

#pragma once

#include "rtbkit/core/agent_configuration/agent_configuration_listener.h"
#include "rtbkit/plugins/augmentor/augmentor_base.h"
#include "soa/logger/logger.h"

namespace JamLoop {

class ViewabilityAugmentor : public RTBKIT::AsyncAugmentor {
public:

    static constexpr const char *Name = "viewability";

    ViewabilityAugmentor(
            std::shared_ptr<Datacratic::ServiceProxies> proxies,
            std::string serviceName = "viewability.augmentor");

    ViewabilityAugmentor(
            Datacratic::ServiceBase& parent,
            std::string serviceName = "viewability.augmentor");

    void init(int nthreads);

private:
    void onRequest(
            const RTBKIT::AugmentationRequest& request,
            AsyncAugmentor::SendResponseCB sendResponse);

    std::shared_ptr<RTBKIT::AgentConfigurationListener> agentConfig;

};

} // namespace JamLoop

