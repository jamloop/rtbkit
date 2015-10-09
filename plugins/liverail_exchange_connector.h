/* liverail_exchange_connector.h
   Mathieu Stefani, 16 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Exchange Connector for LiveRail
*/

#pragma once

#include "rtbkit/plugins/exchange/openrtb_exchange_connector.h"
#include "rtbkit/common/creative_configuration.h"
#include "soa/service/logs.h"

namespace Jamloop {

class LiveRailExchangeConnector : public RTBKIT::OpenRTBExchangeConnector {
public:

    LiveRailExchangeConnector(Datacratic::ServiceBase& owner, std::string name);
    LiveRailExchangeConnector(std::string name,
                            std::shared_ptr<Datacratic::ServiceProxies> proxies);

    static std::string exchangeNameString() {
        return "liverail";
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

    struct CreativeInfo {
        std::string adm; //< Valid inline VAST

        std::vector<std::string> adomain; //< The advertiser landing page
        Datacratic::Id buyerId; //< LiveRail provided buyer id
    };

    typedef RTBKIT::CreativeConfiguration<CreativeInfo> LiveRailCreativeConfiguration;

private:

    static Datacratic::Logging::Category print;
    static Datacratic::Logging::Category trace;

    void initCreativeConfiguration();

    void setSeatBid(const RTBKIT::Auction& auction,
                    int spotNum,
                    OpenRTB::BidResponse& response) const;

    LiveRailCreativeConfiguration creativeConfig;
};

} // namespace Jamloop
