/* publisher_connector.h
   Mathieu Stefani, 10 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Special exchange connector to directly connect to a publisher
   through an ad tag
*/

#pragma once

#include "rtbkit/plugins/exchange/http_exchange_connector.h"
#include "soa/service/message_loop.h"
#include "soa/service/http_client.h"
#include "soa/service/typed_message_channel.h"
#include <memory>

namespace JamLoop {

class PublisherConnector : public RTBKIT::HttpExchangeConnector {
public:
    PublisherConnector(Datacratic::ServiceBase& owner, std::string name);
    PublisherConnector(std::string name,
                std::shared_ptr<Datacratic::ServiceProxies> proxies);

    static std::string exchangeNameString() {
        return "publisher";
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

    double
    getTimeAvailableMs(Datacratic::HttpAuctionHandler& handler,
            const Datacratic::HttpHeader& header,
            const std::string& payload);

    Datacratic::HttpResponse
    getResponse(const Datacratic::HttpAuctionHandler& connection,
            const Datacratic::HttpHeader& header,
            const RTBKIT::Auction& auction) const;

    void configure(const Json::Value& parameters);

private:
    /* We should not generate a WIN ourself but instead
     * let the adserver generate and send the win to the
     * adserver connector, but keep the class until then
     */
    class WinSource : public Datacratic::MessageLoop {
    public:
        WinSource(const std::string& adserverHost);

        typedef std::function<void ()> OnWinSent;
        void sendAsync(const RTBKIT::Auction& auction, OnWinSent onSent = nullptr);

        Datacratic::HttpResponse sendSync(const RTBKIT::Auction& auction);

    private:
        struct WinMessage {
            WinMessage() { }

            WinMessage(
                    RTBKIT::Auction::Response response,
                    std::shared_ptr<RTBKIT::BidRequest> request,
                    OnWinSent onSent)
                : response(std::move(response))
                , request(std::move(request))
                , onSent(std::move(onSent))
            { }

            RTBKIT::Auction::Response response;
            std::shared_ptr<RTBKIT::BidRequest> request;
            OnWinSent onSent;
        };

        void doWin(WinMessage&& message);

        Datacratic::TypedMessageSink<WinMessage> queue;
        std::shared_ptr<Datacratic::HttpClient> client;
    };

    std::string genericVast;
    double maxAuctionTime;
    std::unique_ptr<WinSource> wins;
};

} // namespace JamLoop
