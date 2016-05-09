/* basic_bidding_agent.h
   Eric Robert, 7 May 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
*/

#pragma once

#include "soa/service/logs.h"
#include "rtbkit/plugins/bidding_agent/bidding_agent.h"
#include "rtbkit/core/banker/slave_banker.h"

namespace JamLoop {

using RTBKIT::AgentConfig;
using RTBKIT::Amount;
using RTBKIT::BiddingAgent;
using RTBKIT::BidRequest;
using RTBKIT::Bids;
using RTBKIT::BudgetController;
using RTBKIT::WinCostModel;
using Datacratic::Id;
using Datacratic::Logging;
using Datacratic::ServiceProxies;

/*
 * Basic bidding agent reads its configuration from a JSON file and starts
 * bidding at fixed price.
 *
 */

struct BasicBiddingAgent : public BiddingAgent
{
    BasicBiddingAgent(std::shared_ptr<ServiceProxies> proxies,
                      std::string name,
                      std::string const & filename,
                      std::shared_ptr<BudgetController> banker);

    void start();
    void shutdown();
    void report();

private:
    void bid(double timestamp,
             Id id,
             std::shared_ptr<BidRequest> request,
             Bids const & bids,
             double timeLeft,
             Json::Value augmentations,
             WinCostModel const & wcm);

    void readConfig(std::string const & filename);
    void sendConfig();
    void pacing();

    AgentConfig config;

    Amount budget;
    Amount pace;
    Amount price;
    double priority;

    // For pacing mechanism
    int pacing_type;
    Amount total_amount_spent_on_wins_since_last_topup;
    void winOrientedPacing();

    // connection to the banker for pacing and budget
    std::shared_ptr<BudgetController> banker;
    bool ready;

    static Logging::Category print;
    static Logging::Category error;
    static Logging::Category trace;
};

}
