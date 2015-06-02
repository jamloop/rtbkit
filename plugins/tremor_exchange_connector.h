/* tremor_exchange_connector.h
   Mathieu Stefani, 02 June 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Exchange Connector for Tremor
*/

#pragma once

#include "rtbkit/plugins/exchange/openrtb_exchange_connector.h"
#include "rtbkit/common/creative_configuration.h"

namespace Jamloop {

class TremorExchangeConnector : public RTBKIT::OpenRTBExchangeConnector {
public:

    TremorExchangeConnector(Datacratic::ServiceBase& owner, std::string name);
    TremorExchangeConnector(std::string name,
                            std::shared_ptr<Datacratic::ServiceProxies> proxies);

    static std::string exchangeNameString() {
        return "tremor";
    }

    std::string exchangeName() const {
        return exchangeNameString();
    }

    struct CampaignInfo {
        Datacratic::Id seat;
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

    double getTimeAvailableMs(RTBKIT::HttpAuctionHandler& handler,
                              const Datacratic::HttpHeader& header,
                              const std::string& payload);

    struct CreativeInfo {
        std::string adm; //< Actual ad markup
        std::string nurl; //< Win notice URL
        std::string iurl; //< Sample image URL (without cache busting) for content checking

        std::vector<std::string> adomain; //< Advertiser's primary or top-level domain for advertiser checking
        Datacratic::List<OpenRTB::CreativeAttribute> attr; //< Array of creative attributes
    };

    typedef RTBKIT::CreativeConfiguration<CreativeInfo> TremorCreativeConfiguration;

private:
    void initCreativeConfiguration();
    void setSeatBid(const RTBKIT::Auction& auction,
                    int spotNum,
                    OpenRTB::BidResponse& resopnse) const;

    TremorCreativeConfiguration creativeConfig;


};

} // namespace Jamloop
