/* http_viewability_augmentor_runner.cc
   Mathieu Stefani, 06 avril 2016
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Runner for the http viewability augmentor
*/

#include "http_viewability_augmentor.h"
#include "http_augmentor_headers.h"

using namespace Datacratic;

int main() {
    auto proxies = std::make_shared<ServiceProxies>();
    JamLoop::registerAugmentorHeaders();
    JamLoop::ViewabilityEndpoint endpoint("*:9080", "http-viewability", proxies);

    endpoint.init(1);

    endpoint.start();
}
