#include "redis_viewability_backend.h"

using namespace Datacratic;

namespace JamLoop {

    RedisViewabilityBackend::
    RedisViewabilityBackend(const Redis::Address& addr)
    {
        writeConn.reset(new Redis::AsyncConnection(addr));
        readConn.reset(new Redis::AsyncConnection(addr));
    }

} // namespace JamLoop
