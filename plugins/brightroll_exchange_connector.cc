/* brightroll_exchange_connector.cc
   Mathieu Stefani, 08 June 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the BrightRoll Exchange Connector
*/


#include "brightroll_exchange_connector.h"
#include "brightroll-openrtb.pb.h"
#include "rtbkit/plugins/exchange/http_auction_handler.h"
#include "rtbkit/openrtb/openrtb.h"
#include "soa/service/logs.h"

using namespace Datacratic;
using namespace RTBKIT;

namespace JamLoop {

    namespace Default {
        static constexpr double MaximumResponseTime = 90;
    }

    namespace {
        #define GCC_VERSION (__GNUC__ * 10000 \
                               + __GNUC_MINOR__ * 100 \
                               + __GNUC_PATCHLEVEL__)

        /* gcc implements make_unique in 4.9 */
        #if GCC_VERSION < 40900
            template<typename T, typename... Args>
            std::unique_ptr<T> make_unique(Args&& ...args) {
                return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
            }
        #else
            using std::make_unique;
        #endif

        template<typename T, size_t N>
        constexpr size_t array_size(T (&arr)[N]) { return N; }

        constexpr size_t str_size_tail_rec(const char* str, size_t acc) {
            return *str == 0 ? acc : str_size_tail_rec(str + 1, acc + 1);
        }

        constexpr size_t str_size(const char* str) {
            return str_size_tail_rec(str, 0);
        }

    }

    namespace BrightRoll {
        typedef ::BidRequest BidRequest;
        typedef ::BidResponse BidResponse;
        typedef BidResponse::Bid Bid;


        static Logging::Category trace("BrightRoll Bid Request");
        static Logging::Category error("BrightRoll Bid Request Error", trace);

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

        OpenRTB::MimeType
        toMimeType(Mimes mimes) {
            static constexpr const char* mimesStrings[] = {
                 #define MIME(_, str) \
                            str,
                         MIME_TYPES
                  #undef MIME
            };

            static constexpr size_t mimesSize = array_size(mimesStrings);
            auto val = static_cast<int>(mimes);
            ExcAssert(val < mimesSize);
            return OpenRTB::MimeType(mimesStrings[val]);
        }

        Mimes
        toMimes(OpenRTB::MimeType mimeType) {
            struct MimeInfo {
                const char *str;
                Mimes type;
            };

            static constexpr MimeInfo mimes[] = {
               #define MIME(m, str) \
                         { str, m },
                       MIME_TYPES
                #undef MIME
            };

            static constexpr size_t size = sizeof (mimes) / sizeof (*mimes);

            for (size_t i = 0; i < size; ++i) {
                const auto *m = mimes + i;
                if (mimeType.type == m->str) {
                    return m->type;
                }
            }

            throw ML::Exception("Unknown Mime '%s'", mimeType.type.c_str());
        }


        OpenRTB::ContentCategory
        toContentCategory(ContentCategory category) {

            /* Note:
                Currently, the function uses the full category string as the
                ContentCategory, for example "Movies"

                To use the IAB Identifier, just replace the macro underneath
                by #define ITEM(id, _) #id, which will use a stringified version
                of the category identifiers, for example "IAB1_5"
            */

            static constexpr const char* categoriesStrings[] = {
                 #define ITEM(_, str) \
                                     str,
                 #include "content_category.itm"
                 #undef ITEM
            };

            static constexpr size_t categoriesSize = array_size(categoriesStrings);
            auto val = static_cast<int>(category);
            ExcAssert(val < categoriesSize);

            return OpenRTB::ContentCategory(categoriesStrings[val]);
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

        template<typename To, typename From>
        To brightroll_cast(From from) {
            static_assert(detail::is_tagged_enum<From>::value, "Invalid cast");

            return static_cast<To>(static_cast<int>(from.val));
        }

        OpenRTB::Banner
        toBanner(const BidRequest::Banner& banner) {
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
                auto val = banner.api(i);
                if (val == BR_HTML5_1_0 || val == BR_HTML5_2_0) {
                    OpenRTB::ApiFramework api;
                    api.val = static_cast<int>(val);
                    result.api.push_back(api);
                }
                else {
                    result.api.push_back(openrtb_cast<OpenRTB::ApiFramework>(val));
                }
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

        AdSpot
        toAdSpot(const BidRequest::Imp& imp) {
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
                auto val = video.api(i);
                if (val == BR_HTML5_1_0 || val == BR_HTML5_2_0) {
                    OpenRTB::ApiFramework api;
                    api.val = static_cast<int>(val);
                    v->api.push_back(api);
                }
                else {
                    v->api.push_back(openrtb_cast<OpenRTB::ApiFramework>(val));
                }
            }

            if (video.has_w()) {
                v->w = video.w();
            }
            if (video.has_h()) {
                v->h = video.h();
            }

            if (video.has_startdelay()) {
                v->startdelay.val = video.startdelay();
            }

            if (video.has_maxbitrate()) {
                v->maxbitrate = video.maxbitrate();
            }

            for (int i = 0; i < video.playbackmethod_size(); ++i) {
                auto val = video.playbackmethod(i);
                if (val != PLAYBACK_METHOD_UNKNOWN) {
                    v->playbackmethod.push_back(
                            openrtb_cast<OpenRTB::VideoPlaybackMethod>(val));
                }
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
            spot.video.reset(v.release());

            return spot;
        }

        Datacratic::TaggedBool
        toBool(State state) {
            Datacratic::TaggedBool result;

            if (state != STATE_UNKNOWN) {
                result.val = state == YES;
            }

            return result;
        }

        Datacratic::UnicodeString
        toContext(Context context) {
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
                result->keywords = Datacratic::UnicodeString(content.keywords());
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
                result->keywords = Datacratic::UnicodeString(obj.keywords());
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

            if (app.has_ver()) {
                result->ver = app.ver();
            }
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

        OpenRTB::Geo *
        toGeo(const BidRequest::Geo& geo) {
            auto result = make_unique<OpenRTB::Geo>();

            if (geo.has_lat()) {
                result->lat = geo.lat();
            }
            if (geo.has_lon()) {
                result->lon = geo.lon();
            }
            if (geo.has_country()) {
                result->country = geo.country();
            }
            if (geo.has_region()) {
                result->region = geo.region();
            }
            if (geo.has_regionfips104()) {
                result->regionfips104 = geo.regionfips104();
            }
            if (geo.has_metro()) {
                result->metro = geo.metro();
            }
            if (geo.has_city()) {
                result->city = Datacratic::UnicodeString(geo.city());
            }
            if (geo.has_zip()) {
                result->zip = Datacratic::UnicodeString(geo.zip());
            }
            if (geo.has_type()) {
                auto val = geo.type();
                if (val != GEOTYPE_UNKNOWN) {
                    result->type
                        = openrtb_cast<OpenRTB::LocationType>(geo.type());
                }
            }

            return result.release();
        }

        OpenRTB::Device *
        toDevice(const BidRequest::Device& device) {
            auto result = make_unique<OpenRTB::Device>();

            if (device.has_dnt()) {
                result->dnt = toBool(device.dnt());
            }
            if (device.has_ip()) {
                result->ip = device.ip();
            }
            if (device.has_carrier()) {
                result->carrier = device.carrier();
            }
            if (device.has_ua()) {
                result->ua = Datacratic::UnicodeString(device.ua());
            }
            if (device.has_language()) {
                result->language = Datacratic::UnicodeString(device.language());
            }
            if (device.has_make()) {
                result->make = Datacratic::UnicodeString(device.make());
            }
            if (device.has_model()) {
                result->model = Datacratic::UnicodeString(device.model());
            }
            if (device.has_os()) {
                result->os = Datacratic::UnicodeString(device.os());
            }
            if (device.has_osv()) {
                result->osv = Datacratic::UnicodeString(device.osv());
            }
            if (device.has_connectiontype()) {
                result->connectiontype
                    = openrtb_cast<OpenRTB::ConnectionType>(device.connectiontype());
            }
            if (device.has_devicetype()) {
                result->devicetype
                    = openrtb_cast<OpenRTB::DeviceType>(device.devicetype());
            }
            if (device.has_geo()) {
                result->geo.reset(toGeo(device.geo()));
            }
            if (device.has_ipv6()) {
                result->ipv6 = device.ipv6();
            }
            if (device.has_didsha1()) {
                result->didsha1 = device.didsha1();
            }
            if (device.has_didmd5()) {
                result->didmd5 = device.didmd5();
            }
            if (device.has_dpidsha1()) {
                result->dpidsha1 = device.dpidsha1();
            }
            if (device.has_dpidmd5()) {
                result->dpidmd5 = device.dpidmd5();
            }

            return result.release();

        }

        OpenRTB::User *
        toUser(const BidRequest::User& user) {
            auto result = make_unique<OpenRTB::User>();

            result->id = Datacratic::Id(user.id());
            if (user.has_buyeruid()) {
                result->buyeruid = Datacratic::Id(user.buyeruid());
            }
            if (user.has_yob()) {
                // BrightRoll sends the Year of Birth as a string
                auto yob = user.yob();
                try {
                    result->yob = std::stoi(yob);
                } catch (const std::invalid_argument&) {
                    THROW(error) << "Invalid year of birth '" << yob << "'" << std::endl;
                }
            }
            if (user.has_gender()) {
                result->gender = user.gender();
            }
            if (user.has_geo()) {
                result->geo.reset(toGeo(user.geo()));
            }

            return result.release();
        }

        Json::Value
        toExt(const BidRequest::Ext& ext) {
            Json::Value ret;

            auto stateBool = [](State state) {
                if (state == STATE_UNKNOWN || state == NO)
                    return false;

                return true;
            };

            if (ext.has_is_test()) {
                ret["is_test"] = ext.is_test();
            }
            if (ext.has_is_ping()) {
                ret["is_ping"] = ext.is_ping();
            }
            if (ext.has_is_skippable()) {
                ret["is_skippable"] = stateBool(ext.is_skippable());
            }
            if (ext.has_skip_offset()) {
                ret["skip_offset"] = ext.skip_offset();
            }
            if (ext.has_is_fullscreenexpandable()) {
                ret["is_fullscreenexpandable"] = ext.is_fullscreenexpandable();
            }
            if (ext.has_is_facebook()) {
                ret["is_facebook"] = ext.is_facebook();
            }
            if (ext.has_is_incentivized()) {
                ret["is_incentivized"] = stateBool(ext.is_incentivized());
            }
            if (ext.has_is_syndicated()) {
                ret["is_syndicated"] = stateBool(ext.is_syndicated());
            }
            if (ext.has_is_ugc()) {
                ret["is_ugc"] = stateBool(ext.is_ugc());
            }
            if (ext.has_max_wrapper_redirects()) {
                ret["max_wrapper_redirects"] = ext.max_wrapper_redirects();
            }
            if (ext.has_inventory_class()) {
                auto inventory_class = [&ext]() -> const char * const {
                    switch (ext.inventory_class()) {
                        case INVENTORYCLASS_UNKNOWN:
                            return "unknown";
                        case REACH:
                            return "reach";
                        case PREMIUM:
                            return "premium";
                        case SUPERPREMIUM:
                            return "superpremium";
                    }
                    return nullptr;
                }();

                ret["inventory_class"] = inventory_class;

            }
            if (ext.has_ifa()) {
                ret["ifa"] = ext.ifa();
            }
            if (ext.has_viewability()) {
                ret["viewability"] = ext.viewability();
            }
            if (ext.has_xdid()) {
                ret["xdid"] = ext.xdid();
            }
            if (ext.has_secure()) {
                ret["secure"] = ext.secure();
            }

            return ret;

        }

        void generateProviderId(std::shared_ptr<RTBKIT::BidRequest>& req) {
            if (req->device && !req->device->ip.empty() && !req->device->ua.empty()) {
                auto toHash = req->device->ip + req->device->ua.rawString();
                req->userAgentIPHash = Id(CityHash64(toHash.c_str(), toHash.length()));
                req->userIds.add(req->userAgentIPHash, ID_PROVIDER);
            }
            else
                req->userIds.add(Id(0), ID_PROVIDER);
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

            if (request.wseat_size() > 0) {
                std::vector<std::string> wseat;
                wseat.reserve(request.wseat_size());
                for (int i = 0; i < request.wseat_size(); ++i) {
                    wseat.push_back(request.wseat(i));
                }
                result->segments.addStrings("openrtb-wseat", std::move(wseat));
            }
            if (request.bcat_size() > 0) {
                result->blockedCategories.reserve(request.bcat_size());
                for (int i = 0; i < request.bcat_size(); ++i) {
                    result->blockedCategories.push_back(toContentCategory(request.bcat(i)));
                }
            }
            if (request.badv_size() > 0) {
                std::vector<std::string> badv;
                badv.reserve(request.badv_size());
                for (int i = 0; i < request.badv_size(); ++i) {
                    auto val = request.badv(i);
                    badv.push_back(val);
                    result->badv.push_back(Datacratic::UnicodeString(val));
                }
                result->restrictions.addStrings("badv", badv);
            }

            /* BrightRoll only supports one impression */
            result->imp.push_back(toAdSpot(request.imp()));

            if (request.has_site()) {
                result->site.reset(toSite(request.site()));

                if (!result->site->page.empty()) {
                    result->url = result->site->page;
                }
                else if (result->site->id) {
                    result->url = Url("http://" + result->site->id.toString() + ".siteid/");
                }

                // Adding IAB categories to segments
                for(const auto& v : result->site->cat) {
                    result->segments.add("iab-categories", v.val);
                }
            }
            if (request.has_app()) {
                result->app.reset(toApp(request.app()));

                if (!result->app->bundle.empty()) {
                    result->url = Url(result->app->bundle);
                }
                else if (result->app->id) {
                    result->url = Url("http://" + result->app->id.toString() + ".appid/");
                }

                // Adding IAB categories to segments
                for(const auto& v : result->app->cat) {
                    result->segments.add("iab-categories", v.val);
                }
            }
            if (request.has_device()) {
                result->device.reset(toDevice(request.device()));
                auto device = result->device;

                result->language = device->language;
                result->userAgent = device->ua;
                if (!device->ip.empty()) {
                    result->ipAddress = device->ip;
                }
                else if (!device->ipv6.empty()) {
                    result->ipAddress = device->ipv6;
                }

                if (device->geo) {
                    auto geo = device->geo;
                    auto& loc = result->location;
                    loc.countryCode = geo->country;
                    if (!geo->region.empty()) {
                        loc.regionCode = geo->region;
                    }
                    else {
                        loc.regionCode = geo->regionfips104;
                    }
                    loc.cityName = geo->city;
                    loc.postalCode = geo->zip;
                }
            }
            if (request.has_user()) {
                result->user.reset(toUser(request.user()));

                if (result->user->tz.val != -1) {
                    result->location.timezoneOffsetMinutes = result->user->tz.val;
                }
                if (result->user->id) {
                    result->userIds.add(result->user->id, ID_EXCHANGE);
                }

                if (result->user->buyeruid) {
                    result->userIds.add(result->user->buyeruid, ID_PROVIDER);
                }
                else if (result->user->id) {
                    result->userIds.add(result->user->id, ID_PROVIDER);
                }
                else {
                    generateProviderId(result);
                }

                if (result->user->geo) {
                    auto geo = result->user->geo;
                    auto& loc = result->location;
                    if (loc.countryCode.empty() && !geo->country.empty()) {
                        loc.countryCode = geo->country;
                    }
                    if (loc.regionCode.empty() && !geo->region.empty()) {
                        loc.regionCode = geo->region;
                    }
                    if (loc.cityName.empty() && !geo->city.empty()) {
                        loc.cityName = geo->city;
                    }
                    if (loc.postalCode.empty() && !geo->zip.empty()) {
                        loc.postalCode = geo->zip;
                    }
                }
            }
            else {
                // No User so we generate a PROVIDER_ID to be able to identify the user
                generateProviderId(result);
            }

            // BrightRoll only supports USD
            result->bidCurrency.push_back(CurrencyCode::CC_USD);

            if (request.has_ext()) {
                result->ext = toExt(request.ext());
            }


            return result;

        }

    }

    BrightRollExchangeConnector::BrightRollExchangeConnector(
            ServiceBase &owner, std::string name)
        : HttpExchangeConnector(std::move(name), owner)
        , creativeConfig(exchangeName())
    {
        initCreativeConfiguration();
    }

    BrightRollExchangeConnector::BrightRollExchangeConnector(
            std::string name, std::shared_ptr<ServiceProxies> proxies)
        : HttpExchangeConnector(std::move(name), std::move(proxies))
        , creativeConfig(exchangeName())
    {
        initCreativeConfiguration();
    }

    void
    BrightRollExchangeConnector::initCreativeConfiguration()
    {
        creativeConfig.addField(
            "nurl",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.nurl);

                if (info.nurl.empty()) {
                    throw std::invalid_argument("nurl is required");
                }

                return true;
        }).snippet().required();

        creativeConfig.addField(
            "adomain",
            [](const Json::Value& value, CreativeInfo& info) {
                std::string adomain;
                Datacratic::jsonDecode(value, adomain);

                if (adomain.empty()) {
                    throw std::invalid_argument("adomain is required");
                }

                static constexpr const char* Http = "http://";
                static constexpr size_t HttpSize = str_size(Http);

                // Remove http:// from the string
                if (!adomain.compare(0, HttpSize, Http)) {
                    info.adomain = adomain.substr(HttpSize);
                }

                return true;
        }).required();

        creativeConfig.addField(
            "campaign_name",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.campaign_name);

                if (info.campaign_name.empty()) {
                    throw std::invalid_argument("campaign_name is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "line_item_name",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.line_item_name);

                if (info.line_item_name.empty()) {
                    throw std::invalid_argument("line_item_name is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "creative_name",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.creative_name);

                if (info.creative_name.empty()) {
                    throw std::invalid_argument("creative_name is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "creative_duration",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.creative_duration);

                return true;
        }).required();

        creativeConfig.addField(
            "media_desc.media_mime",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.media_desc.media_mime);

                if (info.media_desc.media_mime.empty()) {
                    throw std::invalid_argument("media_mime is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "media_desc.media_bitrate",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.media_desc.media_bitrate);

                return true;
        }).required();

        creativeConfig.addField(
            "api",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.api);

                return true;
        }).required();

        creativeConfig.addField(
            "lid",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.lid);

                if (info.lid.empty()) {
                    throw std::invalid_argument("lid is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "landingpage_url",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.landingpage_url);

                if (info.landingpage_url.empty()) {
                    throw std::invalid_argument("landingpage_url is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "advertiser_name",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.advertiser_name);

                if (info.advertiser_name.empty()) {
                    throw std::invalid_argument("advertiser_name is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "companiontype",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.companiontype);

                return true;
        }).optional();

        creativeConfig.addField(
            "adtype",
            [](const Json::Value& value, CreativeInfo& info) {
                std::string adtype;
                Datacratic::jsonDecode(value, adtype);

                if (adtype == "video") {
                    info.adtype = ADTYPE_VIDEO;
                }
                else if (adtype == "banner") {
                    info.adtype = ADTYPE_BANNER;
                }
                else {
                    throw std::invalid_argument(ML::format("Invalid adtype '%s', must be either video or banner",
                                                adtype.c_str()));
                }

                return true;
        }).defaultTo("video");


    }

    ExchangeConnector::ExchangeCompatibility
    BrightRollExchangeConnector::getCampaignCompatibility(
            const AgentConfig& config,
            bool includeReasons) const
    {
        using std::begin;
        using std::end;

        ExchangeCompatibility result;
        result.setCompatible();

        std::string exchange = exchangeName();
        const char* name = exchange.c_str();

        if (!config.providerConfig.isMember(name)) {
            result.setIncompatible(
                    ML::format("providerConfig.%s is null", name), includeReasons);
            return result;
        }

        auto provConf = config.providerConfig[name];
        if (!provConf.isMember("seat")) {
            result.setIncompatible(
                    ML::format("providerConfig.%s.seat does not exist", name), includeReasons);
            return result;
        }

        auto seat = provConf["seat"].asString();
        if (!std::all_of(begin(seat), end(seat),
                  [](char c) { return std::isalpha(c) || std::isdigit(c); })) {
            result.setIncompatible(
                    ML::format("providerConfig.%s.seat must be either numeric "
                               "or alphanumeric", name), includeReasons);
            return result;
        }

        auto info = std::make_shared<CampaignInfo>();
        info->seat = std::move(seat);
        result.info = std::move(info);

        return result;
    }

    ExchangeConnector::ExchangeCompatibility
    BrightRollExchangeConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const
    {
        return creativeConfig.handleCreativeCompatibility(creative, includeReasons);
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
        // @Todo: to be fully compliant, we chould check if the BidRequest has a tmax
        return Default::MaximumResponseTime;
    }

    HttpResponse
    BrightRollExchangeConnector::getResponse(
            const HttpAuctionHandler& connection,
            const HttpHeader& header,
            const Auction& auction) const
    {
        const Auction::Data* current = auction.getCurrentData();
        if (current->hasError()) {
            return getErrorResponse(connection, current->error + ": " + current->details);
        }

        BrightRoll::BidResponse response;
        response.set_id(auction.id.toString());
        // BrightRoll only supports USD -- return a value of "USD".
        response.set_cur("USD");

        for (size_t spotNum = 0; spotNum < current->responses.size(); ++spotNum) {
            if (!current->hasValidResponse(spotNum))
                continue;

            setSeatBid(auction, spotNum, response);
        }

        if (response.seatbid_size() == 0) {
            return HttpResponse(204, "none", "");
        }

        std::string payload;
        response.SerializeToString(&payload);

        return HttpResponse(200, "application/octet-stream", payload);
    }

    void
    BrightRollExchangeConnector::setSeatBid(
            const Auction& auction,
            size_t spotNum,
            BrightRoll::BidResponse& response) const
    {
        const Auction::Data *current = auction.getCurrentData();

        auto& resp = current->winningResponse(spotNum);

        const AgentConfig* config
            = std::static_pointer_cast<const AgentConfig>(resp.agentConfig).get();
        std::string name = exchangeName();

        auto campaignInfo = config->getProviderData<CampaignInfo>(name);
        int creativeIndex = resp.agentCreativeIndex;

        auto& creative = config->creatives[creativeIndex];
        auto creativeInfo = creative.getProviderData<CreativeInfo>(name);

        BrightRoll::BidResponse::SeatBid* seatBid;
        bool foundSeat = false;
        for (int i = 0; i < response.seatbid_size(); ++i) {
            if (response.seatbid(i).seat() == campaignInfo->seat) {
                foundSeat = true;
                seatBid = response.mutable_seatbid(i);
                break;
            }
        }

        // Create seatBid if it does not exist
        if (!foundSeat) {
            auto sbid = response.add_seatbid();
            sbid->set_seat(campaignInfo->seat);

            seatBid = sbid;
        }

        BrightRollCreativeConfiguration::Context context {
            creative,
            resp,
            *auction.request,
            static_cast<int>(spotNum)
        };

        auto bid = seatBid->add_bid();
        bid->set_id(Id(auction.id, auction.request->imp[spotNum].id).toString());

        double price = USD_CPM(resp.price.maxPrice);
        bid->set_price(price);
        bid->set_nurl(creativeConfig.expand(creativeInfo->nurl, context));
        bid->add_adomain(creativeInfo->adomain);
        bid->set_cid(resp.agent);
        bid->set_crid(std::to_string(resp.creativeId));

        setBidExtension(bid->mutable_ext(), creativeInfo);

    }

    void
    BrightRollExchangeConnector::setBidExtension(
            BrightRoll::BidResponse::BidExt *ext,
            const CreativeInfo* info) const
    {
        ExcAssert(ext);

        ext->set_campaign_name(info->campaign_name);
        ext->set_line_item_name(info->line_item_name);
        ext->set_creative_name(info->creative_name);
        ext->set_creative_duration(info->creative_duration);

        auto mediaDesc = ext->add_media_desc();
        mediaDesc->set_media_mime(BrightRoll::toMimes(info->media_desc.media_mime));
        mediaDesc->set_media_bitrate(info->media_desc.media_bitrate);

        ext->set_api(BrightRoll::brightroll_cast<Api>(info->api));
        ext->set_lid(info->lid);
        ext->set_landingpage_url(info->landingpage_url);
        ext->set_advertiser_name(info->advertiser_name);
        ext->add_companiontype(
                BrightRoll::brightroll_cast<Companiontype>(info->companiontype));
        ext->set_adtype(info->adtype);
        ext->set_adserver_processing_time(0);
    }


} // namespace JamLoop
