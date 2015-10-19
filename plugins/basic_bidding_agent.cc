/* basic_bidding_agent.cc
   Eric Robert, 7 May 2015
   Copyright (c) 2015 Datacratic. All rights reserved.
*/

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <thread>
#include <chrono>

#include "basic_bidding_agent.h"

#include "jml/utils/file_functions.h"
#include "soa/service/service_utils.h"
#include "rtbkit/common/currency.h"

using namespace RTBKIT;
using namespace Datacratic;

Json::Value loadJsonFile(std::string const & filename) {
    ML::File_Read_Buffer buf(filename);
    return Json::parse(std::string(buf.start(), buf.end()));
}

namespace JamLoop {

BasicBiddingAgent::BasicBiddingAgent(std::shared_ptr<ServiceProxies> proxies,
                                     std::string name,
                                     std::string const & filename,
                                     std::shared_ptr<BudgetController> banker) :
    BiddingAgent(proxies, name),
    banker(banker),
    ready(false) {

    // put some default values before reading the configuration
    budget = USD(100);
    pace = USD(1);
    price = USD_CPM(1);
    priority = 1.0;
    readConfig(filename);

    // get rid of warnings for missing callbacks
    strictMode(false);

    // setup bidding
    onBidRequest = bind(&BasicBiddingAgent::bid, this, _1, _2, _3, _4, _5, _6, _7);

    // setup pacing
    if (!pace.isZero()) {
        addPeriodic("BasicBiddingAgent::pace", 60.0, [&](uint64_t) {
            pacing();
        });
    }

    BiddingAgent::init();
}

void BasicBiddingAgent::start() {
    BiddingAgent::start();
    sendConfig();
}

void BasicBiddingAgent::shutdown() {
    BiddingAgent::shutdown();
}

void BasicBiddingAgent::report() {
}

void BasicBiddingAgent::bid(double timestamp,
                            Id id,
                            std::shared_ptr<BidRequest> request,
                            Bids const & bids,
                            double timeLeft,
                            Json::Value augmentations,
                            WinCostModel const & wcm) {
    Bids items(bids);

    // look at all the potentials
    for (auto & bid : items) {
        // select a creative at random
        int creative = bid.availableCreatives[random() % bid.availableCreatives.size()];

        // fixed-price bid
        bid.bid(creative, price, priority);
    }

    doBid(id, items);
}

void BasicBiddingAgent::readConfig(std::string const & filename) {
    // create from file
    LOG(trace) << "Loading bidder configuration from '" << filename << "'" << std::endl;
    config = AgentConfig::createFromJson(loadJsonFile(filename));

    // extract parameters from extension
    auto & ext = config.ext;
    Json::Value item;

    item = ext.get("budget", Json::Value());
    if(!item.isNull()) {
        budget = Amount::parse(item.asString());
    }

    item = ext.get("pace", Json::Value());
    if(!item.isNull()) {
        pace = Amount::parse(item.asString());
    }

    item = ext.get("price", Json::Value());
    if(!item.isNull()) {
        price = Amount::parse(item.asString());
    }

    item = ext.get("priority", Json::Value());
    if(!item.isNull()) {
        priority = item.asDouble();
    }
}

void BasicBiddingAgent::sendConfig() {
    doConfig(config);
}

void BasicBiddingAgent::pacing() {
    if(!ready) {
        // create the account
        banker->addAccountSync(config.account);

        // set budget
        LOG(trace) << "Setting budget for campaign '" << config.account[0] << "' to " << budget << std::endl;
        banker->setBudgetSync(config.account[0], budget);

        ready = true;
    }

    // transfer a bit of money to bidder's account
    LOG(trace) << "Transfering " << pace << std::endl;
    banker->topupTransferSync(config.account, pace);
}

Logging::Category BasicBiddingAgent::print("BasicBiddingAgent");
Logging::Category BasicBiddingAgent::error("BasicBiddingAgent Error", BasicBiddingAgent::print);
Logging::Category BasicBiddingAgent::trace("BasicBiddingAgent Trace", BasicBiddingAgent::print);

}

int main(int argc, char** argv) {
    using namespace boost::program_options;

    ServiceProxyArguments globalArgs;
    SlaveBankerArguments bankerArgs;

    std::string filename;

    options_description options = globalArgs.makeProgramOptions();
    options.add_options()
        ("help,h", "Print this message")
        ("agent-configuration", value<std::string>(&filename), "Configuration filename");
    options.add(bankerArgs.makeProgramOptions());

    variables_map vm;
    store(command_line_parser(argc, argv).options(options).run(), vm);
    notify(vm);

    if(vm.count("help")) {
        std::cerr << options << std::endl;
        return 1;
    }

    if(filename.empty()) {
        std::cerr << "missing configuration file" << std::endl;
    }

    auto proxies = globalArgs.makeServiceProxies();

    // connect to the banker
    auto banker = std::make_shared<SlaveBudgetController>();
    banker->setApplicationLayer(bankerArgs.makeApplicationLayer(proxies));
    banker->start();

    // start the bidding agent
    const std::string name = globalArgs.serviceName("basic-bidder");
    JamLoop::BasicBiddingAgent agent(proxies, name, filename, banker);
    agent.start();

    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        agent.report();
    }

    return 0;
}
