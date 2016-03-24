/* forensiq_augmentor.h
   Mathieu Stefani, 23 mars 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Augmentor for Forensiq
*/

#pragma once

#include "rtbkit/core/agent_configuration/agent_configuration_listener.h"
#include "rtbkit/plugins/augmentor/augmentor_base.h"
#include "soa/service/logs.h"
#include "soa/service/http_client.h"

namespace JamLoop {

class ForensiqAugmentor : public RTBKIT::AsyncAugmentor {
public:

    ForensiqAugmentor(
            std::shared_ptr<Datacratic::ServiceProxies> proxies,
            std::string serviceName = "forensiq.augmentor");

    ForensiqAugmentor(
        Datacratic::ServiceBase& parent,
        std::string serviceName = "forensiq.augmentor");

    void init(int nthreads, const std::string& apiKey);

private:

    void onRequest(
            const RTBKIT::AugmentationRequest& request,
            AsyncAugmentor::SendResponseCB sendResponse);

    RTBKIT::AugmentationList handleHttpResponse(
            const RTBKIT::AugmentationRequest& request,
            Datacratic::HttpClientError error, int statusCode, std::string&& body);

    std::shared_ptr<RTBKIT::AgentConfigurationListener> agentConfig;
    std::shared_ptr<Datacratic::HttpClient> httpClient;

    std::string apiKey_;
};

} // namespace JamLoop
