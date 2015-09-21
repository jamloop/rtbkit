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
#include "soa/service/logs.h"
#include "soa/service/http_client.h"

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
    void useGoView(const std::string& baseUrl);

private:
    struct Logs {
        static Datacratic::Logging::Category print;
        static Datacratic::Logging::Category trace;
        static Datacratic::Logging::Category error;
    };

    void onRequest(
            const RTBKIT::AugmentationRequest& request,
            AsyncAugmentor::SendResponseCB sendResponse);


    RTBKIT::AugmentationList handleHttpResponse(
            const RTBKIT::AugmentationRequest& request,
            Datacratic::HttpClientError error, int statusCode, std::string&& body);

    std::shared_ptr<RTBKIT::AgentConfigurationListener> agentConfig;
    std::shared_ptr<Datacratic::HttpClient> httpClient;

};

} // namespace JamLoop

