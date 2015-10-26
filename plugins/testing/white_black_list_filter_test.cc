/* white_black_list_filter_test.cc
   Mathieu Stefani, 23 October 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Unit tests for the White/Black list filter
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "rtbkit/core/router/filters/static_filters.h"
#include "rtbkit/core/router/filters/testing/utils.h"
#include "rtbkit/core/agent_configuration/agent_config.h"
#include "rtbkit/common/bid_request.h"
#include "soa/types/url.h"

using namespace RTBKIT;
using namespace JamLoop;

namespace {

// Taken from static_filters_test.cc
void check(
        const FilterBase& filter,
        BidRequest& request,
        const string exchangeName,
        const ConfigSet& mask,
        const initializer_list<size_t>& exp)
{
    FilterExchangeConnector conn(exchangeName);

    /* Note that some filters depends on the bid request's exchange field while
       others depend on the exchange connector's name. Now you might think that
       they're always the same but you'd be wrong. To alleviate my endless pain
       on this subject, let's just fudge it here and call it a day.
     */
    request.exchange = exchangeName;

    // A bid request without ad spots doesn't really make any sense and will
    // accidently make state.configs() to return an empty set.
    request.imp.emplace_back();

    CreativeMatrix activeConfigs;
    for (size_t i = mask.next(); i < mask.size(); i = mask.next(i+1))
        activeConfigs.setConfig(i, 1);

    FilterState state(request, &conn, activeConfigs);

    filter.filter(state);
    check(state.configs() & mask, exp);
}

enum class Level { Site, Publisher, Url };

BidRequest makeBr(const std::string& value, Level level) {
    BidRequest br;

    switch (level) {
    case Level::Site:
    case Level::Publisher:
        br.site.reset(new OpenRTB::Site());
        if (level == Level::Site) {
            br.site->domain = value;
        } else {
            br.site->publisher.reset(new OpenRTB::Publisher());
            br.site->publisher->domain = value;
        }
        break;
    case Level::Url:
        br.url = Url(value);
    }

    return br;
};

AgentConfig makeConfig(
        std::initializer_list<std::string> white, std::initializer_list<std::string> black) {

    AgentConfig config;

    for (auto w: white) config.whiteBlackList.addWhite(w);
    for (auto b: black) config.whiteBlackList.addBlack(b);

    return config;
};

}

BOOST_AUTO_TEST_CASE( white_black_list_simple )
{
    WhiteBlackListFilter filter;
    ConfigSet configMask;

    auto doCheck = [&] (
            BidRequest& request,
            const string& exchangeName,
            const initializer_list<size_t>& expected)
    {
        check(filter, request, exchangeName, configMask, expected);
    };

    auto c0 = makeConfig({"foxbusiness.com", "nytimes.com", "nouvelobs.com", "lyrics.com"}, { });
    auto c1 = makeConfig({"about.com", "bbc.com", "bodybuilding.com", "bloomberg.com"}, { });
    auto c2 = makeConfig({"bbc.com", "cbs.com"}, { });
    auto c3 = makeConfig({"foxbusiness.com", "nytimes.com", "nouvelobs.com" }, { "yahoo.com", "answers.com" });
    auto c4 = makeConfig({"answers.com", "nouvelobs.com" }, { "bbc.com" });

    addConfig(filter, 0, c0);  configMask.set(0);
    addConfig(filter, 1, c1);  configMask.set(1);
    addConfig(filter, 2, c2);  configMask.set(2);
    addConfig(filter, 3, c3);  configMask.set(3);
    addConfig(filter, 4, c4);  configMask.set(4);

    auto doTestDomain = [&](Level level) {
        auto br0 = makeBr("foxbusiness.com", level);
        auto br1 = makeBr("bbc.com", level);

        doCheck(br0, "white1", { 0, 3 });
        doCheck(br1, "white2", { 1, 2 });

        auto br2 = makeBr("yahoo.com", level);
        auto br3 = makeBr("answers.com", level);

        doCheck(br2, "black1", { });
        doCheck(br3, "black2", { 4 });

        auto br4 = makeBr("fox.com", level);
        doCheck(br4, "notfound", { });
    };

    doTestDomain(Level::Site);
    doTestDomain(Level::Publisher);

}

BOOST_AUTO_TEST_CASE( white_black_list_directory )
{
    WhiteBlackListFilter filter;
    ConfigSet configMask;

    auto doCheck = [&] (
            BidRequest& request,
            const string& exchangeName,
            const initializer_list<size_t>& expected)
    {
        check(filter, request, exchangeName, configMask, expected);
    };

    auto c0 = makeConfig({"foxbusiness.com", "nytimes.com/info", "lyrics.com" }, { });
    auto c1 = makeConfig({"lyrics.com/index", "bbc.com" }, { "nytimes.com/site" });
    auto c2 = makeConfig({"nytimes.com/site", "bbc.com" }, { });

    addConfig(filter, 0, c0);   configMask.set(0);
    addConfig(filter, 1, c1);   configMask.set(1);
    addConfig(filter, 2, c2);   configMask.set(2);

    auto doTestDomain = [&](Level level) {
        auto br0 = makeBr("nytimes.com", level);
        auto br1 = makeBr("lyrics.com", level);

        doCheck(br0, "white1", { });
        doCheck(br1, "white2", { 0 });
    };

    doTestDomain(Level::Site);
    doTestDomain(Level::Publisher);

    auto br2 = makeBr("http://nytimes.com/info", Level::Url);
    doCheck(br2, "white3", { 0 });

    auto br3 = makeBr("http://www.nytimes.com/site/index.html", Level::Url);
    doCheck(br3, "black1", { 2 });

    auto br4 = makeBr("http://www.foxbusiness.com/markets/2015/10/26/gold-rises-on-dipping-dollar-fed-uncertainty/?intcmp=marketfeatures", Level::Url);
    doCheck(br4, "white", { 0 });
}
