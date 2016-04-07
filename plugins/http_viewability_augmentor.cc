/* http_viewability_augmentor.cc
   Mathieu Stefani, 06 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
*/

#include "http_viewability_augmentor.h"
#include "http_augmentor_headers.h"
#include "soa/service/message_loop.h"
#include "soa/service/service_base.h"
#include "soa/utils/scope.h"
#include "rtbkit/core/agent_configuration/agent_configuration_listener.h"
#include "rtbkit/plugins/bid_request/openrtb_bid_request_parser.h"

using namespace Net;
using namespace Datacratic;
using namespace RTBKIT;

namespace JamLoop {

static constexpr size_t AugmentQueueSize = 128;

struct Worker : public Datacratic::MessageLoop {
public:
    Worker(std::shared_ptr<ServiceProxies> proxies)
        : augmentChannel(AugmentQueueSize)
        , proxies(std::move(proxies))
    { }

    void init() {
        augmentChannel.onEvent = [=](AugmentMessage&& message) {
            doAugment(std::move(message));
        };

        agentConfig = std::make_shared<AgentConfigurationListener>(proxies->zmqContext);
        agentConfig->init(proxies->config);

        addSource("Worker::augmentChannel", augmentChannel);
        addSource("Worker::agentConfig", agentConfig);
    }

    void augment(const Http::Request& request, Http::ResponseWriter response) {
        AugmentMessage message(request, std::move(response));
        auto *data = message.data();
        if (!augmentChannel.tryPush(std::move(message))) {
            data->response.send(Http::Code::Bad_Request);
            return;
        }
    }

private:
    struct AugmentMessage {
        AugmentMessage()
            : data_(nullptr)
        { }

        AugmentMessage(const Http::Request& request, Http::ResponseWriter response)
            : data_(new Data(request, std::move(response)))
        { }

        struct Data {
            Data(const Http::Request& request, Http::ResponseWriter response)
                : request(request)
                , response(std::move(response))
            { }

            Http::Request request;
            Http::ResponseWriter response;
        };

        Data* data() const { return data_.get(); }

    private:
        std::shared_ptr<Data> data_;
    };

    TypedMessageSink<AugmentMessage> augmentChannel;
    std::shared_ptr<ServiceProxies> proxies;
    std::shared_ptr<AgentConfigurationListener> agentConfig;

    void doAugment(AugmentMessage&& message) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto* data = message.data();
        auto& request = data->request;

        AugmentationList result;

        try {
            auto failure = ScopeFailure([=]() noexcept {
            });

            auto openrtbVersion = request.headers().get<XOpenRTBVersion>();
            std::ostringstream oss;
            openrtbVersion->write(oss);
            auto parser = OpenRTBBidRequestParser::openRTBBidRequestParserFactory(oss.str());
            
            auto payload = request.body();
            ML::Parse_Context context("Bid Request", payload.c_str(), payload.size());

            auto bidRequest = parser->parseBidRequest(context);
            auto agents = bidRequest.ext["agents"];
            for (const auto& agent: agents) {
                const AgentConfigEntry& configEntry = agentConfig->getAgentEntry(agent.asString());

                const AgentConfig& config = *configEntry.config;
                const AccountKey& account = config.account;

                result[account].tags.insert("pass-http");
            }

        } catch (const std::exception& e) {
        }

        data->response.send(Http::Code::Ok, result.toJson().toStringNoNewLine(), MIME(Application, Json));
    }
};

struct ViewabilityHandler : public Http::Handler {

    typedef Net::Http::details::prototype_tag tag;

    ViewabilityHandler(std::shared_ptr<Worker> worker)
        : worker(std::move(worker))
    { }

    void onRequest(const Http::Request& request, Http::ResponseWriter resp) {
        if (request.resource() == "/ready" && request.method() == Http::Method::Get) {
            resp.send(Http::Code::Ok, "1");
        }

        else if (request.resource() == "/augment" && request.method() == Http::Method::Get) {
            resp.headers()
                .add<XRTBKitTimestamp>(XRTBKitTimestamp::Clock::now())
                .add<XRTBKitProtocolVersion>(1, 0)
                .add<XRTBKitAugmentorName>("viewability");

            worker->augment(request, std::move(resp));

        } else {
            resp.send(Http::Code::Not_Found);
        }
    }

    std::shared_ptr<Net::Tcp::Handler> clone() const {
        return std::make_shared<ViewabilityHandler>(worker);
    }

private:
    std::shared_ptr<Worker> worker;
};

ViewabilityEndpoint::ViewabilityEndpoint(
        Net::Address addr, std::string serviceName, std::shared_ptr<ServiceProxies> proxies)
    : ServiceBase(std::move(serviceName), std::move(proxies))
    , httpEndpoint(std::make_shared<Http::Endpoint>(addr))
{ }

void
ViewabilityEndpoint::init(int thr) {
    auto opts = Http::Endpoint::options()
        .threads(thr);

    worker = std::make_shared<Worker>(getServices());
    worker->init();

    httpEndpoint->init(opts);
    httpEndpoint->setHandler(Http::make_handler<ViewabilityHandler>(worker));
}

void
ViewabilityEndpoint::start() {
    worker->start();
    httpEndpoint->serve();
}

} // namespace JamLoop
