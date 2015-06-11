/* brightroll_exchange_connector.h
   Mathieu Stefani, 08 June 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   The BrightRoll Exchange Connector
*/

#pragma once

#include "rtbkit/plugins/exchange/http_exchange_connector.h" 
#include "rtbkit/common/creative_configuration.h"
#include "rtbkit/openrtb/openrtb.h"
#include "brightroll-openrtb.pb.h"

namespace JamLoop {

class BrightRollExchangeConnector : public RTBKIT::HttpExchangeConnector {
public:

    BrightRollExchangeConnector(Datacratic::ServiceBase& owner, std::string name);
    BrightRollExchangeConnector(std::string name,
                                std::shared_ptr<Datacratic::ServiceProxies> proxies);

    static std::string exchangeNameString() {
        return "brightroll";
    }

    std::string exchangeName() const {
        return exchangeNameString();
    }

    struct CampaignInfo {
        //< ID provided by the bidder representing the buying entity.
        //  This value can either be numeric or alphanumeric.
        std::string seat;
    };

    RTBKIT::ExchangeConnector::ExchangeCompatibility
    getCampaignCompatibility(
            const RTBKIT::AgentConfig& config,
            bool includeReasons) const;

    RTBKIT::ExchangeConnector::ExchangeCompatibility
    getCreativeCompatibility(
            const RTBKIT::Creative& creative,
            bool includeReasons) const;

    std::shared_ptr<RTBKIT::BidRequest>
    parseBidRequest(RTBKIT::HttpAuctionHandler& handler,
                    const Datacratic::HttpHeader& header,
                    const std::string& payload);

    double getTimeAvailableMs(Datacratic::HttpAuctionHandler& handler,
                              const Datacratic::HttpHeader& header,
                              const std::string& payload);

    Datacratic::HttpResponse getResponse(
                             const Datacratic::HttpAuctionHandler& connection,
                             const Datacratic::HttpHeader& header,
                             const RTBKIT::Auction& auction) const;

    struct CreativeInfo {

        struct MediaDesc {
            //< Mime type of the media file associated with the
            //  returned creative.
            std::string media_mime;

            //< If the media file is a video, provide the associated
            //  bitrate.
            int media_bitrate;
        };

        //< The VAST tag to serve if the bid wins the BrightRoll
        //  auction. A random number or cache busting string
        //  should be added/expanded before submitting in the bid
        //  response. # #BRX_CLEARING_PRICE## should be
        //  included in the URL to pass the winning price ratio.
        std::string nurl;

        //< Advertiser’s primary or top-level domain(s) for advertiser
        //  checking. The adomain field should not include the
        //  “http://” protocol in the response
        std::string adomain;

        //< Friendly campaign name.
        std::string campaign_name;

        //< Friendly line item name.
        std::string line_item_name;

        //< Friendly creative name.
        std::string creative_name;

        //< Duration of the creative returned in seconds.
        int creative_duration;

        //< Object describing the media file/returned in the VAST
        //  associated with the nurl . Multiple media_desc fields
        //  may be returned if multiple media files are included in
        //  the VAST document. BrightRoll will select the first valid
        //  media file in the array to serve.
        MediaDesc media_desc;

        //< API framework required by the returned creative (e.g., VPAID).
        OpenRTB::ApiFramework api;

        //< Line item ID of the returned creative.
        std::string lid;

        //< Landing page URL for the campaign.
        std::string landingpage_url;

        //< Advertiser name.
        std::string advertiser_name;

        //< Companion types in the returned creative. Required only if a
        //  companion is included in response
        OpenRTB::VastCompanionType companiontype;

        //< Defines if the bid is for an impression opportunity defined by a
        //  video or banner object. NOTE: BrightRoll only supports bids for
        //  video objects
        AdType adtype;
    };

    typedef RTBKIT::CreativeConfiguration<CreativeInfo> BrightRollCreativeConfiguration;

private:
    BrightRollCreativeConfiguration creativeConfig;

    void initCreativeConfiguration();

};

} // namespace JamLoop 
