#include "forensiq_augmentor.h"
#include "soa/service/service_utils.h"
#include "rtbkit/common/args.h"

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
    RTBKIT::ProxyArguments serviceArgs;

    using namespace boost::program_options;

    std::string apiKey;
    int threads;

    auto options = serviceArgs.makeProgramOptions();
    options.add_options()
        ("api-key", value<string>(&apiKey),
         "The forensiq API key")
        ("threads", value<int>(&threads)->default_value(2),
         "Number of threads for the augmentor")
        ("help,h", "Print this message");

    variables_map vm;
    store(command_line_parser(argc, argv).options(options).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << options << endl;
        return 1;
    }

    auto serviceName = serviceArgs.serviceName("forensiq");
    auto proxies = serviceArgs.makeServiceProxies(serviceName);

    ForensiqAugmentor augmentor(proxies, serviceName);
    augmentor.init(threads, apiKey);
    augmentor.start();

    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}
