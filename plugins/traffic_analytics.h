/* analytics.h
   Mathieu Stefani, 20 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   An endpoint that provides analytics information on the
   BidRequest stream
*/

#pragma once

#include <chrono>
#include "geo_pipeline.h"

#include "soa/service/service_base.h"
#include "soa/service/message_loop.h"
#include "soa/service/zmq_named_pub_sub.h"

namespace Jamloop {

class TrafficAnalytics : public Datacratic::ServiceBase {
public:

    TrafficAnalytics(
            GeoDatabase& db,
            std::string serviceName, std::shared_ptr<Datacratic::ServiceProxies> proxies);

    class Result {
    public:
        void dump(std::ostream& os) const;
        void save(std::ostream& os) const;

        void record(const GeoDatabase::Result& result);

    private:
        struct Entry {
            size_t latLonCount;
            size_t ipCount;

            size_t total() const {
                return latLonCount + ipCount;
            }
        };

        size_t total;

        std::map<std::string, Entry> dmaDistribution;
        std::vector<std::pair<Subnet, int>> subnetDistribution;
    };

    typedef std::function<void(std::unordered_map<std::string, Result>&&)> OnFinish;

    void run(std::chrono::seconds duration, OnFinish onFinish);

private:

    struct Collector : public Datacratic::MessageLoop {
    public:
        typedef std::function<void (std::vector<Json::Value>&&)> OnDone;

        OnDone onDone;

        void init(
                const std::shared_ptr<Datacratic::ServiceProxies>& proxies,
                std::chrono::seconds dur);

    private:
        void processMessage(std::vector<zmq::message_t>&& message);
        void onTimer();

        std::shared_ptr<Datacratic::ZmqNamedMultipleSubscriber> subscriber;
        std::vector<Json::Value> requests;
        bool shutdown;
    };

    Collector collector;
    OnFinish onFinish;

    void doStats(std::vector<Json::Value>&& requests);
    GeoDatabase& geoDb;

};

} // namespace JamLoop
