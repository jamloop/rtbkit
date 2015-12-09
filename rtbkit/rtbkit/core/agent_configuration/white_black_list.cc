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

    void
    CsvReader::open() {
         auto split = [](const std::string& str, char sep) {
            std::vector<std::string> result;

            std::istringstream iss(str);
            std::string elem;
            while (std::getline(iss, elem, sep)) {
                result.push_back(std::move(elem));
            }

            return result;
        };

        stream_.open(file);
        if (!stream_) {
            throw ML::Exception("Could not read CSV file '%s'", file.c_str());
        }

        std::string header;
        std::getline(stream_, header);
        auto columns = split(header, delimiter);

        std::string line;
        size_t lineIndex = 1;

        std::vector<Row> rows;
        while (std::getline(stream_, line)) {
            auto v = split(line, delimiter);

            if (columns.size() != v.size()) {
                throw ML::Exception(
                        "Error while parsing '%s': columns do not match header at line '%llu'",
                        file.c_str(), lineIndex);
            }

            Row row;
            for (std::vector<std::string>::size_type i = 0; i < columns.size(); ++i) {
                row.add(columns[i], v[i]);
            }

            rows.push_back(std::move(row));

            ++lineIndex;
        }

        rows_.swap(rows);
    }

    WhiteBlackList::Entry::Entry(
        const std::string& page,
        const std::string& exchange,
        const std::string& publisher)
        : page(page)
        , exchange(exchange)
        , publisher(publisher)
    { }

    bool
    WhiteBlackList::Entry::matches(const WhiteBlackList::Context& context) const {
        auto p = context.url.path();
        auto beg = std::begin(p);
        if (*beg == '/')
            ++beg;

        /*
         * domain = www.domain.com/section/subsection
         * page = http://www.domain.com/section/subsection/page.html
         *    -> Match
         *
         * domain = www.domain.com/section/subsection
         * page = http://www.domain.com/foo/section/subsection/page.html
         *    -> Unmatch
         */

        if (!page.empty() && !std::equal(std::begin(page), std::end(page), beg))
            return false;


        if (exchange == Wildcard) {
            if (publisher == Wildcard)
                return true;

            return publisher == context.pubid;
        }
        else {
            if (publisher == Wildcard) {
                return exchange == context.exchange;
            }

            return exchange == context.exchange && publisher == context.pubid;
        }

        return publisher == context.pubid && exchange == context.exchange;

    }

    void
    WhiteBlackList::addWhite(
            const std::string& url,
            const std::string& exchange,
            const std::string& pubid) {
        addList(white, url, exchange, pubid);
    }

    void WhiteBlackList::addBlack(
            const std::string& url,
            const std::string& exchange,
            const std::string& pubid) {
        addList(black, url, exchange, pubid);
    }

    WhiteBlackList::Result
    WhiteBlackList::filter(
            const Domain& domain, const Context& context) const {

        auto tryMatch = [](
            const List& list, const Domain& domain, const Context& context) {

            auto it = list.find(domain);
            if (it != std::end(list)) {
                const auto& entries = it->second;

                auto matchesUrl = [&context](const Entry& entry) {
                    return entry.matches(context);
                };

                if (std::any_of(std::begin(entries), std::end(entries), matchesUrl)) {
                    return true;
                }

            }

            return false;
        };

        if (tryMatch(white, domain, context))
            return Result::Whitelisted;
        else if (tryMatch(black, domain, context))
            return Result::Blacklisted;

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
        CsvReader whiteReader(whiteFile);
        CsvReader blackReader(blackFile);

        for (const auto& row: whiteReader) {
            addList(white, row);
        }

        for (const auto& row: blackReader) {
            addList(black, row);
        }

        this->whiteFile = std::move(whiteFile);
        this->blackFile = std::move(blackFile);

    }

    void
    WhiteBlackList::addList(List& list, const CsvReader::Row& row) {
        auto url   = row.value("domain");
        auto exch  = row.value("exch");
        auto pubid = row.value("pubid");

        addList(list, url, exch, pubid);
    }

    void
    WhiteBlackList::addList(
            List& list,
            const std::string& url,
            const std::string& exch,
            const std::string& pubid) {
        std::string domain;
        std::string directory;

        std::tie(domain, directory) = splitDomain(url);
        auto& entries = list[domain];
        entries.push_back(Entry(directory, exch, pubid));
    }


    std::pair<WhiteBlackList::Domain, std::string>
    WhiteBlackList::splitDomain(std::string url) const {
        url.erase(std::remove(url.begin(), url.end(), '\r'), url.end());
        url.erase(std::remove(url.begin(), url.end(), '\n'), url.end());

        auto slash = url.find('/');
        if (slash == std::string::npos) {
            return std::make_pair(url, "");
        }

        auto domain = url.substr(0, slash);
        auto directory = url.substr(slash + 1);
        return std::make_pair(std::move(domain), std::move(directory));
    }

} // namespace JamLoop
