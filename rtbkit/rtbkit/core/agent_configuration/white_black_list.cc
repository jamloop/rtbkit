/* white_black_list.cc
   Mathieu Stefani, 15 October 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the White and Black List
*/

#include "white_black_list.h"

namespace JamLoop {

    const char* whiteBlackString(WhiteBlackList::Result result) {
        switch (result) {
            case WhiteBlackList::Result::Whitelisted:
                return "whitelisted";
            case WhiteBlackList::Result::Blacklisted:
                return "blacklisted";
            case WhiteBlackList::Result::NotFound:
                return "notfound";
        }

        throw ML::Exception("Unreachable");
    }

    WhiteBlackList::Directory::Directory(std::string path)
        : path(std::move(path))
    { }

    bool
    WhiteBlackList::Directory::matches(const Datacratic::Url& url) const {
        auto p = url.path();

        /*
         * domain = www.domain.com/section/subsection
         * page = http://www.domain.com/section/subsection/page.html
         *    -> Match
         *
         * domain = www.domain.com/section/subsection
         * page = http://www.domain.com/foo/section/subsection/page.html
         *    -> Unmatch
         */

        auto pos = p.find(path);
        if (pos != std::string::npos) {
            return pos == 0;
        }

        return false;
    }

    WhiteBlackList::Result
    WhiteBlackList::filter(const Domain& domain, const Datacratic::Url& url) const {
        auto w = white.find(domain);
        if (w != std::end(white)) {
            const auto& directories = w->second;
            for (const auto& dir: directories) {
                if (dir.matches(url)) {
                    return Result::Whitelisted;
                }
            }
        }

        return Result::NotFound;
    }


    Json::Value
    WhiteBlackList::toJson() const {
        Json::Value ret(Json::objectValue);
        if (!whiteFile.empty() && !blackFile.empty()) {
            ret["whiteFile"] = whiteFile;
            ret["blackFile"] = blackFile;
        }
        return ret;            
    }

    void
    WhiteBlackList::createFromJson(const Json::Value& value) {
        white.clear();

        if (value.isMember("whiteFile") && !value.isMember("blackFile")) {
            throw ML::Exception("Missing 'blackFile' parameter for WhiteBlackList");
        }
        if (value.isMember("blackFile") && !value.isMember("whiteFile")) {
            throw ML::Exception("Missing 'whiteFile' parameter for WhiteBlackList");
        }

        std::string whiteFile;
        std::string blackFile;

        for (auto it = value.begin(), end = value.end(); it != end; ++it) {
            auto key = it.memberName();
            if (key != "whiteFile" && key != "blackFile")
            {
                throw ML::Exception("Invalid key for WhiteBlackList '%s'", key.c_str());
            }
            else {
                if (key == "whiteFile") whiteFile = it->asString();
                else if (key == "blackFile") blackFile = it->asString();
            }
        }

        if (!whiteFile.empty() && !blackFile.empty()) {
            createFromFile(std::move(whiteFile), std::move(blackFile));
        }
    }

    void
    WhiteBlackList::createFromFile(std::string whiteFile, std::string blackFile) {
        ML::filter_istream whiteIn(whiteFile);
        if (!whiteIn) {
            throw ML::Exception("Could not open whitelist file '%s'", whiteFile.c_str());
        }

#if 0
        ML::filter_istream blackIn(blackFile);
        if (!blackIn) {
            throw ML::Exception("Could not open blacklist file '%s'", blackFile.c_str());
        }
#endif

        std::string elem;
        while (std::getline(whiteIn, elem)) {
            std::string domain;
            std::string directory;

            std::tie(domain, directory) = splitDomain(elem);
            auto& dirs = white[domain];
            if (!directory.empty()) {
                dirs.push_back(std::move(directory));
            }
        }
#if 0
        while (std::getline(blackIn, elem)) {
            black.insert(elem);
        }
#endif

        this->whiteFile = std::move(whiteFile);
        this->blackFile = std::move(blackFile);
    }

    std::pair<WhiteBlackList::Domain, std::string>
    WhiteBlackList::splitDomain(const std::string& url) const {
        auto slash = url.find('/');
        if (slash == std::string::npos) {
            return std::make_pair(url, "");
        }

        auto domain = url.substr(0, slash);
        auto directory = url.substr(slash + 1);
        return std::make_pair(std::move(domain), std::move(directory));
    }

} // namespace JamLoop
