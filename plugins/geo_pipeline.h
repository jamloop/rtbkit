/* geo_pipeline.h
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   A Geo location Pipeline that will map IP and lat/lon informations to DMA region code
*/

#pragma once

#include "rtbkit/common/bid_request_pipeline.h"

namespace Jamloop {

typedef uint32_t InAddr;
static constexpr auto InAddrNone = InAddr(-1);
InAddr toAddr(const char* str);

struct GeoDatabase {

    GeoDatabase();

    static const std::string NoMetro;

    void loadAsync(const std::string& ipFile, const std::string& locationFile);

    void load(const std::string& ipFile, const std::string& locationFile);
    std::string findMetro(const std::string &ip);

    bool isLoaded() const;

private:
    struct Entry {
        InAddr ip;
        std::string metroCode;
    };

    std::vector<Entry> entries;
    std::atomic<bool> entriesGuard;
};

class GeoPipeline : public RTBKIT::BidRequestPipeline {
public:
    GeoPipeline(
            std::shared_ptr<Datacratic::ServiceProxies> proxies,
            std::string serviceName, const Json::Value& config);

    RTBKIT::PipelineStatus preBidRequest(
            const RTBKIT::ExchangeConnector* exchange,
            const Datacratic::HttpHeader& header,
            const std::string& payload);

    RTBKIT::PipelineStatus postBidRequest(
            const RTBKIT::ExchangeConnector* exchange,
            const std::shared_ptr<RTBKIT::Auction>& auction);
private:
    std::unique_ptr<GeoDatabase> db;
};

} // namespace JamLoop


