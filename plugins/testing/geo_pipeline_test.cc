/* geo_pipeline_test.cc
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Unit tests for the GeoPipeline
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "geo_pipeline.h"

using namespace RTBKIT;
using namespace Datacratic;
using namespace Jamloop;

constexpr const char *IpFile       = "configs/GeoIP2-City-Blocks-IPv4.csv.gz";
constexpr const char *LocationFile = "configs/GeoIP2-City-Locations-en.csv.gz";

BOOST_AUTO_TEST_CASE( test_ip_addr ) {
    auto check = [](const char* ip, InAddr expected) {
        InAddr addr;
        bool ok = toAddr(ip, &addr);

        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(expected, addr);
    };

    check("23.5.178.10"    , 23UL << 24 | 5UL << 16 | 178UL << 8 | 10 << 0);
    check("1.4.1.0"        , 1UL << 24  | 4UL << 16 | 1UL << 8   | 0 << 0);
    check("255.255.255.255", 255UL << 24 | 255UL << 16 | 255UL << 8 | 255 << 0);
}

namespace IP {

struct Range {

    struct Iterator {
        Iterator(InAddr addr)
            : addr(addr)
        {
        }

        std::string asString() const {

            std::ostringstream oss;
            oss << ((addr >> 24) & 0xFF) << "." << ((addr >> 16) & 0xFF) << "." << ((addr >> 8) & 0xFF) << "." << ((addr >> 0) & 0xFF);
            return oss.str();
        }

        Iterator operator++() {
            ++addr;
            return Iterator(addr);
        }

        std::string operator*() const {
            return asString();
        }

        bool operator==(Iterator other) const {
            return other.addr == addr;
        }

        bool operator!=(Iterator other) const {
            return !(operator==(other));
        }

    private:
        InAddr addr;
    };

    Range(InAddr first, InAddr last)
        : first(first)
        , last(last)  {
    }

    static Range fromCIDR(const char* subnet, int bits) {
        uint32_t mask = (uint32_t(-1) << (32 - bits)) & uint32_t(-1);

        InAddr addr;

        bool ok = toAddr(subnet, &addr);
        if (!ok) throw ML::Exception("Invalid subnet address '%s'", subnet);

        auto last = addr | ~mask;

        return Range(addr, last);
    }

    Iterator begin() const { return Iterator(first); }
    Iterator end() const { return Iterator(last); }

    InAddr first;
    InAddr last;
};

#define CIDR(bits, mask) Range::fromCIDR(#bits, mask)

const struct Data {
    Range range;
    uint32_t metro;
    std::string zip;
    std::string country;
    std::string region;
} Tests[] = {
    { CIDR(24.94.248.0, 24), 606, "36322", "US", "AL" },
    { CIDR(71.44.104.0, 24), 698, "36109", "US", "AL" },
    { CIDR(97.67.115.0, 24), 522, "36027", "US", "AL" },
    { CIDR(98.214.32.0, 22), 717, "62301", "US", "IL" },
    { CIDR(99.44.32.0, 24), 825, "92154", "US", "CA" }
};

} // namespace IP

namespace Geo {

const struct Data {
    double latitude;
    double longitude;
    uint32_t metro;
} Tests[] = {
    { 35.8846, -118.1522, 803 },
    { 39.871, -120.207, 811 },
    { 33.590, -112.331, 753 },
    { 34.75, -112.01, 753 }
};

} // namespace Geo

struct GeoFixture {

    GeoFixture() {
        std::cout << "Loading database" << std::endl;
        auto proxies = std::make_shared<ServiceProxies>();
        db.reset(new GeoDatabase("test.geo", proxies));
        db->load(IpFile, LocationFile, GeoDatabase::Precision(0.1));
    }

    std::unique_ptr<GeoDatabase> db;
};

// @Note: Ideally, I'd like to load the DB once and share it amongst the tests underneath
// but I did not find how to do it unfortunately
BOOST_FIXTURE_TEST_SUITE(geo_suite, GeoFixture)

BOOST_AUTO_TEST_CASE( test_ip_mapping )
{
    for (auto test: IP::Tests) {

        for (auto ip: test.range) {
            GeoDatabase::Context context {
                ip,
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()
            };

            bool found;
            GeoDatabase::Result result;

            std::tie(found, result) = db->lookup(context);
            BOOST_CHECK(found);
            if (std::stoi(result.metroCode) != test.metro) {
                std::cerr << "Found a mismatch for IP[" << ip << "], got metro = " << result.metroCode << ", expected " << test.metro << std::endl;
                BOOST_CHECK(false);
            }

            if (result.zipCode != test.zip) {
                std::cerr << "Found a mismatch for IP[" << ip << "], got zip = " << result.zipCode << ", expected " << test.zip << std::endl;
                BOOST_CHECK(false);
            }

            if (result.countryCode != test.country) {
                std::cerr << "Found a mismatch for IP[" << ip << "], got country = " << result.countryCode << ", expected " << test.country << std::endl;
                BOOST_CHECK(false);
            }

            if (result.region != test.region) {
                std::cerr << "Found a mismatch for IP[" << ip << "], got region = " << result.region << ", expected " << test.region << std::endl;
                BOOST_CHECK(false);
            }
        }

    }

}

BOOST_AUTO_TEST_CASE( test_geo_mapping )
{
    for (auto test: Geo::Tests) {
        GeoDatabase::Context context {
            "0.0.0.0",
            test.latitude,
            test.longitude
        };

        bool found;
        GeoDatabase::Result result;

        std::tie(found, result) = db->lookup(context);

        auto metroCode = db->lookup(context);
        if (!found || std::stoi(result.metroCode) != test.metro) {
            std::cerr << "Found a mismatch for Geo[lat=" << test.latitude << ",lon=" << test.longitude
                      << "], got metro = " << result.metroCode << ", expected " << test.metro << std::endl;
            BOOST_CHECK(false);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
