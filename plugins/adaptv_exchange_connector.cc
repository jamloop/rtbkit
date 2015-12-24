/* adaptv_exchange_connector.cc
   Mathieu Stefani, 28 octobre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Adap.tv Exchange Connector
*/

#include "adaptv_exchange_connector.h"
#include "soa/utils/generic_utils.h"
#include "soa/utils/scope.h"

using namespace RTBKIT;
using namespace Datacratic;

namespace JamLoop {

    namespace Default {
        // Our platform waits for 150ms for a Bid Response;
        // responses received after this are ignored.

        static constexpr double MaximumResponseTime = 150;
    }

Logging::Category AdaptvExchangeConnector::Logs::print(
    "AdaptvExchangeConnector");
Logging::Category AdaptvExchangeConnector::Logs::trace(
    "AdaptvExchangeConnector Trace", AdaptvExchangeConnector::Logs::print);
Logging::Category AdaptvExchangeConnector::Logs::error(
    "AdaptvExchangeConnector Error", AdaptvExchangeConnector::Logs::print);

    AdaptvExchangeConnector::AdaptvExchangeConnector(
            ServiceBase& owner, std::string name)
        : OpenRTBExchangeConnector(owner, name)
        , creativeConfig(exchangeName())
    {
        this->auctionResource = "/auctions";
        this->auctionVerb = "POST";
        initCreativeConfiguration();
    }

    AdaptvExchangeConnector::AdaptvExchangeConnector(
            std::string name, std::shared_ptr<ServiceProxies> proxies)
        : OpenRTBExchangeConnector(std::move(name), std::move(proxies))
        , creativeConfig(exchangeName())
    {
        this->auctionResource = "/auctions";
        this->auctionVerb = "POST";
        initCreativeConfiguration();
    }

    void
    AdaptvExchangeConnector::initCreativeConfiguration()
    {
        creativeConfig.addField(
            "adid",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adid);

                return true;
        }).optional();

        creativeConfig.addField(
            "nurl",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.nurl);

                return true;
        }).snippet().optional();

        creativeConfig.addField(
            "adm",
            [](const Json::Value& value, CreativeInfo& info) {
                Datacratic::jsonDecode(value, info.adm);

                if (info.adm.empty())
                    throw std::invalid_argument("adm is required"); 

                return true;
        }).snippet().required();
    }


    ExchangeConnector::ExchangeCompatibility
    AdaptvExchangeConnector::getCampaignCompatibility(
            const AgentConfig& config, bool includeReasons) const {

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
    AdaptvExchangeConnector::getCreativeCompatibility(
            const Creative& creative,
            bool includeReasons) const {
        return creativeConfig.handleCreativeCompatibility(creative, includeReasons);
    }

    double
    AdaptvExchangeConnector::getTimeAvailableMs(
            HttpAuctionHandler &handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        static const std::string toFind = "\"tmax\":";
        std::string::size_type pos = payload.find(toFind);
        if (pos == std::string::npos)
            return Default::MaximumResponseTime;
        
        int tmax = atoi(payload.c_str() + pos + toFind.length());
        return tmax;
    }

    std::shared_ptr<BidRequest>
    AdaptvExchangeConnector::parseBidRequest(
            HttpAuctionHandler& handler,
            const HttpHeader& header,
            const std::string& payload)
    {
        std::shared_ptr<BidRequest> br;
        try {
            br = OpenRTBExchangeConnector::parseBidRequest(handler, header, payload);

            Scope_Failure(br.reset());

            if (br) {
                if (br->site && br->site->publisher && br->site->publisher->id.notNull())
                    goto end;

                if (br->app && br->app->publisher && br->app->publisher->id.notNull())
                    goto end;

                if (br->site) {
                    const auto& ext = br->site->ext;
                    if (ext.isMember("mpcid")) {
                        if (!br->site->publisher)
                            br->site->publisher.reset(new OpenRTB::Publisher);

                        br->site->publisher->id = Id(ext["mpcid"].asString());
                        goto end;
                    }
                }

                if (br->app) {
                    const auto& ext = br->app->ext;
                    if (ext.isMember("mpcid")) {
                        if (!br->app->publisher)
                            br->app->publisher.reset(new OpenRTB::Publisher);

                        br->app->publisher->id = Id(ext["mpcid"].asString());
                    }
                }

            }

        } catch (const ML::Exception& e) {
            LOG(Logs::error) << "Bid Request: " << payload << std::endl;
        }

        end:
            return br;
    }

    void
    AdaptvExchangeConnector::setSeatBid(
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

        AdaptvCreativeConfiguration::Context context {
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
        bid.adm = creativeConfig.expand(creativeInfo->adm, context);
        if (!creativeInfo->nurl.empty())
            bid.nurl = creativeConfig.expand(creativeInfo->nurl, context);
        if (!creativeInfo->adid.empty())
            bid.adid = Id(creativeInfo->adid);

    }


} // namespace JamLoop

namespace {

struct Init {
    Init() {
        RTBKIT::ExchangeConnector::registerFactory<JamLoop::AdaptvExchangeConnector>();
    }
} init;

}

