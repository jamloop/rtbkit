/* liverail_exchange_connector.cc
   Mathieu Stefani, 16 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the LiveRail Exchange Connector
*/

#include "liverail_exchange_connector.h"
#include "soa/utils/generic_utils.h"

using namespace RTBKIT;
using namespace Datacratic;

namespace Jamloop {

    LiveRailExchangeConnector::LiveRailExchangeConnector(ServiceBase& owner, std::string name)
        : OpenRTBExchangeConnector(owner, std::move(name))
        , creativeConfig(exchangeName())
    {
        this->auctionResource = "/auctions";
        this->auctionVerb = "POST";
        initCreativeConfiguration();
    }

    LiveRailExchangeConnector::LiveRailExchangeConnector(
            std::string name,
            std::shared_ptr<ServiceProxies> proxies)
        : OpenRTBExchangeConnector(std::move(name), std::move(proxies))
        , creativeConfig(exchangeName())
    {
        this->auctionResource = "/auctions";
        this->auctionVerb = "POST";
        initCreativeConfiguration();
    }

    void
    LiveRailExchangeConnector::initCreativeConfiguration()
    {
        creativeConfig.addField(
            "adm",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adm);

                return true;
        }).snippet().required();

        creativeConfig.addField(
            "adomain",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adomain);

                return true;
        }).required();

        creativeConfig.addField(
            "buyerid",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.buyerId);

                return true;
        }).optional();
    }

    ExchangeConnector::ExchangeCompatibility
    LiveRailExchangeConnector::getCampaignCompatibility(
            const AgentConfig& config,
            bool includeReasons) const
    {
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
        auto info = std::make_shared<CampaignInfo>();
        info->seat = Datacratic::Id(seat);
        result.info = std::move(info);

        return result;

    }

    ExchangeConnector::ExchangeCompatibility
    LiveRailExchangeConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const
    {
        return creativeConfig.handleCreativeCompatibility(creative, includeReasons);
    }

    std::shared_ptr<BidRequest>
    LiveRailExchangeConnector::parseBidRequest(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        return OpenRTBExchangeConnector::parseBidRequest(handler, header, payload);
    }

    void
    LiveRailExchangeConnector::setSeatBid(
            const Auction& auction,
            int spotNum,
            OpenRTB::BidResponse& response) const
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

        // Find the index in the seats array
        int seatIndex = indexOf(response.seatbid, &OpenRTB::SeatBid::seat, campaignInfo->seat);

        OpenRTB::SeatBid* seatBid;

        // Create the seat if it does not exist
        if (seatIndex == -1) {
            OpenRTB::SeatBid sbid;
            sbid.seat = campaignInfo->seat;

            response.seatbid.push_back(std::move(sbid));

            seatBid = &response.seatbid.back();
        }
        else {
            seatBid = &response.seatbid[seatIndex];
        }

        ExcAssert(seatBid);
        seatBid->bid.emplace_back();
        auto& bid = seatBid->bid.back();

        LiveRailCreativeConfiguration::Context context {
            creative,
            resp,
            *auction.request,
            spotNum
        };

        bid.cid = Id(resp.agent);
        bid.crid = Id(resp.creativeId);
        bid.impid = auction.request->imp[spotNum].id;
        bid.id = Id(auction.id, auction.request->imp[0].id);
        bid.price.val = USD_CPM(resp.price.maxPrice);
        response.cur = "USD"; // LiveRail currently supports USD only.

        bid.adomain = creativeInfo->adomain;
        bid.adm = creativeConfig.expand(creativeInfo->adm, context);

        if (creativeInfo->buyerId) bid.ext["buyerid"] = creativeInfo->buyerId.toString();
    }


} // namespace Jamloop
