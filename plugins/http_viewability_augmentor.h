/* http_viewability_augmentor.h
   Mathieu Stefani, 06 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the Viewability Augmentor over HTTP
*/

#pragma once

#include <pistache/endpoint.h>
#include "soa/service/service_base.h"

namespace JamLoop {

class Worker;

class ViewabilityEndpoint : public Datacratic::ServiceBase {
public:
    ViewabilityEndpoint(
            Net::Address addr,
            std::string serviceName,
            std::shared_ptr<Datacratic::ServiceProxies> proxies);

    void init(int thr);
    void start();

private:
    std::shared_ptr<Net::Http::Endpoint> httpEndpoint;
    std::shared_ptr<Worker> worker;
};

} // namespace JamLoop
