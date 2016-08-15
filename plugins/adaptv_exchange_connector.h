/* adaptv_exchange_connector.h
   Mathieu Stefani, 28 octobre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   The Adap.tv Exchange Connector
*/

#pragma once

#include "rtbkit/plugins/exchange/openrtb_exchange_connector.h"
#include "rtbkit/common/creative_configuration.h"
#include "soa/service/logs.h"

namespace JamLoop {

class AdaptvExchangeConnector : public RTBKIT::OpenRTBExchangeConnector {
public:

    AdaptvExchangeConnector(Datacratic::ServiceBase& owner, std::string name);
    AdaptvExchangeConnector(std::string name,
                    std::shared_ptr<Datacratic::ServiceProxies> proxies);

    static std::string exchangeNameString() {
        return "adaptv";
    }

    std::string exchangeName() const {
        return exchangeNameString();
    }

    void configure(const Json::Value& config);

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

    double
    getTimeAvailableMs(RTBKIT::HttpAuctionHandler& handler,
            const Datacratic::HttpHeader& header,
            const std::string& payload);

    struct CreativeInfo {
        std::string adid; //< ID that references the ad to be served if the bid wins
        std::string nurl; //< Win notice URL called if the bid wins
        std::string adm; //< VAST XML ad markup for the Video Object
    };

    typedef RTBKIT::CreativeConfiguration<CreativeInfo> AdaptvCreativeConfiguration;

private:
    struct Logs {
        static Datacratic::Logging::Category print;
        static Datacratic::Logging::Category trace;
        static Datacratic::Logging::Category error;
    };

    void initCreativeConfiguration();
    void setSeatBid(const RTBKIT::Auction& auction,
                    int spotNum,
                    OpenRTB::BidResponse& response) const;

    AdaptvCreativeConfiguration creativeConfig;

    std::string inventoryType;

};

} // namespace JamLoop
