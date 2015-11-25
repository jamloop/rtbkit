/* publisher_connector.cc
   Mathieu Stefani, 10 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the publisher connector
*/

#include "rtbkit/plugins/exchange/http_auction_handler.h"
#include "publisher_connector.h"
#include "utils.h"
#include "soa/utils/scope.h"
#include "soa/service/logs.h"
#include "soa/types/string.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace RTBKIT;
using namespace Datacratic;

namespace Jamloop {

    namespace Logs {
        static Logging::Category print("PublisherConnector");
        static Logging::Category trace("PublisherConnector trace");
        static Logging::Category error("PublisherConnector error");
    }

    namespace Default {
        static constexpr double MaxAuctionTime = 100.0;
    }

    namespace {
        Id generateUniqueId() {
            boost::uuids::random_generator gen;
            auto uuid = gen();

            std::stringstream ss;
            ss << uuid;

            return Id(ss.str());
        }

        namespace detail {
            template<typename T> struct LexicalCast
            {
                static T cast(const std::string& value) {
                    return boost::lexical_cast<T>(value);
                }
            };

            template<> struct LexicalCast<Datacratic::UnicodeString>
            {
                static Datacratic::UnicodeString cast(const std::string& value) {
                    return Datacratic::UnicodeString(value);
                }
            };

            template<> struct LexicalCast<Datacratic::Url>
            {
                static Datacratic::Url cast(const std::string& value) {
                    return Datacratic::Url(urldecode(value));
                }
            };

            template<> struct LexicalCast<Datacratic::TaggedInt>
            {
                static Datacratic::TaggedInt cast(const std::string& value) {
                    Datacratic::TaggedInt val;
                    val.val = boost::lexical_cast<int>(value);
                }
            };

            template<> struct LexicalCast<OpenRTB::AdPosition>
            {
                static OpenRTB::AdPosition cast(const std::string& value) {
                    OpenRTB::AdPosition pos;

                    return pos;
                }
            };
        }

        template<typename Param>
        Param param_cast(const std::string& value) {
            return detail::LexicalCast<Param>::cast(value);
        } 

        template<typename Param>
        void extractParam(const RestParams& params, const char* name, Param& out) {
            if (!params.hasValue(name))
                throw ML::Exception("Could not find query param '%s'", name);

            auto value = params.getValue(name);
            out = param_cast<Param>(value);
        }
    }

    PublisherConnector::PublisherConnector(ServiceBase& owner, std::string name)
        : HttpExchangeConnector(std::move(name), owner)
        , maxAuctionTime(Default::MaxAuctionTime)
        , creativeConfig(exchangeName())
    {
        this->auctionVerb = "GET";
        this->auctionResource = "/vast2";
        initCreativeConfiguration();
    }

    PublisherConnector::PublisherConnector(std::string name,
            std::shared_ptr<ServiceProxies> proxies)
        : HttpExchangeConnector(std::move(name), std::move(proxies))
        , maxAuctionTime(Default::MaxAuctionTime)
        , creativeConfig(exchangeName())
    {
        this->auctionVerb = "GET";
        this->auctionResource = "/vast2";
        initCreativeConfiguration();
    }

    void
    PublisherConnector::initCreativeConfiguration() {
        creativeConfig.addExpanderVariable(
            "exhange",
            [](const PublisherCreativeConfiguration::Context& context) {
                return std::string("publisher_") + context.bidrequest.user->id.toString();
        });

        creativeConfig.addField(
            "vast",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.vast);

                if (info.vast.empty())
                    throw std::invalid_argument("vast is required");

                return true;
        }).snippet().required();
    }

    ExchangeConnector::ExchangeCompatibility
    PublisherConnector::getCampaignCompatibility(
            const AgentConfig& config,
            bool includeReasons) const {
        ExchangeConnector::ExchangeCompatibility compatibility;
        compatibility.setCompatible();

        return compatibility;
    }

    ExchangeConnector::ExchangeCompatibility
    PublisherConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const {
        return creativeConfig.handleCreativeCompatibility(creative, includeReasons);
    }

    double
    PublisherConnector::getTimeAvailableMs(
            Datacratic::HttpAuctionHandler& handler,
            const Datacratic::HttpHeader& header,
            const std::string& payload) {
        return maxAuctionTime;
    }

    std::shared_ptr<BidRequest>
    PublisherConnector::parseBidRequest(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        auto br = std::make_shared<BidRequest>();

        try {
            Scope_Failure(br.reset());

            br->auctionId = generateUniqueId();
            br->auctionType = AuctionType::SECOND_PRICE;
            br->timeAvailableMs = maxAuctionTime;
            br->timestamp = Date::now();
            br->isTest = false;

            AdSpot spot;
            spot.id = Id("1");

            Datacratic::Optional<OpenRTB::Video> video;
            video.reset(new OpenRTB::Video());

            Datacratic::Optional<OpenRTB::Device> device;
            device.reset(new OpenRTB::Device());

            Datacratic::Optional<OpenRTB::Site> site;
            site.reset(new OpenRTB::Site());

            Datacratic::Optional<OpenRTB::Content> content;
            content.reset(new OpenRTB::Content());

            Datacratic::Optional<OpenRTB::User> user;
            user.reset(new OpenRTB::User());

            int width, height;
            const auto& queryParams = header.queryParams;
            extractParam(queryParams, "width", width);
            extractParam(queryParams, "height", height);
            extractParam(queryParams, "ip", device->ip);
            extractParam(queryParams, "ua", device->ua);
            extractParam(queryParams, "lang", content->language);
            extractParam(queryParams, "pageurl", site->page);
            extractParam(queryParams, "partner", user->id);

            video->w = width;
            video->h = height;

            double price = 0.0;
            extractParam(queryParams, "price", price);
            br->ext["price"] = price * 1000.0;

            site->content = std::move(content);
            br->device = std::move(device);
            br->site = std::move(site);
            br->user = std::move(user);
            br->url = br->site->page;
            spot.video = std::move(video);
            spot.formats.push_back(Format(width, height));
            br->imp.push_back(std::move(spot));

        } catch (const std::exception& e) {
            LOG(Logs::error) << "Error when processing request: " << e.what();
            handler.dropAuction();
        }

        return br;
    }

    HttpResponse
    PublisherConnector::getDroppedAuctionResponse(
        const HttpAuctionHandler& connection,
        const std::string& reason) const {
        return HttpResponse(200, "application/xml", genericVast);
    }

    HttpResponse
    PublisherConnector::getResponse(
            const HttpAuctionHandler& connection,
            const Datacratic::HttpHeader& header,
            const RTBKIT::Auction& auction) const {
        const Auction::Data* current = auction.getCurrentData();

        if (current->hasError())
            return getDroppedAuctionResponse(connection, "");

        const auto& responses = current->responses;
        ExcAssert(responses.size() <= 1);

        /* We should have only one spot, so only one response */
        if (!current->hasValidResponse(0))
            return getDroppedAuctionResponse(connection, "");

        const auto& resp = current->winningResponse(0);

        auto price = auction.request->ext["price"].asDouble();
        if(price > resp.price.maxPrice.value) {
            recordHit("priceTooHigh");
            return HttpResponse(200, "application/xml", genericVast);
        }

        const AgentConfig* config
            = std::static_pointer_cast<const AgentConfig>(resp.agentConfig).get();

        int creativeIndex = resp.agentCreativeIndex;
        const auto& creative = config->creatives[creativeIndex];
        auto creativeInfo = creative.getProviderData<CreativeInfo>(exchangeName());

        PublisherCreativeConfiguration::Context context {
            creative,
            resp,
            *auction.request,
            0
        };

        return HttpResponse(200, "application/xml",
                creativeConfig.expand(creativeInfo->vast, context));
    }

    void
    PublisherConnector::configure(const Json::Value& parameters) {
        HttpExchangeConnector::configure(parameters);

        maxAuctionTime = parameters.get("maxAuctionTime", Default::MaxAuctionTime).asDouble();
#if 0
        if (!parameters.isMember("adserverHost")) {
            throw ML::Exception("Missing adServerHost parameter");
        }

        wins.reset(new WinSource(parameters["adserverHost"].asString()));
#endif
        if (!parameters.isMember("genericVast")) {
            throw ML::Exception("genericVast is required");
        }
        genericVast = parameters["genericVast"].asString();
    }

    PublisherConnector::WinSource::WinSource(const std::string& adserverHost)
        : queue(std::numeric_limits<uint16_t>::max()) {

        client = std::make_shared<HttpClient>(adserverHost);
        addSource("WinSource::client", client);
        addSource("WinSource::queue", queue);

        queue.onEvent = std::bind(&WinSource::doWin, this, std::placeholders::_1);
    }

    void
    PublisherConnector::WinSource::sendAsync(
            const Auction& auction, PublisherConnector::WinSource::OnWinSent onSent)
    {
        const Auction::Data* current = auction.getCurrentData();
        if (current->hasError()) {
        }

        ExcAssert(current->responses.empty() || current->responses.size() == 1);

        if (current->responses.empty()) {
        }

        else {
            auto& resp = current->winningResponse(0);
            WinMessage message(resp, auction.request, std::move(onSent));
            if (!queue.tryPush(std::move(message))) {
                throw ML::Exception("Failed to push message");
            }
        }
    }

    HttpResponse
    PublisherConnector::WinSource::sendSync(
            const Auction& auction)
    {
        throw ML::Exception("Unimplemented");
    }

    void
    PublisherConnector::WinSource::doWin(WinMessage&& message)
    {
        const auto price = message.response.price;
        const auto cpm = RTBKIT::getAmountIn<CPM>(price.maxPrice);
        Json::Value winPayload;
        winPayload["timestamp"] = Date::now().secondsSinceEpoch();
        winPayload["bidRequestId"] = message.request->auctionId.toString();
        winPayload["impid"] = message.request->imp[0].id.toString();
        winPayload["price"] = cpm.value;

        auto onSent = message.onSent;

        auto callback = std::make_shared<HttpClientSimpleCallbacks>(
            [=](const HttpRequest&, HttpClientError error, int statusCode,
                std::string&& headers, std::string&& body) {

            onSent();
        });

        client->post("/", callback, HttpRequest::Content(winPayload),
                { },
                { },
                1);
    }


} // namespace JamLoop

namespace {

struct Init {
    Init() {
        RTBKIT::ExchangeConnector::registerFactory<Jamloop::PublisherConnector>();
    }
} init;

}

