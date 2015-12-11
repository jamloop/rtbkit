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
    class CsvReader {
    public:

        class Row {
        public:
            friend class CsvReader;

            std::string value(const std::string& column) const {
                auto it = data.find(column);
                if (it == std::end(data))
                    throw ML::Exception("Row has no value for '%s'", column.c_str());

                return it->second;
            }
        private:
            void add(const std::string& key, const std::string& value) {
                data.insert(std::make_pair(key, value));
            }

            std::unordered_map<std::string, std::string> data;

        };

        CsvReader(const std::string& fileName, char delimiter = ',')
            : file(fileName)
            , delimiter(delimiter)

        {
            open();
        }

        typedef std::vector<Row> Rows;
        typedef Rows::iterator iterator;
        typedef Rows::const_iterator const_iterator;

        void open();

        iterator begin() {
            return rows_.begin();
        }

        iterator end() {
            return rows_.end();
        }

        const_iterator begin() const {
            return rows_.begin();
        }

        const_iterator end() const {
            return rows_.end();
        }

    private:
        std::string file;
        char delimiter;
        ML::filter_istream stream_;
        Rows rows_;
    };

    class WhiteBlackList {
    public:
        typedef std::string Domain;

        static constexpr const char* Wildcard = "*";

        enum class Result {
            Whitelisted,
            Blacklisted,
            NotFound
        };

        struct Context {
            Datacratic::Url url;
            std::string exchange;
            std::string pubid;
        };

        Json::Value toJson() const;

        void addWhite(
                const std::string& url,
                const std::string& exchange,
                const std::string& pubid);
        void addBlack(
                const std::string& url,
                const std::string& exchange,
                const std::string& pubid);

        void createFromJson(const Json::Value& value);
        void createFromFile(std::string whiteFile, std::string blackFile);

        Result filter(const Domain& domain, const Context& context) const;

        bool empty() const {
            return white.empty() && black.empty();
        }

    private:
        std::string whiteFile;
        std::string blackFile;

        struct List {
            struct Entry {
            public:

                Entry(
                    const std::string& page,
                    const std::string& exchange,
                    const std::string& publisher);

                bool matches(const Context& context) const;

            private:
                std::string page;
                std::string exchange;
                std::string publisher;
            };

            void add(
                    const std::string& domain, const std::string& dir,
                    const std::string& exch, const std::string& pubid);

            void addWildcard(const std::string& exch, const std::string& pubid);
            bool matches(const std::string& domain, const Context& context) const;

            bool empty() const;
            void clear();

#ifdef REDUCE_MEMORY_ALLOCS
            typedef ML::compact_vector<Entry, 4> Entries;
#else
            typedef std::vector<Entry> Entries;
#endif

        private:
            std::string makeWildcard(const std::string& exch, const std::string& pubid) const;
            std::unordered_map<Domain, Entries> domains;
            std::unordered_set<std::string> wildcards;
        };

        List white;
        List black;

        void addList(List& list, const CsvReader::Row& row);
        void addList(List& list,
                const std::string& url,
                const std::string& exchange,
                const std::string& pubid);

        std::pair<Domain, std::string> splitDomain(std::string url) const;
    };

    const char* whiteBlackString(WhiteBlackList::Result result);
} // namespace JamLoop
