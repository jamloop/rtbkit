/* utils.h
   Mathieu Stefani, 23 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   A collection of utility functions shared by the rest of the code
*/

#include <string>

#pragma once

namespace Jamloop {
    std::string urldecode(const std::string& url);
} // namespace JamLoop
