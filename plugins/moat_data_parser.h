/* moat_data_parser.h
   Mathieu Stefani, 27 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Parser for raw MOAT data
*/

#pragma once

#include "soa/service/message_loop.h"
#include "file_watcher.h"

namespace JamLoop {

namespace Utils {

class MoatDataParser : public Datacratic::MessageLoop {
public:

    /* @Idea: Type-safe CSV Parser ? */

    struct Line {
        Line(std::vector<std::string> &&fields, const std::vector<std::string>& orderedFields)
            : fields(std::move(fields))
            , orderedFieldIndexes(orderedFields)
        { }

        std::string fieldValue(std::string name) const;;
        std::string fieldAt(size_t index) const;

    private:
        std::vector<std::string> fields;
        std::vector<std::string> orderedFieldIndexes;
    };

    typedef std::function<void (std::vector<Line>&&)> OnChange;


    MoatDataParser(const std::string& dataFile, OnChange onChange);

private:
    std::string dataFile;
    OnChange onChange;
    std::shared_ptr<FileWatcher> watcher;

    void handleFileEvent(FileWatcher::Event event) const;

};

} // namespace Utils

} // namespace JamLoop
