/* liverail_exchange_connector.h
   Mathieu Stefani, 05 October 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
    Implementation of the whitelist / blacklist
*/

#include "white_black_list.h"
#include "jml/utils/filter_streams.h"
#include "jml/arch/exception.h"

namespace JamLoop {

Json::Value
WhiteBlackList::toJson() const {
    Json::Value ret(Json::objectValue);
    if (!whiteFile.empty() && !blackFile.empty()) {
        ret["whiteFile"] = whiteFile;
        ret["blackFile"] = blackFile;
    }
    if (!fields.empty()) {
        ret["fields"] = Datacratic::jsonEncode(fields);
    }
    return ret;            
}

void
WhiteBlackList::createFromJson(const Json::Value& value) {
    white.clear();
    black.clear();

    if (value.isMember("whiteFile") && !value.isMember("blackFile")) {
        throw ML::Exception("Missing 'blackFile' parameter for WhiteBlackList");
    }
    if (value.isMember("blackFile") && !value.isMember("whiteFile")) {
        throw ML::Exception("Missing 'whiteFile' parameter for WhiteBlackList");
    }

    std::vector<std::string> fields;
    std::string whiteFile;
    std::string blackFile;

    for (auto it = value.begin(), end = value.end(); it != end; ++it) {
        auto key = it.memberName();
        if (key != "whiteFile" && key != "blackFile" &&
            key != "fields")
        {
            throw ML::Exception("Invalid key for WhiteBlackList '%s'", key.c_str());
        }
        else {
            if (key == "whiteFile") whiteFile = it->asString();
            else if (key == "blackFile") blackFile = it->asString();
            else if (key == "fields") fields = Datacratic::jsonDecode(*it, &fields); 
        }
    }

    createFromFile(std::move(whiteFile), std::move(blackFile), std::move(fields));
}

void
WhiteBlackList::createFromFile(
        std::string whiteFile, std::string blackFile,
        std::vector<std::string> fieldsList)
{
    ML::filter_istream whiteIn(whiteFile);
    if (!whiteIn) {
        throw ML::Exception("Could not open whitelist file '%s'", whiteFile.c_str());
    }

    ML::filter_istream blackIn(blackFile);
    if (!blackIn) {
        throw ML::Exception("Could not open blacklist file '%s'", blackFile.c_str());
    }

    /* Read headers */
    std::string whiteHeader;
    std::string blackHeader;

    if (!std::getline(whiteIn, whiteHeader)) {
        throw ML::Exception("Could not read whiteFile header");
    }
    if (!std::getline(blackIn, blackHeader)) {
        throw ML::Exception("Could not read blackFile header");
    }

    if (whiteHeader != blackHeader) {
        throw ML::Exception("Headers for white and black files don't match");
    }

    auto split = [](const std::string& str, char delim) {
        std::istringstream iss(str);
        std::vector<std::string> tokens;
        std::string elem;

        while (std::getline(iss, elem, delim)) {
            tokens.push_back(std::move(elem));
        }
        return tokens;
    };

    std::vector<std::size_t> indexes;
    auto headerFields = split(whiteHeader, ',');

    if (!fieldsList.empty()) {
        std::transform(begin(fields), end(fields), std::back_inserter(indexes), [&](const std::string& field) {
            auto it = std::find(std::begin(headerFields), std::end(headerFields), field);
            if (it == std::end(headerFields)) {
                throw ML::Exception("Could not find field '%s' in CSV file", field.c_str());
            }
            return std::distance(std::begin(headerFields), it);
        });
        fields = std::move(fieldsList);
    }
    else {
        for (std::vector<std::string>::size_type i = 0; i < headerFields.size(); ++i) {
            indexes.push_back(i);
        }
        fields = std::move(headerFields);
    }

    auto extractFields = [&](const std::string& line) {
        auto tokens = split(line, ',');
        if (tokens.size() < indexes.size()) {
            throw ML::Exception("Missing fields duration extraction for line '%s'", line.c_str());
        }
        
        std::vector<std::string> extracted;
        for (auto index: indexes) {
            if (index >= tokens.size()) {
                throw ML::Exception("Could not extract field number '%lu' for line '%s'", index, line.c_str());
            }

            extracted.push_back(tokens[index]);
        }

        return extracted;

    };

    std::string elem;
    while (std::getline(whiteIn, elem)) {
        auto values = extractFields(elem);
        white.insert(formatKey(values));
    }
    while (std::getline(blackIn, elem)) {
        auto values = extractFields(elem);
        black.insert(formatKey(values));
    }

    whiteFile = std::move(whiteFile);
    blackFile = std::move(blackFile);
}

WhiteBlackResult
WhiteBlackList::filter(const std::vector<std::string>& values) const {
    return filterImpl(std::begin(values), std::end(values), values.size());
}

std::string
WhiteBlackList::formatKey(const std::vector<std::string>& values) const {
    std::ostringstream oss;
    std::copy(std::begin(values), std::end(values) - 2,
         std::ostream_iterator<std::string>(oss, ":"));
    oss << values.back();
    return oss.str();
}

} // namespace JamLoop
