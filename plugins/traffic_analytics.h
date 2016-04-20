/* analytics.h
   Mathieu Stefani, 20 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   An endpoint that provides analytics information on the
   BidRequest stream
*/

#pragma once

#include <chrono>

#include "soa/service/service_base.h"
#include "soa/service/message_loop.h"
#include "soa/service/zmq_named_pub_sub.h"

namespace JamLoop {

class TrafficAnalytics : public Datacratic::ServiceBase {
public:

    TrafficAnalytics(
            std::string serviceName, std::shared_ptr<Datacratic::ServiceProxies> proxies);

    class Result {
    public:
        void dump(std::ostream& os) const;
        void save(std::ostream& os) const;
    };

    typedef std::function<void(const Result&)> OnFinish;

    void run(std::chrono::seconds duration, OnFinish onFinish);

private:

    struct Collector : public Datacratic::MessageLoop {
    public:
        void init(
                const std::shared_ptr<Datacratic::ServiceProxies>& proxies,
                std::chrono::seconds dur);

    private:
        void processMessage(std::vector<zmq::message_t>&& message);
        void onTimer();

        std::shared_ptr<Datacratic::ZmqNamedMultipleSubscriber> subscriber;
    };

    Collector collector;

};

} // namespace JamLoop
