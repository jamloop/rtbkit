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

namespace {
    string urldecode(const std::string& url) {
        auto fromHex = [](char c) {
            if (isdigit(c)) return c - '0';
            switch (c) {
                case 'a':
                case 'A':
                    return 10;
                case 'b':
                case 'B':
                    return 11;
                case 'c':
                case 'C':
                    return 12;
                case 'd':
                case 'D':
                    return 13;
                case 'e':
                case 'E':
                    return 14;
                case 'f':
                case 'F':
                    return 15;
            }

            throw ML::Exception("Invalid hexadecimal character '%c'", c);
        };

        std::ostringstream decoded;
        auto it = url.begin(), end = url.end();
        while (it != end) {
            const char c = *it;
            if (c == '%') {
                if (it[1] && it[2]) {
                    decoded << static_cast<char>(fromHex(it[1]) << 4 | fromHex(it[2]));
                    it += 3;
                }
            }
            else {
                decoded << c;
                ++it;
            }
        }

        return decoded.str();

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
        httpClient = std::make_shared<HttpClient>(baseUrl);
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

        if (httpClient) {

            try {
                AugmentationList emptyResult;
                Scope_Failure(sendResponse(emptyResult));

                const auto& br = request.bidRequest;
                auto& imp = br->imp[0];
                auto w = imp.formats[0].width;


                Json::Value payload(Json::objectValue);
                payload["exchange"] = br->exchange;
                if (br->site && br->site->publisher) {
                    payload["publisher"] = br->site->publisher->id.toString();
                }
                payload["url"] = urldecode(br->url.toString());
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
            } catch (const std::exception& e) {
                LOG(Logs::error) << "Error when processing BidRequest: " << e.what() << endl;
            }
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
                    if (!treshold.isInt()) {
                        recordResult(account, "invalidTreshold");
                        continue;
                    }

                    auto val = treshold.asInt();
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
