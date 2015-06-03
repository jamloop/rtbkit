/* tremor_exchange_connector_test.cc
   Mathieu Stefani, 03 June 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Unit tests for the Tremor Exchange
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "tremor_exchange_connector.h"

#include "rtbkit/common/testing/exchange_source.h"
#include "rtbkit/testing/bid_stack.h"
#include "soa/service/http_header.h"

namespace {

    constexpr const char* SampleBR = R"JSON(
    {"site":{"content":{"series":"book reading","episode":1,"keywords":"Orwell, 1984","userrating":"3","len":129,"url":"http://cdnp.tremormedia.com/video/1984.flv","id":"eb9f13ede5fd225333971523f60... is the video title","season":"1","context":"1","contentrating":"G","videoquality":2},"id":"fk0y7","ref":"http://demo.tremormedia.com/~TAM/rtb/test_supply/index.php?adCode Tremor TAM test supply","domain":"demo.tremormedia.com","publisher":{"id":"1b79c05b-39c4-43a5-9ad8-f66a2e9fad3d","name":"Tremor TAM TEST SUPPLY Publisher"}},"id":"3bca28bc-5697-417b-a045-35e00000bd46","tmax":200,"imp":[{"id":"1","instl":0,"displaymanager":"tremor","secure":0,"displaymanagerver":"1","video":{"startdelay":0,"w":720,"minduration":0,"maxextended":0,"linearity":1,"mimes":["application/x-shockwave-flash","video/x-flv"],"protocols":[2,5],"boxingallowed":1,"api":[1],"maxduration":30,"h":480,"pos":1}}],"at":2,"device":{"os":"Mac OS X","geo":{"region":"QC","type":2,"country":"CAN"},"osv":"10.10.3","flashver":"14.0.0.145","ua":"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/43.0.2357.81 Safari/537.36","devicetype":2,"language":"en","dnt":1,"ip":"198.154.184.0"},"cur":["USD"],"regs":{"coppa":1},"user":{}})JSON";

}

using namespace RTBKIT;
using namespace Jamloop;

BOOST_AUTO_TEST_CASE( test_tremor )
{
    BidStack stack;
    auto proxies = stack.proxies;

    Json::Value routerConfig;
    routerConfig[0]["exchangeType"] = "tremor";

    Json::Value bidderConfig;
    bidderConfig["type"] = "agents";

    AgentConfig config;
    config.bidProbability = 1;
    config.account = { "campaign", "strategy" };

    config.creatives.push_back(Creative::video(720, 480, 10, 16000, "cr1", 1));

    // Configure every creative
    for (auto& creative: config.creatives) {
        auto& creativeConfig = creative.providerConfig["tremor"];
        creativeConfig["adomain"][0] = "jamloop.com";
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

        const auto httpRequest = ML::format(
            "POST /auctions HTTP/1.1\r\n"
            "Content-Length: %zd\r\n"
            "Content-Type: application/json\r\n"
            "Connection: Keep-Alive\r\n"
            "x-openrtb-version: 2.2\r\n"
            "\r\n"
            "%s",
            std::strlen(SampleBR),
            SampleBR);

        exchangeConnection.write(httpRequest);

        auto response = exchangeConnection.read();
        HttpHeader header;
        header.parse(response);

        std::cerr << response << std::endl;

        BOOST_CHECK_EQUAL(header.resource, "200");

    });
}
