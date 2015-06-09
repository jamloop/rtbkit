/* brightroll_exchange_connector.h
   Mathieu Stefani, 08 June 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   The BrightRoll Exchange Connector
*/

#pragma once

#include "rtbkit/plugins/exchange/http_exchange_connector.h" 
#include "rtbkit/common/creative_configuration.h"

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
private:

};

} // namespace JamLoop 
