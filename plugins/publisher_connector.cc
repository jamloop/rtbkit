/* publisher_connector.cc
   Mathieu Stefani, 10 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the publisher connector
*/

#include "publisher_connector.h"
#include "soa/utils/scope.h"
#include "soa/service/logs.h"
#include "soa/types/string.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace RTBKIT;
using namespace Datacratic;

namespace JamLoop {

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
                    return Datacratic::Url(value);
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
    {
        this->auctionVerb = "GET";
        this->auctionResource = "/vast2";
    }

    PublisherConnector::PublisherConnector(std::string name,
            std::shared_ptr<ServiceProxies> proxies)
        : HttpExchangeConnector(std::move(name), std::move(proxies))
        , maxAuctionTime(Default::MaxAuctionTime)
    {
        this->auctionVerb = "GET";
        this->auctionResource = "/vast2";
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
        ExchangeConnector::ExchangeCompatibility compatibility;
        compatibility.setCompatible();

        return compatibility;
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

            const auto& queryParams = header.queryParams;
            extractParam(queryParams, "width", video->w);
            extractParam(queryParams, "height", video->h);
            extractParam(queryParams, "ip", device->ip);
            extractParam(queryParams, "ua", device->ua);
            extractParam(queryParams, "lang", content->language);
            extractParam(queryParams, "pageurl", site->page);

            site->content = std::move(content);
            br->device = std::move(device);
            br->site = std::move(site);
            spot.video = std::move(video);
            br->imp.push_back(std::move(spot));


        } catch (const std::exception& e) {
            LOG(Logs::error) << "Error when processing request: " << e.what();
            throw;
        }

        return br;
    }

    HttpResponse
    PublisherConnector::getResponse(
            const HttpAuctionHandler& connection,
            const Datacratic::HttpHeader& header,
            const RTBKIT::Auction& auction) const {
        return HttpResponse(204, "", "");
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
