/* redis_viewability_backend.h
   Mathieu Stefani, 24 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Redis backend storage for viewability data
*/

#pragma once

#include "soa/service/redis.h"

namespace JamLoop {

class ViewabilityBackend {
};

class RedisViewabilityBackend : public ViewabilityBackend {
public:
    RedisViewabilityBackend(const Redis::Address& addr);

private:
    std::unique_ptr<Redis::AsyncConnection> writeConn;
    std::unique_ptr<Redis::AsyncConnection> readConn;
};

} // namespace JamLoop
