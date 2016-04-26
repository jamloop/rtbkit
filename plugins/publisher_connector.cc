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

#define VIDEO_TYPES \
    VIDEO_TYPE(Instream    , "instream")     \
    VIDEO_TYPE(Outstream   , "outstream")    \
    VIDEO_TYPE(Inbanner    , "inbanner")     \
    VIDEO_TYPE(Interstitial, "interstitial") \
    VIDEO_TYPE(Ingame      , "ingame")       \
    VIDEO_TYPE(Inapp       , "inapp")

#define DEVICE_IDS \
    DEVICE_ID(Idfa    , "idfa") \
    DEVICE_ID(IdfaMd5 , "idfa_md5") \
    DEVICE_ID(IdfaSha1, "idfa_sha1") \
    DEVICE_ID(Aid     , "aid") \
    DEVICE_ID(AidMd5  , "aid_md5") \
    DEVICE_ID(AidSha1 , "aid_sha1")

    enum class VideoType {
    #define VIDEO_TYPE(val, _) val,
        VIDEO_TYPES
    #undef VIDEO_TYPE
    };

    enum class DeviceId {
    #define DEVICE_ID(val, _) val,
        DEVICE_IDS
    #undef DEVICE_ID
    };

    const char *videoTypeString(VideoType type) {
        switch (type) {
        #define VIDEO_TYPE(val, str) \
            case VideoType::val: return str;
        VIDEO_TYPES
        #undef VIDEO_TYPE
        }

        return "";
    }

    const char *deviceIdString(DeviceId id) {
        switch (id) {
        #define DEVICE_ID(val, str) \
            case DeviceId::val: return str;
        DEVICE_IDS
        #undef DEVICE_ID
        }

        return "";
    }

    enum class Flag {
        Required,
        Optional
    };

    enum class ParamResult {
        Invalid,
        NotFound,
        Empty,
        Ok
    };

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
                static std::pair<bool, T> cast(const std::string& value) {
                    std::istringstream iss(value);
                    T ret;
                    if (!(iss >> ret)) {
                        return std::make_pair(false, T());
                    }

                    return std::make_pair(true, ret);
                }
            };

            template<> struct LexicalCast<Datacratic::UnicodeString>
            {
                static std::pair<bool, Datacratic::UnicodeString> cast(const std::string& value) {
                    return std::make_pair(true, Datacratic::UnicodeString(value));
                }
            };

            template<> struct LexicalCast<Datacratic::Url>
            {
                static std::pair<bool, Datacratic::Url> cast(const std::string& value) {
                    return std::make_pair(true, Datacratic::Url(urldecode(value)));
                }
            };

            template<> struct LexicalCast<Datacratic::TaggedInt>
            {
                static std::pair<bool, Datacratic::TaggedInt> cast(const std::string& value) {
                    Datacratic::TaggedInt val;
                    auto ret = LexicalCast<int>::cast(value);
                    if (!ret.first)
                        return std::make_pair(false, val);

                    val.val = ret.second;
                    return std::make_pair(true, val);
                }
            };

            template<> struct LexicalCast<OpenRTB::AdPosition>
            {
                static OpenRTB::AdPosition cast(const std::string& value) {
                    OpenRTB::AdPosition pos;

                    return pos;
                }
            };

            template<> struct LexicalCast<VideoType>
            {
                static std::pair<bool, VideoType> cast(const std::string& value) {
                #define VIDEO_TYPE(val, str) \
                    if (value == str) return std::make_pair(true, VideoType::val);
                VIDEO_TYPES
                #undef VIDEO_TYPE
                    return std::make_pair(false, VideoType());
                }
            };

            template<> struct LexicalCast<DeviceId>
            {
                static std::pair<bool, DeviceId> cast(const std::string& value) {
                #define DEVICE_ID(val, str) \
                    if (value == str) return std::make_pair(true, DeviceId::val);
                    DEVICE_IDS
                #undef DEVICE_ID
                        return std::make_pair(false, DeviceId());
                }
            };

            template<> struct LexicalCast<OpenRTB::DeviceType>
            {
                static std::pair<bool, OpenRTB::DeviceType> cast(const std::string& value) {
                    OpenRTB::DeviceType res;
                    if (value == "2" || value == "desktop") {
                        res.val = static_cast<int>(OpenRTB::DeviceType::Vals::PC);
                    }
                    else if (value == "3" || value == "ctv") {
                        res.val = static_cast<int>(OpenRTB::DeviceType::Vals::TV);
                    }
                    else if (value == "4" || value == "phone") {
                        res.val = static_cast<int>(OpenRTB::DeviceType::Vals::PHONE);
                    }
                    else if (value == "5" || value == "tablet") {
                        res.val = static_cast<int>(OpenRTB::DeviceType::Vals::TABLET);
                    }
                    else {
                        return std::make_pair(false, res);
                    }

                    return std::make_pair(true, res);
                }
            };
        }

        template<typename Param>
        std::pair<bool, Param> param_cast(const std::string& value) {
            return detail::LexicalCast<Param>::cast(value);
        } 

        template<typename Param>
        ParamResult extractParam(const RestParams& params, const char* name, Param& out, Param defaultValue = Param(), Flag flag = Flag::Optional) {
            if (!params.hasValue(name)) {
                if (flag == Flag::Required) {
                    throw ML::Exception("Could not find required query param '%s'", name);
                }
                return ParamResult::NotFound;
            }

            auto value = params.getValue(name);
            if (value.empty()) {
                out = defaultValue;
                return ParamResult::Empty;
            }

            auto ret = param_cast<Param>(value);
            if (!ret.first)
                return ParamResult::Invalid;

            out = std::move(ret.second);
            return ParamResult::Ok;
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
            "exchange",
            [](const PublisherCreativeConfiguration::Context& context) {
                return std::string("publisher_") + context.bidrequest.user->id.toString();
        });

        creativeConfig.addExpanderVariable(
            "tag.price",
            [](const PublisherCreativeConfiguration::Context& context) {
                const double price = context.bidrequest.ext["price"].asDouble();
                return std::to_string(price / 1000.0);
        });

        creativeConfig.addExpanderVariable(
            "bidrequest.pos",
            [](const PublisherCreativeConfiguration::Context& context) {
                const auto& br = context.bidrequest;
                if (br.device) {
                    using OpenRTB::DeviceType;
                    auto type = static_cast<DeviceType::Vals>(br.device->devicetype.val);
                    switch (type) {
                    case DeviceType::Vals::PC:
                        return std::string("pc");
                    case DeviceType::Vals::TV:
                        return std::string("tv");
                    case DeviceType::Vals::PHONE:
                        return std::string("phone");
                    case DeviceType::Vals::TABLET:
                        return std::string("tablet");
                    }
                }

                return std::string();
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

        {
            auto failure = ScopeFailure([&]() noexcept { br.reset(); });

#define TRY_EXTRACT(name, out)                                          \
            [&]() {                                                     \
                auto ret = extractParam(header.queryParams, name, out); \
                if (ret == ParamResult::Invalid) {                      \
                    fail(failure, [&]() {                               \
                        recordHit("invalid.total");                     \
                        recordHit("invalid.%s", name);                  \
                    });                                                 \
                    handler.dropAuction();                              \
                    return false;                                       \
                }                                                       \
                else if (ret == ParamResult::Ok) {                      \
                    return true;                                        \
                }                                                       \
                return false;                                           \
            }();                                                        \
            if (!failure.ok()) goto end;                                \
            (void) 0


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

            Datacratic::Optional<OpenRTB::User> user;
            user.reset(new OpenRTB::User());

            int width, height;
            width = height = -1;
            double lat, lon;
            lat = lon = std::numeric_limits<double>::quiet_NaN();
            VideoType videoType;
            DeviceId did;
            /* App fields */
            Datacratic::UnicodeString appName;
            Datacratic::Url appStoreUrl;
            Datacratic::UnicodeString appBundle;

            /* Site fields */
            Datacratic::Url pageUrl;

            /* Content fields */
            std::string language;

            std::string partner;

            const auto& queryParams = header.queryParams;
            TRY_EXTRACT("width", width);
            TRY_EXTRACT("height", height);
            TRY_EXTRACT("ip", device->ip);
            TRY_EXTRACT("ua", device->ua);
            TRY_EXTRACT("devicetype", device->devicetype);

            auto hasLang = TRY_EXTRACT("lang", language);

            TRY_EXTRACT("partner", partner);

            auto hasPageUrl     = TRY_EXTRACT("pageurl", pageUrl);
            auto hasAppStoreUrl = TRY_EXTRACT("app_storeurl", appStoreUrl);
            auto hasAppBundle   = TRY_EXTRACT("app_bundle", appBundle);
            auto hasAppName     = TRY_EXTRACT("appName", appName);


            if (hasPageUrl) {
                br->site.reset(new OpenRTB::Site);
                br->site->page = std::move(pageUrl);
                if (hasLang) {
                    br->site->content.reset(new OpenRTB::Content);
                    br->site->content->language = language;
                }
                br->url = br->site->page;
            }

            const auto hasApp = hasAppStoreUrl || hasAppBundle || hasAppName;
            if (hasApp) {
                br->app.reset(new OpenRTB::App);
                br->app->storeurl = std::move(appStoreUrl);
                br->app->bundle = std::move(appBundle);
                br->app->name = std::move(appName);
                if (hasLang) {
                    br->app->content.reset(new OpenRTB::Content);
                    br->app->content->language = language;
                }
            }

            auto hasVideoType = TRY_EXTRACT("videotype", videoType);
            auto hasDeviceId  = TRY_EXTRACT("deviceid", did);

            if (hasVideoType) {
                br->ext["videotype"] = videoTypeString(videoType);
            }
            if (hasDeviceId) {
                br->ext["deviceid"] = deviceIdString(did);
            }

            auto pos = partner.find('_');
            if (pos != std::string::npos) {
                user->id = Id(partner.substr(0, pos));
            } else {
                user->id = Id(partner);
            }

            auto hasLat = TRY_EXTRACT("lat", lat);
            auto hasLon = TRY_EXTRACT("lon", lon);
            const auto hasGeo = hasLat || hasLon;

            video->w.val = width;
            video->h.val = height;

            if (hasGeo) {
                user->geo.reset(new OpenRTB::Geo);
                user->geo->lat.val = lat;
                user->geo->lon.val = lon;
            }

            double price = 0.0;
            TRY_EXTRACT("price", price);
            br->ext["price"] = price * 1000.0;

            br->device = std::move(device);
            br->user = std::move(user);
            br->userAgent = br->device->ua;
            spot.video = std::move(video);
            spot.formats.push_back(Format(width, height));
            br->imp.push_back(std::move(spot));
            br->exchange = "publisher";

        }

        end:
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

