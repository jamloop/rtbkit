/* viewability_augmentor.cc
   Mathieu Stefani, 23 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the MOAT Viewability augmentor
*/

#include "viewability_augmentor.h"

using namespace std;
using namespace RTBKIT;
using namespace Datacratic;

namespace JamLoop {

    ViewabilityAugmentor::ViewabilityAugmentor(
        shared_ptr<ServiceProxies> proxies,
        string serviceName)
        : AsyncAugmentor(
            ViewabilityAugmentor::Name, std::move(serviceName), std::move(proxies))
    {
    }
    
    
    ViewabilityAugmentor::
    ViewabilityAugmentor(
            ServiceBase& parent,
            string serviceName)
    : AsyncAugmentor(
            ViewabilityAugmentor::Name, std::move(serviceName), parent)
    {
    }
    

    void
    ViewabilityAugmentor::init(int nthreads)
    {
        AsyncAugmentor::init(nthreads);
        agentConfig = std::make_shared<AgentConfigurationListener>(getZmqContext());
        agentConfig->init(getServices()->config);

        addSource("ViewabilityAugmentor::agentConfig", agentConfig);
    }

    void
    ViewabilityAugmentor::onRequest(
            const AugmentationRequest& request,
            AsyncAugmentor::SendResponseCB sendResponse)
    {
    }

} // namespace JamLoop
