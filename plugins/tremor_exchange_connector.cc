/* tremor_exchange_connector.cc
   Mathieu Stefani, 02 June 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Tremor Exchange Connector
*/

#include "tremor_exchange_connector.h"
#include "soa/utils/generic_utils.h"

using namespace RTBKIT;
using namespace Datacratic;

namespace Jamloop {

    namespace Default {
        // Tremor Exchange currently conducts 200 millisecond auctions

        static constexpr double MaximumResponseTime = 200;
    }

    TremorExchangeConnector::TremorExchangeConnector(ServiceBase& owner, std::string name)
        : OpenRTBExchangeConnector(owner, std::move(name))
        , creativeConfig(exchangeName())
    {
        this->auctionResource = "/auctions";
        this->auctionVerb = "POST";
        initCreativeConfiguration();
    }

    TremorExchangeConnector::TremorExchangeConnector(
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
    TremorExchangeConnector::initCreativeConfiguration()
    {

        creativeConfig.addField(
            "adm",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adm);

                return true;
        }).snippet().optional();

        creativeConfig.addField(
            "nurl",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.nurl);

                return true;
        }).snippet().optional();

        creativeConfig.addField(
            "iurl",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.iurl);

                return true;
        }).optional();

        creativeConfig.addField(
            "adomain",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adomain);

                if (info.adomain.empty()) {
                    throw std::invalid_argument("adm is required");
                }

                return true;
        }).required();

        creativeConfig.addField(
            "attr",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.attr);

                return true;
        }).optional();

    }

    double
    TremorExchangeConnector::getTimeAvailableMs(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {

        return Default::MaximumResponseTime;
    }

    ExchangeConnector::ExchangeCompatibility
    TremorExchangeConnector::getCampaignCompatibility(
            const AgentConfig& config,
            bool includeReasons) const
    {

        ExchangeCompatibility result;
        result.setCompatible();

        std::string exchange = exchangeName();
        const char* name = exchange.c_str();
        auto info = std::make_shared<CampaignInfo>();

        if (config.providerConfig.isMember(exchange)) {
            auto provConf = config.providerConfig[exchange];

            if (provConf.isMember("seat")) {
                info->seat = Id(provConf["seat"].asString());
            }

        }

        result.info = std::move(info);
        return result;

    }

    ExchangeConnector::ExchangeCompatibility
    TremorExchangeConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const {
        return creativeConfig.handleCreativeCompatibility(creative, includeReasons);
    }

    std::shared_ptr<BidRequest>
    TremorExchangeConnector::parseBidRequest(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        return OpenRTBExchangeConnector::parseBidRequest(handler, header, payload);
    }

    void
    TremorExchangeConnector::setSeatBid(
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
        int seatIndex = indexOf(response.seatbid, &OpenRTB::SeatBid::seat, Id(campaignInfo->seat));

        OpenRTB::SeatBid* seatBid;

        // Create the seat if it does not exist
        if (seatIndex == -1) {
            OpenRTB::SeatBid sbid;
            sbid.seat = Id(campaignInfo->seat);

            response.seatbid.push_back(std::move(sbid));

            seatBid = &response.seatbid.back();
        }
        else {
            seatBid = &response.seatbid[seatIndex];
        }

        ExcAssert(seatBid);
        seatBid->bid.emplace_back();
        auto& bid = seatBid->bid.back();

        TremorCreativeConfiguration::Context context {
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

        bid.adomain = creativeInfo->adomain;
        if (!creativeInfo->adm.empty()) bid.adm = creativeConfig.expand(creativeInfo->adm, context);
        if (!creativeInfo->nurl.empty()) bid.nurl = creativeConfig.expand(creativeInfo->nurl, context);

        if (!creativeInfo->attr.empty()) bid.attr = creativeInfo->attr;
    }

} // namespace Jamloop

namespace {

struct Init {
    Init() {
        RTBKIT::ExchangeConnector::registerFactory<Jamloop::TremorExchangeConnector>();
    }
} init;

}

