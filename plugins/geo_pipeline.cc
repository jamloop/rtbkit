/* geo_pipeline.cc
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Geo Pipeline
*/

#include "geo_pipeline.h"
#include "jml/utils/filter_streams.h"

using namespace Datacratic;
using namespace RTBKIT;

namespace Jamloop {

InAddr toAddr(const char* str) {
    size_t shift = 24 ;

    InAddr res = 0;

    uint8_t byte = 0;
    const char *p = str;
    while (*p) {
        if (*p == '.') {
            if (shift == 0) return InAddrNone;

            res |= (InAddr(byte) << shift);
            shift -= 8;
            byte = 0;
        } else {
            if (!isdigit(*p)) return InAddrNone;

            byte *= 10;
            byte += *p - '0';
        }

        ++p;
    }

    // Last one
    res |= byte;

    return res;
}

std::vector<std::string> split(const std::string& str, char c) {
    std::istringstream iss(str);
    std::vector<std::string> res;
    std::string line;
    while (std::getline(iss, line, c))
        res.push_back(std::move(line));

    return res;
}

const std::string GeoDatabase::NoMetro;

GeoDatabase::Entry
GeoDatabase::Entry::fromIp(InAddr ip) {
    Entry e;
    e.u.ip = ip;
    return e;
}

GeoDatabase::Entry
GeoDatabase::Entry::fromGeo(double latitude, double longitude) {
    Entry e;
    e.u.latitude = latitude;
    e.u.longitude = longitude;
    return e;
}

InAddr
GeoDatabase::Entry::ip() const {
    return u.ip;
}

std::pair<double, double>
GeoDatabase::Entry::geo() const {
    return std::make_pair(u.latitude, u.longitude);
}

bool
GeoDatabase::Entry::isLocated(double latitude, double longitude) const {
    // @Note Might need to tweak it a little bit or even put it configurable
    static constexpr double Epsilon = 1e-3;

    auto almostEquals = [&](double lhs, double rhs) {
        return std::fabs(lhs - rhs) < Epsilon;
    };

    return almostEquals(latitude, u.latitude) && almostEquals(longitude, u.longitude);
}

GeoDatabase::GeoDatabase() {
    loadGuard.store(false, std::memory_order_relaxed);
}

uint64_t
GeoDatabase::makeGeoHash(double latitude, double longitude, Precision precision) {
    return precision.scale<uint64_t>(latitude) << 32 | precision.scale<uint32_t>(longitude);
}

uint64_t
GeoDatabase::makeGeoHash(const GeoDatabase::Entry& entry, Precision precision) {
    /* Structured binding soon ?
     * auto {latitude, longitude} = entry.geo();
     */

    double latitude;
    double longitude;

    std::tie(latitude, longitude) = entry.geo();
    return makeGeoHash(latitude, longitude, precision);
}

uint64_t
GeoDatabase::makeGeoHash(const GeoDatabase::Context& context, Precision precision) {
    ExcAssert(context.hasValidGeo());

    return makeGeoHash(context.latitude, context.longitude, precision);
}

void
GeoDatabase::load(
        const std::string& ipFile, const std::string& locationFile,
        Precision precision)
{
    ML::filter_istream ipFs(ipFile);
    if (!ipFs)
        throw ML::Exception("Could not open IP file '%s'", ipFile.c_str());

    ML::filter_istream locationFs(locationFile);
    if (!locationFs)
        throw ML::Exception("Could not open Location file '%s'", locationFile.c_str());

    std::string line;
    if (!std::getline(ipFs, line))
        throw ML::Exception("Could not read IP file header");

    auto ipHeaders = split(line, ',');

    if (!std::getline(locationFs, line))
        throw ML::Exception("Could not read Location file header");

    auto locationHeaders = split(line, ',');

    struct LocationEntry {
        std::string metroCode;
        uint32_t geoNameId;

        // Useful for debug
        std::string subDivision1Name;
    };

    std::unordered_map<uint32_t, LocationEntry> locationIndex;
    while (std::getline(locationFs, line)) {
        auto fields = split(line, ',');
        if (fields.size() != locationHeaders.size()) continue;

        auto geoId = fields.at(0);
        auto metroCode = fields.at(11);
        if (metroCode.empty()) continue;

        auto subDivision1Name = fields.at(7);

        auto geoNameId = std::stoi(geoId);

        locationIndex.insert(
                std::make_pair(geoNameId, LocationEntry { metroCode, geoNameId, std::move(subDivision1Name) }));
    }

    size_t count { 1 };
    size_t skipped { 0 };

    while (std::getline(ipFs, line)) {
        auto fields = split(line, ',');
        ++count;

        if (fields.size() != ipHeaders.size()) {
            ++skipped;
            continue;
        }

        auto subnet = fields.at(0);
        auto geoName = fields.at(1);

        auto latitude = fields.at(7);
        auto longitude = fields.at(8);

        std::string ip;
        auto cidrPos = subnet.find('/');
        if (cidrPos == std::string::npos) ip = std::move(subnet);
        else ip = subnet.substr(0, cidrPos);

        auto addr = toAddr(ip.c_str());
        if (addr == InAddrNone)
            throw ML::Exception("Encountered invalid IP '%s', line '%lu'", ip.c_str(), count);

        auto geoNameId = std::stoi(geoName);
        auto locationIt = locationIndex.find(geoNameId);
        if (locationIt == std::end(locationIndex)) {
            ++skipped;
            continue;
        }

        const auto& locationEntry = locationIt->second;

        auto ipEntry = Entry::fromIp(addr);
        ipEntry.metroCode = locationEntry.metroCode;
        subnets.push_back(ipEntry);

        auto geoEntry = Entry::fromGeo(std::stod(latitude), std::stod(longitude));
        geoEntry.metroCode = locationEntry.metroCode;

        auto geoHash = makeGeoHash(geoEntry, precision);
        auto& geoEntries = geoloc[geoHash];
        geoEntries.push_back(geoEntry);

    }

    std::sort(std::begin(subnets), std::end(subnets), [](const Entry& lhs, const Entry& rhs) {
        return lhs.ip() < rhs.ip();
    });

    precision_ = precision;

    std::cout << "Parsed " << count << " lines, skipped " << skipped << " (" << ((skipped * 100) / count) << "%)" << std::endl;

    loadGuard.store(true, std::memory_order_release);
}

bool
GeoDatabase::isLoaded() const {
    return loadGuard.load();
}

std::string
GeoDatabase::findMetro(const GeoDatabase::Context& context) {
    auto loaded = loadGuard.load(std::memory_order_acquire);
    if (!loaded) {
        return GeoDatabase::NoMetro;
    }

    if (context.hasValidGeo()) {
        auto it = geoloc.find(makeGeoHash(context, precision_));
        if (it != std::end(geoloc)) {
            const auto& entries = it->second;
            for (const auto& entry: entries) {
                if (entry.isLocated(context.latitude, context.longitude))
                    return entry.metroCode;
            }
        }
    }

    if (context.ip.empty())
        return GeoDatabase::NoMetro;

    auto addr = toAddr(context.ip.c_str());
    if (addr == InAddrNone) {
        return GeoDatabase::NoMetro;
    }

    auto it = std::lower_bound(
            std::begin(subnets), std::end(subnets), addr, [&](const Entry& lhs, InAddr rhs) {
            return lhs.ip() < rhs;
    });

    if (it == std::end(subnets) || it == std::begin(subnets)) {
        return GeoDatabase::NoMetro;
    }

    if (it->ip() > addr)
        --it;
    return it->metroCode;
}

void
GeoDatabase::loadAsync(
        const std::string& ipFile, const std::string& locationFile,
        GeoDatabase::Precision precision) {
    std::thread thr([=]() { load(ipFile, locationFile, precision); });
    thr.detach();
};

GeoPipeline::GeoPipeline(
        std::shared_ptr<Datacratic::ServiceProxies> proxies,
        std::string serviceName, const Json::Value& config)
    : BidRequestPipeline(std::move(proxies), std::move(serviceName))
{
    auto ipFile = config["ipFile"].asString();
    auto locationFile = config["locationFile"].asString();
    auto prec = config["precision"].asDouble();

    db.reset(new GeoDatabase);
    db->loadAsync(ipFile, locationFile, GeoDatabase::Precision(prec));
}

PipelineStatus
GeoPipeline::preBidRequest(
        const ExchangeConnector* exchange,
        const HttpHeader& header,
        const std::string& payload) {
    return PipelineStatus::Continue;
}

PipelineStatus
GeoPipeline::postBidRequest(
        const ExchangeConnector* exchange,
        const std::shared_ptr<Auction>& auction) {

    auto &br = auction->request;

    if (br->user && br->user->geo)
       if (!br->user->geo->metro.empty()) return PipelineStatus::Continue;

    std::string ip;

    if (br->device);
        ip = br->device->ip;

    double latitude, longitude;
    latitude = longitude = std::numeric_limits<double>::quiet_NaN();
    if (br->user && br->user->geo) {
        latitude = br->user->geo->lat.val;
        longitude = br->user->geo->lon.val;
    }

    GeoDatabase::Context context {
        std::move(ip),
        latitude,
        longitude
    };

    auto metro = db->findMetro(context);
    if (metro != GeoDatabase::NoMetro) {
        if (!br->user) br->user.reset(new OpenRTB::User);
        if (!br->user->geo) br->user->geo.reset(new OpenRTB::Geo);

        br->user->geo->metro = std::move(metro);
    }

    return PipelineStatus::Continue;
}

} // namespace Jamloop

namespace {

struct Init {
    Init() {
        PluginInterface<BidRequestPipeline>::registerPlugin("geo",
                [](std::string serviceName,
                   std::shared_ptr<ServiceProxies> proxies,
                   const Json::Value& config) {
                return new Jamloop::GeoPipeline(std::move(proxies), std::move(serviceName), config);
        });
    }
} init;

}
