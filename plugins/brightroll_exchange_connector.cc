/* brightroll_exchange_connector.cc
   Mathieu Stefani, 08 June 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the BrightRoll Exchange Connector
*/


#include "brightroll_exchange_connector.h"
#include "brightroll-openrtb.pb.h"
#include "rtbkit/plugins/exchange/http_auction_handler.h"
#include "rtbkit/openrtb/openrtb.h"

using namespace Datacratic;
using namespace RTBKIT;

namespace JamLoop {

    namespace Default {
        static constexpr double MaximumResponseTime = 90;
    }

    namespace {
        template<typename T, typename... Args>
        std::unique_ptr<T> make_unique(Args&& ...args) {
            return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }

        template<typename T, size_t N>
        constexpr size_t array_size(T (&arr)[N]) { return N; }
    }

    namespace BrightRoll {
        typedef ::BidRequest BidRequest;

        /* BrightRoll specific enum */
#define MIME_TYPES \
        MIME(FLV             , "video/x-flv") \
        MIME(SHOCKWAVE_FLASH , "application/x-shockwave-flash") \
        MIME(MP4             , "video/mp4") \
        MIME(TEXT_HTML       , "text/html") \
        MIME(JPG             , "image/jpeg") \
        MIME(GIF             , "image/gif") \
        MIME(PNG             , "image/png")

        bool isPing(const BidRequest& br) {
            if (!br.has_ext()) {
                return false;
            }

            const auto &ext = br.ext();
            return ext.is_ping();
        }

        OpenRTB::MimeType toMimeType(Mimes mimes) {
            static constexpr const char* mimesStrings[] = {
                 #define MIME(_, str) \
                            str
                         MIME_TYPES
                  #undef MIME
            };

            static constexpr size_t mimesSize = array_size(mimesStrings);
            auto val = static_cast<int>(mimes);
            ExcAssert(val < mimesSize);
            return OpenRTB::MimeType(mimesStrings[val]);
        }

        OpenRTB::ContentCategory toContentCategory(ContentCategory category) {
            static constexpr const char* categoriesStrings[] = {
                 #define ITEM(_, str) \
                                     str
                 #include "content_category.itm"
                 #undef ITEM
            };

            static constexpr size_t categoriesSize = array_size(categoriesStrings);
            auto val = static_cast<int>(category);
            ExcAssert(val < categoriesSize);

            return OpenRTB::ContentCategory(categoriesStrings[val]);
        }

        OpenRTB::VideoPlaybackMethod toPlaybackMethod(Playbackmethod method) {
            using OpenRTB::VideoPlaybackMethod;

            VideoPlaybackMethod result;
            if (method == PLAYBACK_METHOD_UNKNOWN) {
                result.val = VideoPlaybackMethod::UNSPECIFIED;
            } else {
                result.val = static_cast<VideoPlaybackMethod::Vals>(static_cast<int>(method));
            }

            return result;
        }

        OpenRTB::AdPosition toAdPosition(Pos pos) {
            using OpenRTB::AdPosition;

            AdPosition result;
            result.val = static_cast<AdPosition::Vals>(static_cast<int>(pos));
            return result;
        }

        namespace detail {

            template<typename Enum>
            struct is_tagged_enum {

                template<typename T>
                static std::true_type tagged_enum_test(typename T::isTaggedEnumType*);

                template<typename T>
                static std::false_type tagged_enum_test(...);

                static const bool value
                    = std::is_same<decltype(tagged_enum_test<Enum>(nullptr)), std::true_type>::value;
            };

            /* gcc > 4.6
             * template<typename Enum> using is_tagged_enum_t = is_tagged_enum<Enum>::value;
             */
        }


        template<typename To, typename From>
        To openrtb_cast(From from) {
            static_assert(detail::is_tagged_enum<To>::value, "Invalid cast");
            To result;
            result.val = static_cast<typename To::Vals>(static_cast<int>(from));

            return result;
        }

        OpenRTB::Banner toBanner(const BidRequest::Banner& banner) {
            OpenRTB::Banner result;

            result.id = Id(banner.id());
            if (banner.has_w()) {
                result.w.push_back(banner.w());
            }
            if (banner.has_h()) {
                result.h.push_back(banner.h());
            }

            if (banner.has_pos()) {
                result.pos = openrtb_cast<OpenRTB::AdPosition>(banner.pos());
            }

            for (int i = 0; i < banner.mimes_size(); ++i) {
                result.mimes.push_back(toMimeType(banner.mimes(i)));
            }

            for (int i = 0; i < banner.api_size(); ++i) {
                /* TODO: BrightRoll specific value */
            }

            if (banner.has_ext()) {
                const auto& ext = banner.ext();
                if (ext.has_minduration()) {
                    result.ext["minduration"] = ext.minduration();
                }
                if (ext.has_maxduration()) {
                    result.ext["maxduration"] = ext.maxduration();
                }
            }

            return result;

        }

        AdSpot toAdSpot(const BidRequest::Imp& imp) {
            AdSpot spot;
            spot.id = Id(imp.id());

            const auto& video = imp.video();
            
            auto v = make_unique<OpenRTB::Video>();
            for (int i = 0; i < video.mimes_size(); ++i) {
                v->mimes.push_back(toMimeType(video.mimes(i)));
            }

            if (video.has_linearity()) {
                v->linearity = openrtb_cast<OpenRTB::VideoLinearity>(video.linearity());
            }

            if (video.has_minduration()) {
                v->minduration = video.minduration();
            }

            if (video.has_maxduration()) {
                v->maxduration = video.maxduration();
            }

            for (int i = 0; i < video.protocol_size(); ++i) {
                v->protocols.push_back(
                        openrtb_cast<OpenRTB::VideoBidResponseProtocol>(video.protocol(i)));
            }

            for (int i = 0; i < video.api_size(); ++i) {
                /* TODO: BrightRoll specific value */
            }

            if (video.has_w()) {
                v->w = video.w();
            }
            if (video.has_h()) {
                v->h = video.h();
            }

            /* TODO */
            if (video.has_startdelay()) {
            }

            if (video.has_maxbitrate()) {
                v->maxbitrate = video.maxbitrate();
            }

            for (int i = 0; i < video.playbackmethod_size(); ++i) {
                v->playbackmethod.push_back(toPlaybackMethod(video.playbackmethod(i)));
            }

            for (int i = 0; i < video.delivery_size(); ++i) {
                v->delivery.push_back(
                        openrtb_cast<OpenRTB::ContentDeliveryMethod>(video.delivery(i)));
            }

            for (int i = 0; video.companiontype_size(); ++i) {
                v->companiontype.push_back(
                        openrtb_cast<OpenRTB::VastCompanionType>(video.companiontype(i)));
            }

            for (int i = 0; i < video.companionad_size(); ++i) {
                v->companionad.push_back(toBanner(video.companionad(i)));
            }

            spot.formats.push_back(Format(v->w.value(), v->h.value()));
            // spot.video = std::move(v)
            spot.video.reset(v.release());

            return spot;
        }

        Datacratic::TaggedBool toBool(State state) {
            Datacratic::TaggedBool result;

            if (state != STATE_UNKNOWN) {
                result.val = state == YES;
            }

            return result;
        }

        Datacratic::UnicodeString toContext(Context context) {
            static constexpr const char* contextStrings[] = {
                "Video",
                "Game",
                "Music",
                "Application",
                "Text",
                "Other",
                "Unknown"
            };

            static constexpr size_t contextSize = array_size(contextStrings);
            // 1-indexed
            auto val = static_cast<int>(context) - 1;
            ExcAssert(val < contextSize);

            return Datacratic::UnicodeString(contextStrings[val]);
        }

        OpenRTB::Content *
        toContent(const BidRequest::Content& content) {
            auto result = make_unique<OpenRTB::Content>();

            result->id = Id(content.id());
            if (content.has_title()) {
                result->title = Datacratic::UnicodeString(content.title());
            }
            if (content.has_url()) {
                result->url = Url(content.url());
            }
            if (content.has_contentrating()) {
                result->contentrating = Datacratic::UnicodeString(content.contentrating());
            }
            for (int i = 0; i < content.cat_size(); ++i) {
                result->cat.push_back(toContentCategory(content.cat(i)));
            }
            if (content.has_keywords()) {
                // TODO: CSList
            }
            if (content.has_context()) {
                result->context = toContext(content.context());
            }
            if (content.has_len()) {
                result->len = content.len();
            }

            if (content.has_qagmediarating()) {
                auto rating = content.qagmediarating();
                if (rating != MATURITY_RATING_UNKNOWN) {
                    result->qagmediarating = openrtb_cast<OpenRTB::MediaRating>(rating);
                }
            }
            if (content.has_embeddable()) {
                auto val = content.embeddable();
                if (val != STATE_UNKNOWN) {
                    result->embeddable = openrtb_cast<OpenRTB::Embeddable>(val);
                }
            }

            if (content.has_language()) {
                result->language = content.language();
            }

            return result.release();
        }

        OpenRTB::Publisher *
        toPublisher(const BidRequest::Publisher& publisher) {
            auto result = make_unique<OpenRTB::Publisher>();
            result->id = Id(publisher.id());

            if (publisher.has_name()) {
                result->name = Datacratic::UnicodeString(publisher.name());
            }
            if (publisher.has_cat()) {
                result->cat.push_back(toContentCategory(publisher.cat()));
            }
            if (publisher.has_domain()) {
                result->domain = Datacratic::UnicodeString(publisher.domain());
            }

            return result.release();
        }

        /* Parse common fields between site and app */
        template<typename Result, typename Obj>
        void parseCommon(const Obj &obj, Result* result) {
            for (int i = 0; i < obj.cat_size(); ++i) {
                result->cat.push_back(toContentCategory(obj.cat(i)));
            }
            
            for (int i = 0; i < obj.sectioncat_size(); ++i) {
                result->sectioncat.push_back(toContentCategory(obj.sectioncat(i)));
            }

            for (int i = 0; i < obj.pagecat_size(); ++i) {
                result->pagecat.push_back(toContentCategory(obj.pagecat(i)));
            }

            if (obj.has_privacypolicy()) {
                auto policy = obj.privacypolicy();
                result->privacypolicy = toBool(policy);
            }

            if (obj.has_keywords()) {
                // TODO: CSList
            }
            if (obj.has_name()) {
                result->name = Datacratic::UnicodeString(obj.name());
            }
            if (obj.has_domain()) {
                result->domain = Datacratic::UnicodeString(obj.domain());
            }
            if (obj.has_content()) {
                result->content.reset(toContent(obj.content()));
            }
            if (obj.has_publisher()) {
                result->publisher.reset(toPublisher(obj.publisher()));
            }
        }


        OpenRTB::App *
        toApp(const BidRequest::App& app) {
            auto result = make_unique<OpenRTB::App>();
            result->id = Id(app.id());
            parseCommon(app, result.get());

            if (app.has_bundle()) {
                result->bundle = Datacratic::UnicodeString(app.bundle());
            }
            if (app.has_paid()) {
                result->paid = toBool(app.paid());
            }
            if (app.has_storeurl()) {
                result->storeurl = Datacratic::Url(app.storeurl());
            }

            return result.release();
        }

        OpenRTB::Site *
        toSite(const BidRequest::Site& site) {
            auto result = make_unique<OpenRTB::Site>();
            result->id = Id(site.id());
            parseCommon(site, result.get());

            if (site.has_page()) {
                result->page = Url(site.page());
            }

            if (site.has_ref()) {
                result->ref = Url(site.ref());
            }
            if (site.has_search()) {
                result->search = Datacratic::UnicodeString(site.search());
            }

            return result.release();
        }

        std::shared_ptr<RTBKIT::BidRequest>
        toInternalBidRequest(BidRequest&& request) {
            auto result = std::make_shared<RTBKIT::BidRequest>();

            result->auctionId = Id(request.id());
            result->auctionType = AuctionType::SECOND_PRICE;
            if (!request.has_tmax()) {
                result->timeAvailableMs = Default::MaximumResponseTime;
            } else {
                result->timeAvailableMs = request.tmax();
            }

            result->timestamp = Date::now();
            if (!request.has_ext()) {
                result->isTest = false;
            } else {
                result->isTest = request.ext().is_test();
            }

            /* BrightRoll only supports one impression */
            result->imp.push_back(toAdSpot(request.imp()));

            if (request.has_site()) {
                result->site.reset(toSite(request.site()));
            }
            if (request.has_app()) {
                result->app.reset(toApp(request.app()));
            }
            return result;

        }

    }

    BrightRollExchangeConnector::BrightRollExchangeConnector(
            ServiceBase &owner, std::string name)
        : HttpExchangeConnector(std::move(name), owner)
    {
    }

    BrightRollExchangeConnector::BrightRollExchangeConnector(
            std::string name, std::shared_ptr<ServiceProxies> proxies)
        : HttpExchangeConnector(std::move(name), std::move(proxies))
    {
    }

    ExchangeConnector::ExchangeCompatibility
    BrightRollExchangeConnector::getCampaignCompatibility(
            const AgentConfig& config,
            bool includeReasons) const
    {
        ExchangeCompatibility compatibility;
        compatibility.setCompatible();

        return compatibility;
    }

    ExchangeConnector::ExchangeCompatibility
    BrightRollExchangeConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const
    {
        ExchangeCompatibility compatibility;
        compatibility.setCompatible();

        return compatibility;
    }

    std::shared_ptr<RTBKIT::BidRequest>
    BrightRollExchangeConnector::parseBidRequest(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        if (header.contentType != "application/octet-stream") {
            handler.sendErrorResponse("Bad HTTP Content-Type Header");
            return nullptr;
        }  

        BrightRoll::BidRequest request;
        if (!request.ParseFromString(payload)) {
            handler.sendErrorResponse("Error parsing BidRequest");
            return nullptr;
        }

        if (BrightRoll::isPing(request)) {
            handler.dropAuction();
            return nullptr;
        }

        return BrightRoll::toInternalBidRequest(std::move(request));
    } 

    double
    BrightRollExchangeConnector::getTimeAvailableMs(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        return Default::MaximumResponseTime;
    }

    HttpResponse
    BrightRollExchangeConnector::getResponse(
            const HttpAuctionHandler& connection,
            const HttpHeader& header,
            const Auction& auction) const
    {
        return HttpResponse(204, "none", "");
    }


} // namespace JamLoop
