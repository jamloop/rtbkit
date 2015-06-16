/* brightroll_exchange_connector_test.cc
   Mathieu Stefani, 12 June 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Unit tests for the BrightRoll Exchange
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <fstream>
#include <boost/test/unit_test.hpp>
#include "brightroll_exchange_connector.h"

#include "rtbkit/common/testing/exchange_source.h"
#include "rtbkit/testing/bid_stack.h"
#include "soa/service/http_header.h"

namespace {

std::string readBidRequest() {
    std::ifstream ifs("brightroll-bidrequest.dat", std::ios::in | std::ios::binary);
    if (!ifs) {
        throw ML::Exception("Failed to open sample bid request file");
    }

    ifs.seekg(0, ifs.end);
    auto length = ifs.tellg();
    ifs.seekg(0, ifs.beg);

    std::unique_ptr<char[]> buffer(new char[length]);
    ifs.read(buffer.get(), length);

    return std::string(buffer.get(), length);

}

}

using namespace RTBKIT;
using namespace JamLoop;

BOOST_AUTO_TEST_CASE( test_brightroll )
{
    BidStack stack;
    auto proxies = stack.proxies;

    Json::Value routerConfig;
    routerConfig[0]["exchangeType"] = "brightroll";

    Json::Value bidderConfig;
    bidderConfig["type"] = "agents";

    AgentConfig config;
    config.bidProbability = 1;
    config.account = { "campaign", "strategy" };
    auto &provConfig = config.providerConfig["brightroll"];
    provConfig["seat"] = "12341";

    config.creatives.push_back(Creative::video(640, 480, 10, 600, "cr1", 1));

    // Configure every creative
    for (auto& creative: config.creatives) {
        auto& creativeConfig = creative.providerConfig["brightroll"];
        creativeConfig["nurl"] = "http://adserver.com?brid=%{bidrequest.id}&impid=%{imp.id}&price=##BRX_CLEARING_PRICE##";

        creativeConfig["adomain"] = "http://jamloop.com";
        creativeConfig["campaign_name"] = "test_campaign";
        creativeConfig["line_item_name"] = "line_item";
        creativeConfig["creative_name"] = "test_creative";
        creativeConfig["creative_duration"] = creative.duration;
        creativeConfig["media_desc"]["media_mime"] = "video/x-flv";
        creativeConfig["media_desc"]["media_bitrate"] = creative.bitrate;
        creativeConfig["api"] = 1;
        creativeConfig["lid"] = "428885";
        creativeConfig["landingpage_url"] = "http://jamloop.com";
        creativeConfig["advertiser_name"] = "jamloop";
    }

    auto agent = std::make_shared<TestAgent>(proxies, "agent");
    agent->config = config;
    agent->bidWithFixedAmount(USD_CPM(10));
    stack.addAgent(agent);

    stack.runThen(
        routerConfig, bidderConfig, USD_CPM(10), 0,
        [&](const Json::Value& config)
    {

        const auto& bids = config["workers"][0]["bids"];
        auto url = bids["url"].asString();

        NetworkAddress address(url);
        ExchangeSource exchangeConnection(address);

        auto bidRequest = readBidRequest();

        std::ostringstream oss;
        oss << "POST /auctions HTTP/1.1\r\n"
            << "Content-Length: " << bidRequest.size() << "\r\n"
            << "Content-Type: application/octet-stream\r\n"
            << "Connection: Keep-Alive\r\n"
            << "x-openrtb-version: 2.2\r\n"
            << "\r\n"
            << bidRequest;

        exchangeConnection.write(oss.str());

        auto response = exchangeConnection.read();
        HttpHeader header;
        header.parse(response);

        std::cerr << response << std::endl;

        BOOST_CHECK_EQUAL(header.resource, "200");

    });
}
