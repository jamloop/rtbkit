/* file_watcher.h
   Mathieu Stefani, 23 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   An AsyncEventSource to watch for file modifications that uses inotify
   underneath
*/

#include <sys/inotify.h>
#include <type_traits>
#include "soa/service/async_event_source.h"

namespace JamLoop {

namespace Utils {

#define INOTIFY_FLAGS \
    FLAG(Access, IN_ACCESS) \
    FLAG(MetadataChange, IN_ATTRIB) \
    FLAG(ClosingWrite, IN_CLOSE_WRITE) \
    FLAG(ClosingNoWrite, IN_CLOSE_NOWRITE) \
    FLAG(Creation, IN_CREATE) \
    FLAG(Deletion, IN_DELETE) \
    FLAG(SelfDeletion, IN_DELETE_SELF) \
    FLAG(Modification, IN_MODIFY) \
    FLAG(SelfMoving, IN_MOVE_SELF) \
    FLAG(MovedFrom, IN_MOVED_FROM) \
    FLAG(MovedTo, IN_MOVED_TO) \
    FLAG(Opening, IN_OPEN)

enum class WatchFor : std::uint32_t {
    #define FLAG(flag, val) flag = val,
         INOTIFY_FLAGS
    #undef FLAG
};

inline WatchFor operator|(WatchFor lhs, WatchFor rhs) {
    typedef uint32_t UnderlyingType;

    return static_cast<WatchFor>(
            static_cast<UnderlyingType>(lhs) | static_cast<UnderlyingType>(rhs));
}

struct FileWatcher : public Datacratic::AsyncEventSource
{

    struct Event {
        Event(
            std::string name,
            WatchFor flags)
          : name(std::move(name))
          , flags(flags)
        { }

        std::string name;
        WatchFor flags;
    };

    typedef std::function<void (Event)> OnNotify;

    FileWatcher(OnNotify notify);

    int selectFd() const;

    void startWatching(const std::string& pathName, WatchFor mask);
    void startWatching(const char* pathName, WatchFor mask);

    void stopWatching(const std::string& pathName);

    bool processOne();

private:
    int toInotifyMask(WatchFor watchFor) const;

    WatchFor toWatchFor(uint32_t mask) const;

    int inotifyFd;
    OnNotify onNotify;
};

} // namespace Utils

} // namespace JamLoop
