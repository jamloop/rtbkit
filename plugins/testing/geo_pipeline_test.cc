/* geo_pipeline_test.cc
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Unit tests for the GeoPipeline
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "geo_pipeline.h"
#include "soa/jsoncpp/json.h"

using namespace RTBKIT;
using namespace Datacratic;
using namespace Jamloop;

constexpr const char *IpFile       = "configs/GeoLite2-City-Blocks-IPv4.csv.gz";
constexpr const char *LocationFile = "configs/GeoLite2-City-Locations-en.csv.gz";

BOOST_AUTO_TEST_CASE( test_ip_addr ) {
    BOOST_CHECK_EQUAL(toAddr("23.5.178.10"), 23UL << 24 | 5UL << 16 | 178UL << 8 | 10 << 0);
    BOOST_CHECK_EQUAL(toAddr("1.4.1.0"),     1UL << 24  | 4UL << 16 | 1UL << 8   | 0 << 0);
}

namespace IP {

struct Range {

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

    Range(Iterator first, Iterator last)
        : first(first)
        , last(last)  {
    }

    static Range fromCIDR(const char* subnet, int bits) {
        uint32_t mask = (uint32_t(-1) << (32 - bits)) & uint32_t(-1);

        auto addr = toAddr(subnet);
        auto last = addr | ~mask;

        return Range(Iterator(addr, 0), Iterator(last, 0));
    }

    Iterator begin() const { return first; }
    Iterator end() const { return last; }

    Iterator first;
    Iterator last;
};

#define CIDR(bits, mask) Range::fromCIDR(#bits, mask)

const struct Data {
    Range range;
    uint32_t metro;
    std::string zip;
    std::string country;
    std::string region;
} Tests[] = {
    { CIDR(24.94.248.0, 24), 606, "36362", "US", "AL" },
    { CIDR(71.44.104.0, 24), 698, "36024", "US", "AL" },
    { CIDR(97.67.115.0, 24), 522, "36027", "US", "AL" },
    { CIDR(98.214.32.0, 25), 717, "62351", "US", "IL" },
    { CIDR(99.44.32.0, 22), 825, "91932", "US", "CA" }
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

#if 0
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
#endif

BOOST_AUTO_TEST_CASE( test_metro_distribution )
{
    struct Stats {

        Stats()
            : totalLines(0)
            , totalMatches(0)
        { }

        size_t totalLines;
        size_t totalMatches;

        struct Entry {
            Entry()
                : latLonCount(0)
                , ipCount(0)
            { }
            size_t latLonCount;
            size_t ipCount;

            size_t total() const {
                return latLonCount + ipCount;
            }
        };

        std::map<std::string, Entry> dmaDistribution;
        std::vector<std::pair<InAddr, int>> subnetDistribution;

        void record(const GeoDatabase::Result& result) {
            auto& entry = dmaDistribution[result.metroCode];

            if (result.matchType == GeoDatabase::MatchType::LatLon)
                ++entry.latLonCount;
            else {
                ++entry.ipCount;

                auto it = std::find_if(subnetDistribution.begin(), subnetDistribution.end(), [&](const std::pair<InAddr, int>& entry) {
                        return entry.first == result.ip;
                });

                if (it == std::end(subnetDistribution)) {
                    auto newEntry = std::make_pair(result.ip, 1);
                    subnetDistribution.push_back(newEntry);
                } else {
                    auto& val = it->second;
                    ++val;
                }
            }

            ++totalMatches;
        }

        void dump() {
            std::cout << "Stats:" << std::endl;
            std::cout << "Total parsed lines: " << totalLines << std::endl;
            std::cout << "Total matched DMAs: " << totalMatches << std::endl;
            std::cout << "DMA distribution:" << std::endl;

            auto pct = [](size_t val, size_t max) {
                return (val * 100) / max;
            };

            const std::string indent(4, ' ');
            for (const auto& d: dmaDistribution) {
                std::cout << d.first << " -> " << d.second.total() << " ("  << pct(d.second.total(), totalMatches) << "%)" << std::endl;
                std::cout << indent << "IP      -> " << d.second.ipCount << " (" << pct(d.second.ipCount, d.second.total()) << "%)" << std::endl;
                std::cout << indent << "lat/lon -> " << d.second.latLonCount << " (" << pct(d.second.latLonCount, d.second.total()) << "%)" << std::endl;
            }
            std::cout << "Subnet distribution:" << std::endl;

            auto ipStr = [](InAddr ip) {
                auto a = (ip >> 24) & 0xFF;
                auto b = (ip >> 16) & 0xFF;
                auto c = (ip >> 8) & 0xFF;
                auto d = ip & 0xFF;

                std::ostringstream oss;
                oss << a << "." << b << "." << c << "." << d;
                return oss.str();
            };

            std::sort(subnetDistribution.begin(), subnetDistribution.end(), [](const std::pair<InAddr, int>& lhs, const std::pair<InAddr, int>& rhs) {
                    return lhs.second > rhs.second;
            });

            for (const auto&s : subnetDistribution) {
                std::cout << ipStr(s.first) << " -> " << s.second << " (" << pct(s.second, totalMatches) << "%)" << std::endl;
            }
        }
    };

    std::ifstream in("plugins/testing/adap-9976-brs.log");
    if (!in)
        throw ML::Exception("Could not read bid requests file");
    std::string br;
    Stats stats;
    
    while (std::getline(in, br)) {
        auto request = Json::parse(br);
        auto& device = request["device"];
        auto ip = device.get("ip", "").asString();
        auto& geo = device["geo"];

        auto lat = device.get("lat", std::numeric_limits<double>::quiet_NaN()).asDouble();
        auto lon = device.get("lon", std::numeric_limits<double>::quiet_NaN()).asDouble();

        GeoDatabase::Context context {
            ip,
            lat,
            lon
        };

        bool found;
        GeoDatabase::Result result;

        std::tie(found, result) = db->lookup(context);
        if (found) {
            stats.record(result);
        }

        ++stats.totalLines;
    }

    stats.dump();
}

BOOST_AUTO_TEST_SUITE_END()
