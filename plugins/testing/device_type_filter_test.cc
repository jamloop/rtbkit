/* device_type_filter_test.cc
   Mathieu Stefani, 24 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Unit test for the device type filter
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "rtbkit/core/router/filters/static_filters.h"
#include "rtbkit/core/router/filters/testing/utils.h"
#include "rtbkit/core/agent_configuration/agent_config.h"
#include "rtbkit/common/bid_request.h"

using namespace RTBKIT;
using namespace JamLoop;

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

template<typename T, typename Enum>
IncludeExclude<T> makeIE(std::initializer_list<Enum> include, std::initializer_list<Enum> exclude) {
    IncludeExclude<T> ie;
    for (auto inc: include) {
        T obj;
        obj.val = static_cast<int>(inc);
        ie.include.push_back(obj);
    }

    for (auto exc: exclude) {
        T obj;
        obj.val = static_cast<int>(exc);
        ie.exclude.push_back(obj);
    }
    return ie;
}


BOOST_AUTO_TEST_CASE( test_device_type ) {
    DeviceTypeFilter filter;
    ConfigSet mask;

    auto doCheck = [&] (
            BidRequest& request,
            const string& exchangeName,
            const initializer_list<size_t>& expected)
    {
        check(filter, request, exchangeName, mask, expected);
    };

    auto makeBr = [](OpenRTB::DeviceType::Vals val) {
        BidRequest br;
        br.device.reset(new OpenRTB::Device);
        br.device->devicetype.val = val;

        return br;
    };

    AgentConfig c0;
    c0.deviceTypeFilter = makeIE<OpenRTB::DeviceType>({ OpenRTB::DeviceType::PC }, { });
    
    AgentConfig c1;
    c1.deviceTypeFilter = makeIE<OpenRTB::DeviceType>({ OpenRTB::DeviceType::TABLET }, { OpenRTB::DeviceType::PC });

    AgentConfig c2;
    c2.deviceTypeFilter = makeIE<OpenRTB::DeviceType>( {OpenRTB::DeviceType::PC, OpenRTB::DeviceType::TV }, { });

    addConfig(filter, 0, c0);   mask.set(0);
    addConfig(filter, 1, c1);   mask.set(1);
    addConfig(filter, 2, c2);   mask.set(2);

    BidRequest br0 = makeBr(OpenRTB::DeviceType::PC);
    BidRequest br1 = makeBr(OpenRTB::DeviceType::PHONE);

    doCheck(br0, "test1", { 0, 2 });
    doCheck(br1, "test2", {  });
}
