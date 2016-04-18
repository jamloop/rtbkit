/* geo_pipeline.h
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   A Geo location Pipeline that will map IP and lat/lon informations to DMA region code
*/

#pragma once

#include "rtbkit/common/bid_request_pipeline.h"
#include <cmath>
#include <atomic>

namespace Jamloop {

typedef uint32_t InAddr;
bool toAddr(const char* str, InAddr* out);

struct GeoDatabase {


    struct Context {
        std::string ip;
        double latitude;
        double longitude;

        bool hasValidGeo() const {
            return !std::isnan(latitude) && !std::isnan(longitude);
        }
    };

    struct Precision {
        explicit Precision(double s = 1.0)
            : s(s)
        {
        }

        template<typename T>
        T scale(double value) {
            return static_cast<T>(value / s);
        }

    private:
        double s;
    };

    struct Result {
        std::string metroCode;
        std::string zipCode;
        std::string countryCode;
        std::string region;
    };

    GeoDatabase(
            const std::string& prefix,
            std::shared_ptr<Datacratic::ServiceProxies> proxies);
    ~GeoDatabase();

    void loadAsync(
            const std::string& ipFile, const std::string& locationFile,
            Precision precision);

    void load(
            const std::string& ipFile, const std::string& locationFile,
            Precision precision);

    std::pair<bool, Result> lookup(const Context &context);

    bool isLoaded() const;

private:

    struct Entry {
        std::string metroCode;
        std::string zipCode;
        std::string countryCode;
        std::string region;

        static Entry fromIp(InAddr ip);
        static Entry fromGeo(double latitude, double longitude);

        InAddr ip() const;
        std::pair<double, double> geo() const;

        bool isLocated(double latitude, double longitude) const;

        Result toResult() const;

        union {
            struct {
                InAddr ip;
            };
            struct {
                double latitude;
                double longitude;
            };
        } u;

    };

    uint64_t makeGeoHash(double latitude, double longitude, Precision precision);
    uint64_t makeGeoHash(const Entry& entry, Precision precision);
    uint64_t makeGeoHash(const Context& context, Precision precision);

    struct Data {
        std::vector<Entry> subnets;
        std::unordered_map<uint64_t, std::vector<Entry>> geoloc;
        Precision precision;
    };

    std::atomic<Data *> dataGuard;
    std::unique_ptr<Datacratic::EventRecorder> events;
};

class GeoPipeline : public RTBKIT::BidRequestPipeline {
public:
    GeoPipeline(
            const std::shared_ptr<Datacratic::ServiceProxies>& proxies,
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


