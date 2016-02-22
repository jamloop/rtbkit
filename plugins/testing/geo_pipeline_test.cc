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
using namespace Jamloop;

constexpr const char *IpFile       = "configs/GeoLite2-City-Blocks-IPv4.csv.gz";
constexpr const char *LocationFile = "configs/GeoLite2-City-Locations-en.csv.gz";

BOOST_AUTO_TEST_CASE( test_ip_addr ) {
    BOOST_CHECK_EQUAL(toAddr("23.5.178.10"), 23UL << 24 | 5UL << 16 | 178UL << 8 | 10 << 0);
    BOOST_CHECK_EQUAL(toAddr("1.4.1.0"),     1UL << 24  | 4UL << 16 | 1UL << 8   | 0 << 0);
}

struct IPRange {

    struct Iterator {
        Iterator(InAddr addr, size_t incrIndex)
            : incrIndex(incrIndex)
        {
            vals[3] = (addr >> 24) & 0xFF;
            vals[2] = (addr >> 16) & 0xFF;
            vals[1] = (addr >> 8) & 0xFF;
            vals[0] = addr & 0xFF;
        }

        Iterator(uint8_t *v, size_t incrIndex)
            : incrIndex(incrIndex)
        {
            std::memcpy(vals, v, sizeof(vals));
        }

        std::string asString() const {
            std::ostringstream oss;
            oss << (int)vals[3] << "." << (int)vals[2] << "." << (int)vals[1] << "." << (int)vals[0];
            return oss.str();
        }

        Iterator operator++() {
            if (vals[incrIndex] == 255) {
                ++incrIndex;
            }
            ++vals[incrIndex];
            return Iterator(vals, incrIndex);
        }

        std::string operator*() const {
            return asString();
        }

        bool operator==(Iterator other) const {
            return vals[3] == other.vals[3] && vals[2] == other.vals[2] && vals[1] == other.vals[1] && vals[0] == other.vals[0];
        }

        bool operator!=(Iterator other) const {
            return !(operator==(other));
        }

    private:
        uint8_t vals[4];
        size_t incrIndex;
    };

    IPRange(Iterator first, Iterator last)
        : first(first)
        , last(last)  {
    }

    static IPRange fromCIDR(const char* subnet, int bits) {
        uint32_t mask = (uint32_t(-1) << (32 - bits)) & uint32_t(-1);

        auto addr = toAddr(subnet);
        auto last = addr | ~mask;

        return IPRange(Iterator(addr, 0), Iterator(last, 0));
    }

    Iterator begin() const { return first; }
    Iterator end() const { return last; }

    Iterator first;
    Iterator last;
};

#define CIDR(bits, mask) IPRange::fromCIDR(#bits, mask)

const struct TestData {
    IPRange range;
    uint32_t metro;
} tests[] = {
    { CIDR(24.94.248.0, 24), 606 },
    { CIDR(71.44.104.0, 24), 698 },
    { CIDR(97.67.115.0, 24), 522 },
    { CIDR(98.214.32.0, 25), 717 },
    { CIDR(99.44.32.0, 22), 825 }
};

BOOST_AUTO_TEST_CASE( test_ip_mapping )
{
    GeoDatabase db;
    std::cout << "Loading Database" << std::endl;
    db.load(IpFile, LocationFile);

    for (auto test: tests) {

        for (auto ip: test.range) {
            auto metroCode = db.findMetro(ip);
            BOOST_CHECK(!metroCode.empty());
            if (std::stoi(metroCode) != test.metro) {
                std::cerr << "Found a mismatch for IP[" << ip << "], got metro = " << metroCode << ", expected " << test.metro << std::endl;
                BOOST_CHECK(false);
            }
        }

    }

}
