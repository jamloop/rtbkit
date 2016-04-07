/* http_augmentor_headers.h
   Mathieu Stefani, 06 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Headers used by the http augmentor protocol
*/

#pragma once

#include <pistache/http_header.h>
#include <pistache/http_headers.h>
#include <chrono>

namespace JamLoop {

class VersionHeader : public Net::Http::Header::Header {
public:
    VersionHeader()
        : major_(0)
        , minor_(0)
    { }

    VersionHeader(int major, int minor)
        : major_(major)
        , minor_(minor)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

    int majorVersion() const { return major_; }
    int minorVersion() const { return minor_; }

private:
    int major_;
    int minor_;
};

class XOpenRTBVersion : public VersionHeader {
public:
    NAME("X-Operntb-Version")

    XOpenRTBVersion()
        : VersionHeader()
    { }

    explicit XOpenRTBVersion(int major, int minor = 0)
        : VersionHeader(major, minor)
    { }

};

class XRTBKitProtocolVersion : public VersionHeader {
public:
    NAME("X-Rtbkit-Protocol-Version")

    XRTBKitProtocolVersion()
        : VersionHeader()
    { }

    explicit XRTBKitProtocolVersion(int major, int minor = 0)
        : VersionHeader(major, minor)
    { }
};

class XRTBKitTimestamp : public Net::Http::Header::Header {
public:

    typedef std::chrono::system_clock Clock;
    typedef std::chrono::time_point<Clock> TimePoint;

    NAME("X-Rtbkit-Timestamp")

    XRTBKitTimestamp() { }

    explicit XRTBKitTimestamp(TimePoint ts)
        : ts_(ts)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

private:
    TimePoint ts_;
};

class XRTBKitAuctionId : public Net::Http::Header::Header {
public:
    NAME("X-Rtbkit-Auction-Id")

    XRTBKitAuctionId() { }

    explicit XRTBKitAuctionId(const std::string& id)
        : id_(id)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

private:
    std::string id_;
};

class XRTBKitAugmentorName : public Net::Http::Header::Header {
public:
    NAME("X-Rtbkit-Augmentor-Name")

    XRTBKitAugmentorName() { }

    explicit XRTBKitAugmentorName(const std::string& name)
        : name_(name)
    { }

    void parseRaw(const char* str, size_t len);
    void write(std::ostream& os) const;

private:
    std::string name_;
};

void registerAugmentorHeaders();

} // namespace JamLoop
