/* white_black_list.h
   Mathieu Stefani, 15 October 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   White and Black List that can be used to white-list domains and sub-directories
*/


#pragma once

#include <string>
#include <unordered_set>
#include "jml/utils/filter_streams.h"
#include "jml/arch/exception.h"
#include "soa/jsoncpp/value.h"
#include "soa/service/json_codec.h"

namespace JamLoop {

    template<typename T>
    Json::Value jsonEncode(const std::unordered_set<T>& set) {
        Json::Value result(Json::arrayValue);
        for (const auto& elem: set) {
            result.append(Datacratic::jsonEncode(elem));
        }
        return result;
    }

    inline std::string jsonParse(const Json::Value& value) {
        return value.asString();
    }

    class WhiteBlackList {
    public:

        typedef std::string Domain;

        enum class Result {
            Whitelisted,
            Blacklisted,
            NotFound
        };

        Json::Value toJson() const;

        void createFromJson(const Json::Value& value);
        void createFromFile(std::string whiteFile, std::string blackFile);

        Result filter(const Domain& domain, const Datacratic::Url& url) const;

        bool empty() const {
            return !white.empty();
        }

    private:
        std::string whiteFile;
        std::string blackFile;

        struct Directory {
            Directory(std::string path);

            bool matches(const Datacratic::Url& url) const;
        private:
            std::string path;
        };

#ifdef REDUCE_MEMORY_ALLOCS
        typedef ML::compact_vector<Directory, 4> Directories;
#else
        typedef std::vector<Directory> Directories;
#endif
        std::unordered_map<Domain, Directories> white;
        std::pair<Domain, std::string> splitDomain(const std::string& url) const;
    };

    const char* whiteBlackString(WhiteBlackList::Result result);
} // namespace JamLoop
