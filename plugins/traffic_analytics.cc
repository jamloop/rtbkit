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

namespace JamLoop {

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
        std::string serviceName, std::shared_ptr<ServiceProxies> proxies)
    : ServiceBase(std::move(serviceName), std::move(proxies))
{ }

void
TrafficAnalytics::Result::dump(std::ostream& os) const {
}

void
TrafficAnalytics::Result::save(std::ostream& os) const {
}

void
TrafficAnalytics::run(std::chrono::seconds duration, TrafficAnalytics::OnFinish onFinish)
{
    collector.init(getServices(), duration);
    collector.start();
}

void
TrafficAnalytics::Collector::init(
        const std::shared_ptr<ServiceProxies>& proxies,
        std::chrono::seconds dur) {

    subscriber = std::make_shared<ZmqNamedMultipleSubscriber>(proxies->zmqContext);
    subscriber->init(proxies->config);
    subscriber->messageHandler = [&](vector<zmq::message_t>&& message) {
        processMessage(std::move(message));
    };

    subscriber->connectAllServiceProviders("rtbRequestRouter", "logger", { "AUCTION" });

    addSource("Collector::subscriber", subscriber);
    addSource("Collector::timer",
            std::make_shared<OneShotTimerEventSource>(dur.count(), [&](uint64_t) { onTimer(); } ));
}

void
TrafficAnalytics::Collector::onTimer() {
    std::cout << "Timer fired" << std::endl;
}

void
TrafficAnalytics::Collector::processMessage(std::vector<zmq::message_t>&& message) {
    std::cout << "Received BidRequest" << std::endl;
}

}

int main(int argc, char* argv[]) {
    ServiceProxyArguments serviceArgs;

    using namespace boost::program_options;

    int sampleDurationSeconds;

    std::string outFile;

    auto opts = serviceArgs.makeProgramOptions();
    opts.add_options()
        ("duration", value<int>(&sampleDurationSeconds),
         "Duration of the sample in seconds")
        ("out", value<std::string>(&outFile),
         "Name of the output file")
        ("help,h", "Print this message");

    variables_map vm;
    store(command_line_parser(argc, argv).options(opts).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << opts << endl;
        return 1;
    }

    auto proxies = serviceArgs.makeServiceProxies();
    auto serviceName = serviceArgs.serviceName("forensiq");

    JamLoop::TrafficAnalytics analytics(serviceName, proxies);
    analytics.run(std::chrono::seconds(sampleDurationSeconds), [&](const JamLoop::TrafficAnalytics::Result& result) {
        result.dump(std::cout);

        std::ofstream os(outFile);
        result.save(os);
    });

    std::this_thread::sleep_for(std::chrono::seconds(12));

}
