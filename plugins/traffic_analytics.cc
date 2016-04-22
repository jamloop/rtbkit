/* analytics_endpoint.cc
   Mathieu Stefani, 20 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the analytics endpoint
*/


#include "traffic_analytics.h"

#include "soa/service/service_utils.h"
#include "soa/service/async_event_source.h"
#include <sys/timerfd.h>

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

using namespace Datacratic;
using namespace std;

namespace Jamloop {

struct OneShotTimerEventSource : public AsyncEventSource {
    OneShotTimerEventSource()
        : timerFd(-1)
        , timePeriodSeconds(0.0)
    { }

    OneShotTimerEventSource(int timePeriodSeconds,
            std::function<void (uint64_t)> onTimeout)
        : timerFd(-1)
        , timePeriodSeconds(timePeriodSeconds)
        , onTimeout(onTimeout)
    {
        init(timePeriodSeconds, onTimeout);
    }

    ~OneShotTimerEventSource()
    {
        if (timerFd != -1)
            close(timerFd);
    }


    void init(int timePeriodSeconds, std::function<void (uint64_t)> onTimeout) {
        this->timePeriodSeconds = timePeriodSeconds;
        this->onTimeout = onTimeout;

        timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timerFd == -1)
            throw ML::Exception(errno, "timerfd_create");

        itimerspec spec;
        int res = clock_gettime(CLOCK_MONOTONIC, &spec.it_value);
        if (res == -1)
            throw ML::Exception(errno, "clock_gettime");

        uint64_t seconds, nanoseconds;
        seconds = timePeriodSeconds;
        nanoseconds = (timePeriodSeconds - seconds) * 1000000000;

        spec.it_value.tv_sec = seconds;
        spec.it_value.tv_nsec = nanoseconds;

        spec.it_interval.tv_sec = spec.it_interval.tv_nsec = 0;

        res = timerfd_settime(timerFd, 0, &spec, 0);
        if (res == -1)
            throw ML::Exception(errno, "timerfd_settime");

    }

    int selectFd() const {
        return timerFd;
    }

    bool processOne() {
        uint64_t numWakeups = 0;
        for (;;) {
            int res = read(timerFd, &numWakeups, 8);
            if (res == -1 && errno == EINTR) continue;
            if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (res == -1)
                throw ML::Exception(errno, "timerfd read");
            else if (res != 8)
                throw ML::Exception("timerfd read: wrong number of bytes: %d",
                                    res);
            onTimeout(numWakeups);
            break;
        }
        return false;
    }


private:
    int timerFd;
    int timePeriodSeconds;
    std::function<void (uint64_t)> onTimeout;


};

TrafficAnalytics::TrafficAnalytics(
        GeoDatabase& db,
        std::string serviceName, std::shared_ptr<ServiceProxies> proxies)
    : ServiceBase(std::move(serviceName), std::move(proxies))
    , geoDb(db)
{ }

void
TrafficAnalytics::Result::dump(std::ostream& os, int top, bool dma = true) {
    for (auto& exchange: exchanges) {
        os << "Result for " << exchange.first << std::endl;
        os << "----------------------------------" << std::endl;
        auto pct = [](size_t val, size_t max) {
            return (val * 100) / max;
        };

        auto& entry = exchange.second;

        os << "Stats: " << std::endl;
        os << "Total matched requests: " << entry.total << std::endl;
        if (dma) {
            os << "DMA distribution:" << std::endl;

            const std::string indent(4, ' ');
            for (const auto& d: entry.dmaDistribution) {
                os << d.first << " -> " << d.second.total() << " ("  << pct(d.second.total(), entry.total) << "%)" << std::endl;
                os << indent << "IP      -> " << d.second.ipCount << " (" << pct(d.second.ipCount, d.second.total()) << "%)" << std::endl;
                os << indent << "lat/lon -> " << d.second.latLonCount << " (" << pct(d.second.latLonCount, d.second.total()) << "%)" << std::endl;
            }
        }

        os << "Subnet distribution:" << std::endl;

        std::sort(entry.subnetDistribution.begin(), entry.subnetDistribution.end(), [](const std::pair<Subnet, int>& lhs, const std::pair<Subnet, int>& rhs) {
                return lhs.second > rhs.second;
        });

        size_t total = top == -1 ? entry.subnetDistribution.size() : top;

        for (size_t i = 0; i < total; ++i) {
            const auto& s = entry.subnetDistribution[i]; 
            os << s.first.toString() << " -> " << s.second << " (" << pct(s.second, total) << "%)" << std::endl;
        }

        os << std::endl;
    }
}

void
TrafficAnalytics::Result::save(std::ostream& os) const {
    for (const auto& req: requests) {
        os << req << '\n';
    }
    os.flush();
}

void
TrafficAnalytics::Result::record(
        const GeoDatabase::Result& result, const std::string& exchangeName) {
    auto& entry = exchanges[exchangeName];
    entry.record(result);
}

void
TrafficAnalytics::Result::setRequests(std::vector<Json::Value> reqs) {
    requests = std::move(reqs);
}

void
TrafficAnalytics::Result::ExchangeEntry::record(const GeoDatabase::Result& result) {
    ++total;
    auto& entry = dmaDistribution[result.metroCode];
    
    if (result.matchType == GeoDatabase::MatchType::LatLon)
        ++entry.latLonCount;
    else {
        ++entry.ipCount;

        auto it = std::find_if(subnetDistribution.begin(), subnetDistribution.end(),
            [&](const std::pair<Subnet, int>& entry) {
                return entry.first == result.subnet;
        }); 

        if (it == std::end(subnetDistribution)) {
            auto newEntry = std::make_pair(result.subnet, 1);
            subnetDistribution.push_back(newEntry);
        } else {
            auto& val = it->second;
            ++val;
        }
    }
}

void
TrafficAnalytics::run(std::chrono::seconds duration, TrafficAnalytics::OnFinish onFinish)
{
    this->onFinish = onFinish;
    collector.onDone = [=](std::vector<Json::Value>&& requests) {
        doStats(std::move(requests));
    };

    collector.init(getServices(), duration);
    collector.start();
}

void
TrafficAnalytics::doStats(std::vector<Json::Value>&& requests) {

    Result results;

    for (const auto& request: requests) {
        if (request.isMember("device")) {
            auto device = request["device"];
            if (device.isMember("ip")) {
                auto ip = device["ip"].asString();
                GeoDatabase::Context context {
                    ip,
                    std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN()
                }; 

                bool found;
                GeoDatabase::Result result;

                std::tie(found, result) = geoDb.lookup(context);
                if (found) {
                    auto exchange = request["exchange"].asString();
                    results.record(result, exchange);
                } 
            }
        }
    }

    results.setRequests(std::move(requests));

    onFinish(std::move(results));
}

void
TrafficAnalytics::Collector::init(
        const std::shared_ptr<ServiceProxies>& proxies,
        std::chrono::seconds dur) {

    requests.reserve(1e6);
    shutdown = false;
    subscriber = std::make_shared<ZmqNamedMultipleSubscriber>(proxies->zmqContext);
    subscriber->init(proxies->config);
    subscriber->messageHandler = [&](vector<zmq::message_t>&& message) {
        if (!shutdown)
            processMessage(std::move(message));
    };

    subscriber->connectAllServiceProviders("rtbRequestRouter", "logger", { "AUCTION" });

    addSource("Collector::subscriber", subscriber);
    addSource("Collector::timer",
            std::make_shared<OneShotTimerEventSource>(dur.count(), [&](uint64_t) { onTimer(); } ));
}

void
TrafficAnalytics::Collector::onTimer() {
    shutdown = true;
    onDone(std::move(requests));
}

void
TrafficAnalytics::Collector::processMessage(std::vector<zmq::message_t>&& message) {
    auto reqStr = message[3].toString();
    requests.push_back(Json::parse(reqStr));
}

}

int main(int argc, char* argv[]) {
    ServiceProxyArguments serviceArgs;

    using namespace boost::program_options;

    int sampleDurationSeconds;

    std::string geoIpFile;
    std::string geoLocFile;

    int top;
    bool dma;

    auto opts = serviceArgs.makeProgramOptions();
    opts.add_options()
        ("duration", value<int>(&sampleDurationSeconds),
         "Duration of the sample in seconds")
        ("geo-ip-file", value<string>(&geoIpFile),
         "Location of the Geo Ipv4 file")
        ("geo-location-file", value<string>(&geoLocFile),
         "Location of the Geo locations file")
        ("top", value<int>(&top)->default_value(-1),
         "Only print the top subnets (-1 for all)")
        ("dma", bool_switch(&dma)->default_value(false),
         "Print DMAs")
        ("help,h", "Print this message");

    variables_map vm;
    store(command_line_parser(argc, argv).options(opts).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << opts << endl;
        return 1;
    }

    auto proxies = serviceArgs.makeServiceProxies();
    auto serviceName = serviceArgs.serviceName("traffic-analytics");

    Jamloop::GeoDatabase db(serviceName, proxies);
    db.load(geoIpFile, geoLocFile, Jamloop::GeoDatabase::Precision(0.1));

    std::atomic<bool> over(false);

    Jamloop::TrafficAnalytics analytics(db, serviceName, proxies);
    analytics.run(std::chrono::seconds(sampleDurationSeconds), [&](Jamloop::TrafficAnalytics::Result&& result) {

        const auto now = std::time(NULL);
        char s[100];
        std::strftime(s, sizeof(s), "m_d_Y-HMS", std::localtime(&now));
        const std::string outFile = std::string("traffic.dump.") + s;

        result.dump(std::cout, top, dma);

        std::ofstream out(outFile);
        result.save(out);
        over = true;
    });

    while (!over) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

}
