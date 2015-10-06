#pragma once

#include <string>
#include <unordered_set>
#include "jml/utils/filter_streams.h"
#include "jml/arch/exception.h"
#include "soa/jsoncpp/value.h"
#include "soa/service/json_codec.h"

namespace JamLoop {

    enum class WhiteBlackResult {
        Whitelisted,
        Blacklisted,
        NotFound
    };

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

    template<typename T>
    class WhiteBlackList {
    public:

        template<typename Arg>
        void addWhite(Arg&& arg) {
            white.insert(std::forward<Arg>(arg));
        }

        template<typename Arg>
        void addBlack(Arg&& arg) {
            black.insert(std::forward<Arg>(arg));
        }

        Json::Value toJson() const {
            Json::Value ret(Json::objectValue);
            if (!whiteFile.empty() && !blackFile.empty()) {
                ret["whiteFile"] = whiteFile;
                ret["blackFile"] = blackFile;
            }
            else {
                ret["whiteList"] = jsonEncode(white);
                ret["blackList"] = jsonEncode(black);
            }
            return ret;            
        }

        void createFromJson(const Json::Value& value) {
            white.clear();
            black.clear();

            if (value.isMember("whiteFile") && !value.isMember("blackFile")) {
                throw ML::Exception("Missing 'blackFile' parameter for WhiteBlackList");
            }
            if (value.isMember("blackFile") && !value.isMember("whiteFile")) {
                throw ML::Exception("Missing 'whiteFile' parameter for WhiteBlackList");
            }

            if ((value.isMember("whiteFile") || value.isMember("blackFile")) && (value.isMember("whiteList") || value.isMember("blackList"))) {
                throw ML::Exception("Invalid mutually exclusive parameters for WhiteBlackList");
            }

            std::string whiteFile;
            std::string blackFile;

            for (auto it = value.begin(), end = value.end(); it != end; ++it) {
                auto key = it.memberName();
                if (key != "whiteFile" && key != "blackFile" &&
                    key != "whiteList" && key != "blackList")
                {
                    throw ML::Exception("Invalid key for WhiteBlackList '%s'", key.c_str());
                }
                else {
                    if (key == "whiteFile") whiteFile = std::move(key);
                    else if (key == "blackFile") blackFile = std::move(key);
                    else if (key == "whiteList" || key == "blackList") {
                        auto val = *it;
                        if (!val.isArray()) throw ML::Exception("whiteList parameter must be an array");
                        
                        for (const auto& w: val) {
                            auto elem = jsonParse(w); 
                            if (key == "whiteList") white.insert(std::move(elem));
                            else black.insert(std::move(elem));
                        }
                    }
                }
            }

            if (!whiteFile.empty() && !blackFile.empty()) {
                createFromFile(std::move(whiteFile), std::move(blackFile));
            }
        }

        void createFromFile(std::string whiteFile, std::string blackFile) {
            ML::filter_istream whiteIn(whiteFile);
            if (!whiteIn) {
                throw ML::Exception("Could not open whitelist file '%s'", whiteFile.c_str());
            }

            ML::filter_istream blackIn(blackFile);
            if (!blackIn) {
                throw ML::Exception("Could not open blacklist file '%s'", blackFile.c_str());
            }

            std::string elem;
            while (std::getline(whiteIn, elem)) {
                white.insert(elem);
            }
            while (std::getline(blackIn, elem)) {
                black.insert(elem);
            }

            whiteFile = std::move(whiteFile);
            blackFile = std::move(blackFile);
        }

        WhiteBlackResult filter(const T& value) const {
            if (black.find(value) != std::end(black)) {
                return WhiteBlackResult::Blacklisted;
            }
            else if (white.find(value) != std::end(white)) {
                return WhiteBlackResult::Whitelisted;
            }
            else {
                return WhiteBlackResult::NotFound;
            }
        }

        bool empty() const {
            return !white.empty() && !black.empty();
        }

    private:
        std::string whiteFile;
        std::string blackFile;

        std::unordered_set<T> white;
        std::unordered_set<T> black;
    };
} // namespace JamLoop
