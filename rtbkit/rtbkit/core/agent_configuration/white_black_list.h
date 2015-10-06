/* liverail_exchange_connector.h
   Mathieu Stefani, 05 October 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
    A multiple-lookup whitelist / blacklist
*/

#pragma once

#include <string>
#include <unordered_set>
#include <array>
#include <type_traits>
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

        Json::Value toJson() const;
        void createFromJson(const Json::Value& value);

        void createFromFile(std::string whiteFile, std::string blackFile, std::vector<std::string> fields);

        template<typename T, size_t Size>
        WhiteBlackResult filter(const std::array<T, Size>& arr) const {
            return filterImpl(std::begin(arr), std::end(arr), Size);
        }

        WhiteBlackResult filter(const std::vector<std::string>& values) const;

        bool empty() const {
            return !white.empty() && !black.empty();
        }

    private:
        std::string formatKey(const std::vector<std::string>& values) const;

        template<typename ForwardIterator>
        WhiteBlackResult filterImpl(ForwardIterator first, ForwardIterator last, size_t size) const {
            static_assert(
                std::is_same<typename std::iterator_traits<ForwardIterator>::value_type, std::string>::value,
                "Lookups are restricted to std::string"
            );

            if (size != fields.size()) {
                throw ML::Exception("values size does not match fields size (%lu != %lu)", size, fields.size());
            }

            std::vector<std::string> toLookup;
            toLookup.reserve(size);
            for (auto it = first; it != last; ++it) {
                toLookup.push_back(*it);
                auto key = formatKey(toLookup);

                if (black.find(key) != std::end(black)) {
                    return WhiteBlackResult::Blacklisted;
                }
                else if (white.find(key) != std::end(white)) {
                    return WhiteBlackResult::Whitelisted;
                }
            }
            return WhiteBlackResult::NotFound;
        }

        std::string whiteFile;
        std::string blackFile;
        std::vector<std::string> fields;

        std::unordered_set<std::string> white;
        std::unordered_set<std::string> black;
    };
} // namespace JamLoop
