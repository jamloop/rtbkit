/* forensiq_augmentor.cc
   Mathieu Stefani, 23 mars 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Forensiq Augmentor
*/


#include "forensiq_augmentor.h"
#include "soa/utils/scope.h"

using namespace std;
using namespace RTBKIT;
using namespace Datacratic;

namespace JamLoop {

static constexpr const char *ForensiqAPI = "http://api.forensiq.com";
static constexpr int HTTP_OK = 200;

std::string urlencode(const std::string& str) {
    std::string result;
    for (auto c: str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else result += ML::format("%%%02X", c);
    }
    return result;
}

ForensiqAugmentor::ForensiqAugmentor(
        shared_ptr<ServiceProxies> proxies,
        string serviceName)
    : AsyncAugmentor("forensiq", std::move(serviceName), std::move(proxies))
{ }

ForensiqAugmentor::ForensiqAugmentor(
        ServiceBase& parent,
        string serviceName)
    : AsyncAugmentor("forensiq", std::move(serviceName), parent)
{ }

void
ForensiqAugmentor::init(int nthreads, const std::string& apiKey)
{
    AsyncAugmentor::init(nthreads);

    agentConfig = std::make_shared<AgentConfigurationListener>(getZmqContext());
    agentConfig->init(getServices()->config);

    addSource("ForensiqAugmentor::agentConfig", agentConfig);

    httpClient = std::make_shared<HttpClient>(ForensiqAPI, 128);
    addSource("ForensiqAugmentor::httpClient", httpClient);

    apiKey_ = apiKey;
}

void
ForensiqAugmentor::onRequest(
        const AugmentationRequest& request,
        AsyncAugmentor::SendResponseCB sendResponse)
{
    RestParams queryParams;

    auto addGeo = [&](const Optional<OpenRTB::Geo>& geo) {
        if (geo) {
            auto lat = geo->lat;
            auto lon = geo->lon;
            if (!std::isnan(lat.val))
                queryParams.push_back(std::make_pair("lat", std::to_string(lat.val)));
            if (!std::isnan(lon.val))
                queryParams.push_back(std::make_pair("long", std::to_string(lon.val)));

        }
    };

    auto br = request.bidRequest;
    std::string seller = br->exchange;

    auto addPublisher = [&](const Optional<OpenRTB::Publisher>& publisher) {
        if (publisher) {
            auto id = publisher->id;
            if (id) {
                seller += id.toString();
            }
        }

    };

    if (br->device) {
        auto ip = br->device->ip;
        if (!ip.empty())
            queryParams.push_back(std::make_pair("ip", ip));
        addGeo(br->device->geo);
    }
    if (br->app) {
        auto bundle = br->app->bundle;
        if (!bundle.empty())
            queryParams.push_back(std::make_pair("aid", bundle.rawString()));
        addPublisher(br->app->publisher);
    }
    if (br->site) {
        auto page = br->site->page;
        if (!page.empty())
            queryParams.push_back(std::make_pair("url", urlencode(page.toString())));
        addPublisher(br->site->publisher);
    }
    if (br->user) {
        auto uid = br->user->id;
        if (uid)
            queryParams.push_back(std::make_pair("id", uid.toString()));
        addGeo(br->user->geo);
    }

    auto ua = br->userAgent;
    if (!ua.empty())
        queryParams.push_back(std::make_pair("ua", urlencode(ua.rawString())));

    queryParams.push_back(std::make_pair("rt", "display"));
    queryParams.push_back(std::make_pair("output", "json"));
    queryParams.push_back(std::make_pair("ck", apiKey_));
    queryParams.push_back(std::make_pair("seller", seller));

    Date start = Date::now();

    auto onResponse =
        std::make_shared<HttpClientSimpleCallbacks>(
                [=](const HttpRequest& req, HttpClientError error,
                    int status, std::string&&, std::string&& body)
        {
            auto latency = Date::now().secondsSince(start) * 1000;
            recordHit("http.responses");
            recordOutcome(latency, "http.latencyMs");

            sendResponse(handleHttpResponse(
                    request, error, status, std::move(body)));
        });


    recordHit("http.request");
    httpClient->get("/check", onResponse, queryParams, { }, 1);
}

AugmentationList
ForensiqAugmentor::handleHttpResponse(
    const AugmentationRequest& augRequest,
    HttpClientError error,
    int statusCode,
    std::string&& body)
{
    auto recordResult = [&](const AccountKey& account, const char* key) {
        recordHit("accounts.%s.%s", account.toString(), key);
    };

    auto recordError = [&](std::string key) {
        recordHit("http.error.%s", key.c_str());
        recordHit("http.error.error.total");
    };

    AugmentationList result;

    auto failure = ScopeFailure([&]() noexcept {
        for (const auto& agent: augRequest.agents) {
            const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);
            const AccountKey& account = configEntry.config->account;

            result[account].tags.insert("pass-forensiq");
        }
    });

    if (error != HttpClientError::None) {
        fail(failure, [&] {
            recordError(HttpClientCallbacks::errorMessage(error));
        });
        return result;
    }
    else {
        if (statusCode != HTTP_OK) {
            fail(failure, [&] {
                recordError("invalidCode");
            });
            return result;
        }

        recordHit("http.validResponses");
        auto response = Json::parse(body);
        auto score = response["riskScore"].asDouble();

        for (const auto& agent: augRequest.agents) {
            const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);

            const AgentConfig& config = *configEntry.config;
            const AccountKey& account = config.account;

            auto augConfigIt = std::find_if(
                    config.augmentations.begin(), config.augmentations.end(),
                    [&](const AugmentationConfig& augConfig) {
                        return augConfig.name == augRequest.augmentor;
            });

            if (augConfigIt != config.augmentations.end()) {
                const AugmentationConfig& augConfig = *augConfigIt;
                if (!augConfig.config.isMember("riskScoreThreshold")) {
                    recordResult(account, "invalidConfig");
                    continue;
                }

                auto threshold = augConfig.config["riskScoreThreshold"];
                if (!threshold.isInt()) {
                    recordResult(account, "invalidThreshold");
                    continue;
                }

                auto thresh = threshold.asInt();
                if (thresh < 0 || thresh > 100) {
                    recordResult(account, "invalidThreshold");
                    continue;
                }

                recordOutcome(score, "accounts.%s.score", account.toString());
                if (score <= thresh) {
                    result[account].tags.insert("pass-forensiq");
                    recordResult(account, "passed");
                } else {
                    recordResult(account, "filtered");
                }
            }

        }
    }

    return result;
}

} // namespace JamLoop
