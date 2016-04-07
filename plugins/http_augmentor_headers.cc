/* http_augmentor_headers.cc
   Mathieu Stefani, 06 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
*/

#include "http_augmentor_headers.h"
#include "jml/utils/parse_context.h"

using namespace Net;
using namespace Net::Http;

namespace JamLoop {

void
VersionHeader::parseRaw(const char* str, size_t len) {
    ML::Parse_Context context("VersionHeader", str, len);

    int major = context.expect_int(0, INT_MAX);
    int minor = 0;
    if (context.match_literal('.')) {
        minor = context.expect_int(0, INT_MAX);
    }

    major_ = major;
    minor_ = minor;
}

void
VersionHeader::write(std::ostream& os) const {
    os << major_ << '.'  << minor_;
}

void
XRTBKitTimestamp::parseRaw(const char* str, size_t len) {
}

void
XRTBKitTimestamp::write(std::ostream& os) const {
    auto epoch
        = std::chrono::duration_cast<std::chrono::seconds>(ts_.time_since_epoch());
    os << epoch.count();
}

void
XRTBKitAuctionId::parseRaw(const char* str, size_t len) {
    id_ = std::string(str, len);
}

void
XRTBKitAuctionId::write(std::ostream& os) const {
    os << id_;
}

void
XRTBKitAugmentorName::parseRaw(const char* str, size_t len) {
}

void
XRTBKitAugmentorName::write(std::ostream& os) const {
    os << name_;
}

void
registerAugmentorHeaders() {
    Header::Registry::registerHeader<XOpenRTBVersion>();
    Header::Registry::registerHeader<XRTBKitProtocolVersion>();
    Header::Registry::registerHeader<XRTBKitTimestamp>();
    Header::Registry::registerHeader<XRTBKitAuctionId>();
    Header::Registry::registerHeader<XRTBKitAugmentorName>();
}

} // namespace JamLoop

