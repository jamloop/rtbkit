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

Logging::Category ViewabilityAugmentor::Logs::print(
    "ViewabilityAugmentor");
Logging::Category ViewabilityAugmentor::Logs::trace(
    "ViewabilityAugmentor Trace", ViewabilityAugmentor::Logs::print);
Logging::Category ViewabilityAugmentor::Logs::error(
    "ViewabilityAugmentor Error", ViewabilityAugmentor::Logs::print);

enum class ExchangeViewability {
    Viewable,
    NonViewable,
    Unknown
};

namespace Default {
    static constexpr int MaximumHttpConnections = 128;
}

namespace {
    /* Some exchanges like Adap or Brightroll directly send a viewability score inside the BidRequest.
     * We use that score to determine whether an impression is viewable or not if is unknown to the
     * go service
    */
    ExchangeViewability getExchangeViewability(const std::shared_ptr<RTBKIT::BidRequest>& br, int threshold) {
        /* BrightRoll sends an int inside the ext of the BR */
        if (br->exchange == "brightroll") {
            auto ext = br->ext;
            if (ext.isMember("viewability")) {
                auto viewability = ext["viewability"].asInt();
                switch (viewability) {
                    case 1:
                        return ExchangeViewability::Viewable;
                    case 2:
                        return ExchangeViewability::NonViewable;
                }
            }
        } else if (br->exchange == "adaptv") {
            /* Adaptv sends only one impression and puts the viewability flag inside
             * the ext of the video object */
            const auto& spot = br->imp[0];
            auto ext = spot.video->ext;
            if (ext.isMember("viewability")) {
                auto score = ext["viewability"].asInt();
                if (score > -1) {
                    if (score >= threshold)
                        return ExchangeViewability::Viewable;
                    else
                        return ExchangeViewability::NonViewable;
                }
            }
        }

        return ExchangeViewability::Unknown;
    }
}

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
    httpClient = std::make_shared<HttpClient>(baseUrl, Default::MaximumHttpConnections);
    addSource("ViewabilityAugmentor::httpClient", httpClient);
}

void
ViewabilityAugmentor::onRequest(
        const AugmentationRequest& request,
        AsyncAugmentor::SendResponseCB sendResponse)
{

    using OpenRTB::AdPosition;
    auto adPosition = [](AdPosition position) -> const char* const {
        switch (static_cast<AdPosition::Vals>(position.val)) {
            case AdPosition::UNSPECIFIED:
            case AdPosition::UNKNOWN:
                return "unknown";
            case AdPosition::ABOVE:
                return "above";
            case AdPosition::BELOW:
                return "below";
            case AdPosition::HEADER:
                return "header";
            case AdPosition::FOOTER:
                return "footer";
            case AdPosition::SIDEBAR:
                return "sidebar";
            case AdPosition::FULLSCREEN:
                return "fullscreen";

        }
        return "unknown";
    };

    try {
        AugmentationList emptyResult;
        Scope_Failure(sendResponse(emptyResult));

        const auto& br = request.bidRequest;
        if (br->ext.isMember("inventoryType")) {
            auto type = br->ext["inventoryType"].asString();
            if (type == "highviewable") {
                AugmentationList result;

                for (const auto& agent: request.agents) {
                    const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);
                    if (!configEntry.valid()) continue;

                    const AccountKey& account = configEntry.config->account;

                    result[account].tags.insert("pass-viewability");
                    result[account].tags.insert("pass-vr");
                    recordHit("accounts.%s.adapviewable", account.toString());
                    recordHit("accounts.%s.passed", account.toString());
                }

                sendResponse(result);
                return;
            }
        }

        if (httpClient) {

            auto& imp = br->imp[0];
            auto w = imp.formats[0].width;

            Json::Value payload(Json::objectValue);
            payload["exchange"] = br->exchange;
            if (br->site && br->site->publisher) {
                payload["publisher"] = br->site->publisher->id.toString();
            }
            payload["url"] = br->url.toString();
            payload["w"] = w;
            if (imp.video) {
                payload["position"] = adPosition(imp.video->pos);
            }

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
    } catch (const std::exception& e) {
        LOG(Logs::error) << "Error when processing BidRequest: " << e.what() << endl;
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
        recordHit("accounts.%s.go", account.toString());
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
            if (!configEntry.valid()) continue;

            const AccountKey& account = configEntry.config->account;

            result[account].tags.insert("pass-viewability");
            result[account].tags.insert("pass-vr");
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

        std::string lookupStage;
        double viewableRate = 0.0;
        double measuredRate = 0.0;
        double completedViewRate = 0.0;
        double clickThroughRate = 0.0;
        double viewableCompletedViewRate = 0.0;

        if (statusCode == 200) {
            if (body.empty()) {
                fail(failure, [&]() {
                    recordError("http.emptyBody");
                });
                return result;
            }

            Json::Value response = Json::parse(body);
            Json::Value metrics = response["metrics"];
            viewableRate = metrics["vr"].asDouble();
            measuredRate = metrics["mr"].asDouble();
            completedViewRate = metrics["cvr"].asDouble();
            clickThroughRate = metrics["ctr"].asDouble();
            viewableCompletedViewRate = metrics["vcvr"].asDouble();
            lookupStage = response.get("stage", "").asString();
        }

        for (const auto& agent: augRequest.agents) {
            const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent);
            if (!configEntry.valid()) continue;

            const AgentConfig& config = *configEntry.config;
            const AccountKey& account = config.account;

            for (const auto& agentAugConfig: configEntry.config->augmentations) {
                if (agentAugConfig.name != augRequest.augmentor) continue;

                auto getThreshold = [&](std::string const & name) {
                    int result = 0;
                    if (agentAugConfig.config.isMember(name)) {
                        auto threshold = agentAugConfig.config[name];
                        result = threshold.asInt();
                    }

                    return result;
                };

                double legacyViewThreshold = getThreshold("viewThreshold");
                double viewableRateThreshold = getThreshold("vrThreshold");
                double measuredRateThreshold = getThreshold("mrThreshold");
                double completedViewRateThreshold = getThreshold("cvrThreshold");
                double clickThroughRateThreshold = getThreshold("ctrThreshold");
                double viewableCompletedViewRateThreshold = getThreshold("vcvrThreshold");
                bool exchangeFallback = true;
                if (agentAugConfig.config.isMember("exchangeFallback")) {
                    auto fallback = agentAugConfig.config["exchangeFallback"];
                    exchangeFallback = fallback.asBool();
                }

                if (legacyViewThreshold != 0 && viewableRateThreshold == 0) {
                    viewableRateThreshold = legacyViewThreshold;
                }

                if (statusCode == 204) {
                    recordHit("accounts.%s.lookup.NoHit", account.toString());

                    const auto& br = augRequest.bidRequest;

                    auto recordExchangeResult = [&](const char* result) {
                        recordHit("accounts.%s.result.%s.%s", account.toString(), br->exchange, result);
                    };

		    if (exchangeFallback) {
                        auto ev = getExchangeViewability(br, viewableRateThreshold);
                        if (ev == ExchangeViewability::Viewable) {
                            result[account].tags.insert("pass-viewability");
                            result[account].tags.insert("pass-vr");

                            recordExchangeResult("viewable");
                            recordResult(account, "passed");
                            continue;
                        } else if (ev == ExchangeViewability::NonViewable) {
                            recordExchangeResult("nonviewable");
                            recordResult(account, "filtered");
                            continue;
                        }
		    }

                    recordExchangeResult("unknown");

                    auto strategy = agentAugConfig.config.get("unknownStrategy", "nobid").asString();
                    if (strategy != "nobid" && strategy != "bid") {
                        recordResult(account, "invalidStrategy");
                        continue;
                    }
		    
		    if (strategy == "bid") {
                        result[account].tags.insert("pass-viewability");
                        result[account].tags.insert("pass-vr");
                        recordResult(account, "passed");
                        continue;
                    }

                } else {
                    if (!lookupStage.empty()) {
                        recordHit("accounts.%s.lookup.%s", account.toString(), lookupStage);
                    }

                    bool passed = true;

                    recordOutcome(viewableRate, "accounts.%s.vr", account.toString());
                    if (viewableRate > 0) {
                        if (viewableRate >= viewableRateThreshold) {
                            result[account].tags.insert("pass-viewability");
			    result[account].tags.insert("pass-vr");
                            recordResult(account, "passed-vr");
                        } else {
                            passed = false;
                        }
                    }

                    recordOutcome(measuredRate, "accounts.%s.mr", account.toString());
                    if (measuredRate > 0) {
                        if (measuredRate >= measuredRateThreshold) {
                            result[account].tags.insert("pass-mr");
                            recordResult(account, "passed-mr");
                        } else {
                            passed = false;
                        }
                    }

                    recordOutcome(completedViewRate, "accounts.%s.cvr", account.toString());
                    if (completedViewRate > 0) {
                        if (completedViewRate >= completedViewRateThreshold) {
                            result[account].tags.insert("pass-cvr");
                            recordResult(account, "passed-cvr");
                        } else {
                            passed = false;
                        }
                    }

                    recordOutcome(clickThroughRate, "accounts.%s.ctr", account.toString());
                    if (clickThroughRate > 0) {
                        if (clickThroughRate >= clickThroughRateThreshold) {
                            result[account].tags.insert("pass-ctr");
                            recordResult(account, "passed-ctr");
                        } else {
                            passed = false;
                        }
                    }

                    recordOutcome(viewableCompletedViewRate, "accounts.%s.vcvr", account.toString());
                    if (viewableCompletedViewRate > 0) {
                        if (viewableCompletedViewRate >= viewableCompletedViewRateThreshold) {
                            result[account].tags.insert("pass-vcvr");
                            recordResult(account, "passed-vcvr");
                        } else {
                            passed = false;
                        }
                    }

                    if (passed) continue;
                }

                recordResult(account, "filtered");
            }
        }
    }

    return result;
}

} // namespace JamLoop
