/* viewability_augmentor.cc
   Mathieu Stefani, 23 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the MOAT Viewability augmentor
*/

#include "viewability_augmentor.h"
#include "soa/utils/scope.h"

using namespace std;
using namespace RTBKIT;
using namespace Datacratic;

namespace JamLoop {

    ViewabilityAugmentor::ViewabilityAugmentor(
        shared_ptr<ServiceProxies> proxies,
        string serviceName)
        : AsyncAugmentor(
            ViewabilityAugmentor::Name, std::move(serviceName), std::move(proxies))
    {
    }
    
    
    ViewabilityAugmentor::
    ViewabilityAugmentor(
            ServiceBase& parent,
            string serviceName)
    : AsyncAugmentor(
            ViewabilityAugmentor::Name, std::move(serviceName), parent)
    {
    }
    

    void
    ViewabilityAugmentor::init(int nthreads)
    {
        AsyncAugmentor::init(nthreads);
        agentConfig = std::make_shared<AgentConfigurationListener>(getZmqContext());
        agentConfig->init(getServices()->config);

        addSource("ViewabilityAugmentor::agentConfig", agentConfig);
    }

    void
    ViewabilityAugmentor::useGoView(const std::string& baseUrl)
    {
        httpClient = std::make_shared<HttpClient>(baseUrl);
        addSource("ViewabilityAugmentor::httpClient", httpClient);
    }

    void
    ViewabilityAugmentor::onRequest(
            const AugmentationRequest& request,
            AsyncAugmentor::SendResponseCB sendResponse)
    {
        const auto& br = request.bidRequest;
        const auto& url = br->url;

        auto w = br->imp[0].formats[0].width;

        if (httpClient) {
            Json::Value payload(Json::objectValue);
            payload["url"] = url.toString();
            payload["w"] = w;

            auto onResponse =
                std::make_shared<HttpClientSimpleCallbacks>(
                        [=](const HttpRequest& req, HttpClientError error,
                            int status, std::string&&, std::string&& body)
                {
                    sendResponse(handleHttpResponse(
                            request, error, status, std::move(body)));

                });

            httpClient->post("/viewability", onResponse,
                    HttpRequest::Content(payload),
                    { } /* queryParams */,
                    { } /* headers */,
                    1);
        }
    }

    AugmentationList
    ViewabilityAugmentor::handleHttpResponse(
            const AugmentationRequest& augRequest,
            HttpClientError error,
            int statusCode,
            std::string&& body)
    {
        auto isValidResponse = [](int statusCode) {
            return statusCode == 200 || statusCode == 204;
        };

        auto recordResult = [&](const AccountKey& account, const char* key) {
            recordHit("accounts.%s.%s", account.toString(), key);
        };

        auto recordError = [&](std::string key) {
            recordHit("error.%s", key.c_str());
            recordHit("error.total");
        };

        AugmentationList result;

        /* On failure, we consider that it passed */
        auto failure = ScopeFailure([&]() noexcept {
            for (const auto& agent: augRequest.agents) {
                const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);
                const AccountKey& account = configEntry.config->account;

                result[account].tags.insert("pass-viewability");
            }
        });

        if (error != HttpClientError::None) {
            fail(failure, [&] {
                recordError("http." + HttpClientCallbacks::errorMessage(error));
            });
            return result;
        }
        else {
            if (!isValidResponse(statusCode)) {
                fail(failure, [&] {
                    recordError("http.invalidCode");
                });
                return result;
            }

            auto viewabilityPrct = std::stoi(body);

            for (const auto& agent: augRequest.agents) {
                const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);

                const AgentConfig& config = *configEntry.config;
                const AccountKey& account = config.account;

                for (const auto& agentAugConfig: configEntry.config->augmentations) {
                    if (agentAugConfig.name != augRequest.augmentor) continue;

                    if (!agentAugConfig.config.isMember("viewTreshold")) {
                        recordResult(account, "invalidConfig");
                        continue;
                    }

                    auto treshold = agentAugConfig.config["viewTreshold"];
                    if (!treshold.isUInt()) {
                        recordResult(account, "invalidTreshold");
                        continue;
                    }

                    auto val = treshold.asUInt();
                    if (statusCode == 204 || viewabilityPrct >= val) {
                        result[account].tags.insert("pass-viewability");
                        recordResult(account, "passed");
                    }
                    else {
                        recordResult(account, "filtered");
                    }

                }
            }
        }

        return result;
    }

} // namespace JamLoop
