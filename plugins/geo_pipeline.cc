/* geo_pipeline.cc
   Mathieu Stefani, 19 février 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Geo Pipeline
*/

#include "geo_pipeline.h"
#include "jml/utils/filter_streams.h"
#include "soa/utils/scope.h"

using namespace Datacratic;
using namespace RTBKIT;

namespace Jamloop {

bool toAddr(const char* str, InAddr* out) {
    size_t shift = 24 ;

    InAddr res = 0;

    uint8_t byte = 0;
    const char *p = str;
    while (*p) {
        if (*p == '.') {
            if (shift == 0) return false;

            res |= (InAddr(byte) << shift);
            shift -= 8;
            byte = 0;
        } else {
            if (!isdigit(*p)) return false;

            byte *= 10;
            byte += *p - '0';
        }

        ++p;
    }

    // Last one
    res |= byte;

    *out = res;
    return true;

}

struct Subnet {
    explicit Subnet(InAddr addr, int bits)
        : bits_(bits)
        , mask_(createMask(bits))
        , host_(addr & mask_)
    {
        createString();
    }

    bool isIn(InAddr addr) const {
        return (addr & mask_) == host_;
    }

    bool isIn(const std::string& ip) const {
        InAddr addr;
        bool ok = toAddr(ip.c_str(), &addr);
        if (!ok) return false;

        return isIn(addr);
    }

    std::string toString() const {
        return str_;
    }

private:
    int bits_;
    uint32_t mask_;
    InAddr host_;

    std::string str_;

    uint32_t createMask(int bits) const {
        ExcCheck(bits > 0, "Invalid bits (must be > 0)");
        ExcCheck(bits <= 32, "Invalid bits (must be <= 32)");

        uint32_t mask = (uint32_t(-1) << (32 - bits)) & uint32_t(-1);
        return mask;
    }

    void createString() {
        std::ostringstream oss;
        uint8_t a = (host_ >> 24) & 0xFF;
        uint8_t b = (host_ >> 16) & 0xFF;
        uint8_t c = (host_ >> 8) & 0xFF;
        uint8_t d = (host_ >> 0) & 0xFF;

        oss << a << '.' << b << '.' << c << '.' << d << '/' << bits_;
        str_ = oss.str();
    }
};

struct Ipv4 {
    Ipv4(InAddr addr)
        : addr(addr)
    { }

    static Ipv4 fromString(const char* str) {
        InAddr out;
        bool ok = toAddr(str, &out);
        if (!ok)
            throw ML::Exception("Invalid IP address: '%s'", str);

        return Ipv4(out);
    }

    InAddr addr;
};

#define IP(addr) Ipv4::fromString(#addr)

Subnet operator/(Ipv4 ip, int bits) {
    return Subnet(ip.addr, bits);
}

static Subnet BannedSubnets[] = {
    IP(196.62.0.0)    / 24,
    IP(161.8.128.0)   / 18,
    IP(161.8.192.0)   / 19,
    IP(161.8.224.0)   / 20,
    IP(161.8.240.0)   / 20,
    IP(161.8.248.0)   / 22,
    IP(161.8.252.0)   / 23,
    IP(161.8.253.0)   / 23,
    IP(185.120.56.0)  / 24,
    IP(212.22.65.0)   / 23,
    IP(212.22.80.0)   / 23,
    IP(212.22.82.0)   / 24,
    IP(212.22.88.0)   / 24,
    IP(212.22.89.0)   / 24,
    IP(212.22.90.0)   / 24,
    IP(212.22.91.0)   / 24,
    IP(213.169.150.0) / 24
};


std::string cleanField(const std::string& str) {
    auto first = str.begin(), last = str.end();
    if (str.front() == '"')
        std::advance(first, 1);
    if (str.back() == '"')
        std::advance(last, -2); // -2 because end() points past the end

    return std::string(first, last);
}

std::vector<std::string> split(const std::string& str, char c) {
    std::istringstream iss(str);
    std::vector<std::string> res;
    std::string line;
    while (std::getline(iss, line, c))
        res.push_back(std::move(line));

    return res;
}

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
    static constexpr double Epsilon = 1e-2;

    auto almostEquals = [&](double lhs, double rhs) {
        return std::fabs(lhs - rhs) < Epsilon;
    };

    return almostEquals(latitude, u.latitude) && almostEquals(longitude, u.longitude);
}

GeoDatabase::Result
GeoDatabase::Entry::toResult() const {
    return Result { metroCode, zipCode, countryCode, region };
}

GeoDatabase::GeoDatabase(
        const std::string& prefix,
        std::shared_ptr<ServiceProxies> proxies)
    : events(new EventRecorder(prefix, std::move(proxies)))
{
    dataGuard.store(nullptr, std::memory_order_relaxed);
}

GeoDatabase::~GeoDatabase() {
    auto *d = dataGuard.load(std::memory_order_acquire);
    if (d) delete d;
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
        std::string countryCode;
        std::string region;

        uint32_t geoNameId;

        // Useful for debug
        std::string subDivision1Name;
    };

    Data* d = new Data();

    std::unordered_map<uint32_t, LocationEntry> locationIndex;
    while (std::getline(locationFs, line)) {
        auto fields = split(line, ',');
        if (fields.size() != locationHeaders.size()) continue;

        auto geoId = fields.at(0);
        auto metroCode = fields.at(11);
        if (metroCode.empty()) continue;

        auto countryCode = fields.at(4);
        auto region = fields.at(6);
        auto subDivision1Name = fields.at(7);

        auto geoNameId = std::stoi(geoId);

        locationIndex.insert(
                std::make_pair(geoNameId, LocationEntry { std::move(metroCode), std::move(countryCode), std::move(region), geoNameId, std::move(subDivision1Name) }));
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

        auto postalCode = cleanField(fields.at(6));
        auto latitude = fields.at(7);
        auto longitude = fields.at(8);

        std::string ip;
        auto cidrPos = subnet.find('/');
        if (cidrPos == std::string::npos) ip = std::move(subnet);
        else ip = subnet.substr(0, cidrPos);

        InAddr addr;
        auto ok = toAddr(ip.c_str(), &addr);
        if (!ok)
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
        ipEntry.zipCode = postalCode;
        ipEntry.countryCode = locationEntry.countryCode;
        ipEntry.region = locationEntry.region;
        d->subnets.push_back(ipEntry);

        auto geoEntry = Entry::fromGeo(std::stod(latitude), std::stod(longitude));
        geoEntry.metroCode = locationEntry.metroCode;
        geoEntry.zipCode = postalCode;
        geoEntry.countryCode = locationEntry.countryCode;
        geoEntry.region = locationEntry.region;

        auto geoHash = makeGeoHash(geoEntry, precision);
        auto& geoEntries = d->geoloc[geoHash];
        geoEntries.push_back(geoEntry);

    }

    std::sort(std::begin(d->subnets), std::end(d->subnets), [](const Entry& lhs, const Entry& rhs) {
        return lhs.ip() < rhs.ip();
    });

    d->precision = precision;

    dataGuard.store(d, std::memory_order_release);

    std::cout << "Parsed " << count << " lines, skipped " << skipped << " (" << ((skipped * 100) / count) << "%)" << std::endl;
}

bool
GeoDatabase::isLoaded() const {
    return dataGuard.load(std::memory_order_acquire) != nullptr;
}

std::pair<bool, GeoDatabase::Result>
GeoDatabase::lookup(const GeoDatabase::Context& context) {
    auto start = Date::now();
    auto exit = ScopeExit([=]() noexcept {
        auto end = Date::now();
        events->recordOutcome(end.secondsSince(start) * 1000, "matchTimeMs");
    });
    // @Note since we have a data-dependency here (pointer load), we could
    // in theory use consume memory ordering. However, compilers do not
    // implement it correctly and emit a simple acquire barrier. Also,
    // since we do not target platforms with data-dependency reordering,
    // we mostly don't care.
    auto data = dataGuard.load(std::memory_order_acquire);

    auto recordUnmatch = [=](const char* key) {
        events->recordHit("unmatch.total");
        events->recordHit("unmatch.detail.%s", key);
    };

    static const auto NoEntry = std::make_pair(false, Result { });

    auto makeResult = [](const Entry& entry) {
        return std::make_pair(true, entry.toResult());
    };

    if (!data) {
        recordUnmatch("noData");
        return NoEntry;
    }

    if (context.hasValidGeo()) {
        auto it = data->geoloc.find(makeGeoHash(context, data->precision));
        if (it != std::end(data->geoloc)) {
            const auto& entries = it->second;
            for (const auto& entry: entries) {
                if (entry.isLocated(context.latitude, context.longitude)) {
                    events->recordHit("match.latlon");
                    return makeResult(entry);
                }
            }

            recordUnmatch("latlon.noHit");
        } else {
            recordUnmatch("latlon.unknownHash");
        }
    } else {
        recordUnmatch("noLatLon");
    }

    if (context.ip.empty()) {
        recordUnmatch("noIp");
        return NoEntry;
    }

    InAddr addr;
    bool ok = toAddr(context.ip.c_str(), &addr);
    if (!ok) {
        recordUnmatch("invalidIp");
        return NoEntry;
    }

    auto it = std::lower_bound(
            std::begin(data->subnets), std::end(data->subnets), addr, [&](const Entry& lhs, InAddr rhs) {
            return lhs.ip() < rhs;
    });

    if (it == std::end(data->subnets) || it == std::begin(data->subnets)) {
        recordUnmatch("unknownSubnet");
        return NoEntry;
    }

    if (it->ip() > addr)
        --it;

    events->recordHit("match.ip");
    return makeResult(*it);
}

void
GeoDatabase::loadAsync(
        const std::string& ipFile, const std::string& locationFile,
        GeoDatabase::Precision precision) {
    std::thread thr([=]() { load(ipFile, locationFile, precision); });
    thr.detach();
};

GeoPipeline::GeoPipeline(
        const std::shared_ptr<Datacratic::ServiceProxies>& proxies,
        std::string serviceName, const Json::Value& config)
    : BidRequestPipeline(proxies, std::move(serviceName))
{
    auto ipFile = config["ipFile"].asString();
    auto locationFile = config["locationFile"].asString();
    auto prec = config["precision"].asDouble();

    db.reset(new GeoDatabase(this->serviceName() + ".geo", proxies));
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

    if (!ip.empty()) {
        auto it = std::find_if(std::begin(BannedSubnets), std::end(BannedSubnets), [&](const Subnet& subnet) {
            return subnet.isIn(ip);
        });


        if (it != std::end(BannedSubnets)) {
            auto subnet = *it;

            recordHit("subnet.blacklist.total");
            recordHit("subnet.blacklist.detail.%s", subnet.toString().c_str());

            return PipelineStatus::Stop;
        }

    }

    double latitude, longitude;
    latitude = longitude = std::numeric_limits<double>::quiet_NaN();
    if (br->user && br->user->geo) {
        latitude = br->user->geo->lat.val;
        longitude = br->user->geo->lon.val;
    } else if (br->device && br->device->geo) {
        latitude = br->device->geo->lat.val;
        longitude = br->device->geo->lon.val;
    }

    GeoDatabase::Context context {
        std::move(ip),
        latitude,
        longitude
    };

    bool found;
    GeoDatabase::Result result;

    std::tie(found, result) = db->lookup(context);

    if (found) {
        if (!br->user) br->user.reset(new OpenRTB::User);
        if (!br->user->geo) br->user->geo.reset(new OpenRTB::Geo);

        br->user->geo->metro = std::move(result.metroCode);
        br->user->geo->country = std::move(result.countryCode);
        br->user->geo->region = std::move(result.region);
        br->user->geo->zip = Datacratic::UnicodeString(std::move(result.zipCode));
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
