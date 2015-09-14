#include "viewability_service.h"
#include "redis_viewability_backend.h"
#include "soa/service/process_stats.h"
#include "soa/service/service_utils.h"
#include "jml/arch/format.h"

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <thread>
#include <chrono>

using namespace Datacratic;
using namespace JamLoop;

using namespace std;

int main(int argc, char *argv[]) {
    ServiceProxyArguments serviceArgs;
    ViewabilityService::Config config;

    using namespace boost::program_options;

    options_description allOptions;
    allOptions
        .add(config.makeProgramOptions())
        .add(serviceArgs.makeProgramOptions());

    allOptions.add_options() ("help,h", "Print this message");

    variables_map vm;
    store(command_line_parser(argc, argv).options(allOptions).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << allOptions << endl;
        return 1;
    }

#if 0
    const auto redisAddr = Redis::Address::tcp(config.redisHost, config.redisPort);
    auto redisBackend = std::make_shared<RedisViewabilityBackend>(redisAddr);
#endif

    auto proxies = serviceArgs.makeServiceProxies();
    auto serviceName = serviceArgs.serviceName("viewabilityService");

    ViewabilityService service(proxies, serviceName);
    //service.setBackend(redisBackend);
    service.setConfig(config);

    service.init();
    service.bindTcp(PortRange(), PortRange());

    service.start();

    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

}
