/* moat_data_parser.cc
   Mathieu Stefani, 27 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the MOAT Data Parser
*/

#include "moat_data_parser.h"
#include "jml/utils/exc_assert.h"
#include <fstream>

using namespace std;
using namespace Datacratic;
using namespace ML;

namespace JamLoop {

namespace Utils {

    namespace {
        std::string dirName(const std::string& name) {
            auto index = name.rfind('/');
            if (index == std::string::npos)
                throw ML::Exception("Invalid file name '%s'", name.c_str());

            return name.substr(0, index);
        }

        std::string baseName(const std::string& name) {
            auto index = name.rfind('/');
            if (index == std::string::npos)
                throw ML::Exception("Invalid file name '%s'", name.c_str());

            return name.substr(index + 1);
        }
    }

    std::string
    MoatDataParser::Line::
    fieldValue(std::string name) const
    {
        auto fieldIt = find(begin(orderedFieldIndexes), end(orderedFieldIndexes),
                            name);
        if (fieldIt == end(orderedFieldIndexes))
            throw ML::Exception("Unknown field name '%s'", name.c_str());

        const auto index = distance(begin(orderedFieldIndexes), fieldIt);
        return fields[index];
    }

    std::string
    MoatDataParser::Line::
    fieldAt(size_t index) const
    {
        ExcCheck(index < fields.size(), "Invalid index");

        return fields[index];
    }

    MoatDataParser::
    MoatDataParser(const std::string& dataFile, OnChange onChange)
       : dataFile(baseName(dataFile))
       , onChange(onChange)
    {
        watcher = std::make_shared<FileWatcher>(
            [this](FileWatcher::Event event) {
                handleFileEvent(event);
        });

        std::string dataDir = dirName(dataFile);
        watcher->startWatching(dataDir,  WatchFor::Modification);

        addSource("MoatDataParser::fileWatcher", watcher);

    }

    void
    MoatDataParser::handleFileEvent(FileWatcher::Event event) const
    {
        if (event.name != dataFile) return;

        ifstream in(event.name);
        if (!in) {
            throw ML::Exception("Could not open '%s' file for reading",
                                event.name.c_str());
        }

        std::string line;

        auto split = [](const std::string &str, char c) -> std::vector<std::string> {
            std::istringstream iss(str);

            std::vector<std::string> parts;
            std::string cur;
            while (std::getline(iss, cur, c)) {
                parts.push_back(cur);
            }

            return parts;
        };

        /* Reading fields order first */
        std::getline(in, line);

        auto orderedFields = split(line, ',');
        auto fieldsCount = orderedFields.size();

        std::vector<Line> lines;

        /* Now read line by line */
        while (std::getline(in, line)) {
            auto fields = split(line, ',');
            /* Make sure we are reading the right number of fields */
            //ExcAssertEqual(fields.size(), fieldsCount);

            lines.push_back(Line(std::move(fields), orderedFields));
        }

        onChange(std::move(lines));

    }

} // namespace Utils

} // namespace JamLoop
