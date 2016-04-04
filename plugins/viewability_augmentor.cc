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

static constexpr int AdaptvUnknownViewabilityScore = 5;

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
                if (score <= AdaptvUnknownViewabilityScore)
                    return ExchangeViewability::Unknown;
                else if (score >= threshold)
                    return ExchangeViewability::Viewable;
                else
                    return ExchangeViewability::NonViewable;
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

        if (httpClient) {

            const auto& br = request.bidRequest;
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

        std::string lookupStage;
        double viewabilityPrct = 0.0;

        if (statusCode == 200) {
            if (body.empty()) {
                fail(failure, [&]() {
                    recordError("http.emptyBody");
                });
                return result;
            }

            Json::Value response = Json::parse(body);
            viewabilityPrct = response["score"].asDouble();
            lookupStage = response.get("stage", "").asString();
        }

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

                auto threshold = agentAugConfig.config["viewTreshold"];
                if (!threshold.isInt()) {
                    recordResult(account, "invalidThreshold");
                    continue;
                }

                auto thresh = threshold.asInt();

                if (statusCode == 204) {
                    recordHit("accounts.%s.lookup.NoHit", account.toString());

                    const auto& br = augRequest.bidRequest;

                    auto recordExchangeResult = [&](const char* result) {
                        recordHit("accounts.%s.result.%s.%s", account.toString(), br->exchange, result);
                    };

                    auto ev = getExchangeViewability(br, thresh);
                    if (ev == ExchangeViewability::Viewable) {
                        result[account].tags.insert("pass-viewability");

                        recordExchangeResult("viewable");
                        recordResult(account, "passed");
                        continue;

                    } else if (ev == ExchangeViewability::NonViewable) {
                        recordExchangeResult("nonviewable");
                        recordResult(account, "filtered");
                        continue;
                    } else {
                        recordExchangeResult("unknown");

                        auto strategy = agentAugConfig.config.get("unknownStrategy", "nobid").asString();
                        if (strategy != "nobid" && strategy != "bid") {
                            recordResult(account, "invalidStrategy");
                            continue;
                        }
                        if (strategy == "bid") {
                            result[account].tags.insert("pass-viewability");
                            recordResult(account, "passed");
                            continue;
                        }
                    }

                } else {
                    recordOutcome(viewabilityPrct, "accounts.%s.score", account.toString());
                    if (viewabilityPrct >= thresh) {
                        result[account].tags.insert("pass-viewability");
                        recordResult(account, "passed");
                        if (!lookupStage.empty()) {
                            recordHit("accounts.%s.lookup.%s", account.toString(), lookupStage);
                        }
                        continue;
                    }
                }
                recordResult(account, "filtered");
            }
        }
    }

    return result;
}

} // namespace JamLoop
