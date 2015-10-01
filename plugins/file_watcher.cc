/* file_watcher.cc
   Mathieu Stefani, 23 July 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   Implementation of the file watcher
*/

#include "file_watcher.h"

using namespace Datacratic;

namespace JamLoop {

namespace Utils {

    FileWatcher::
    FileWatcher(OnNotify onNotify)
        : onNotify(onNotify)
    {
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd == -1) {
            throw ML::Exception(errno, "inotify_init");
        }

        inotifyFd = fd;

    }

    int
    FileWatcher::selectFd() const
    {
        return inotifyFd;
    }

    void
    FileWatcher::startWatching(const std::string& pathName, WatchFor mask)
    {
        startWatching(pathName.c_str(), mask);
    }

    void
    FileWatcher::startWatching(const char* pathName, WatchFor mask)
    {
        int res = inotify_add_watch(inotifyFd, pathName, toInotifyMask(mask));
        if (res == -1) {
            throw ML::Exception(errno, "inotify_add_watch");
        }
    }

    void
    FileWatcher::stopWatching(const std::string& pathName)
    {
        throw ML::Exception("Unimplemented!");
    }

    bool
    FileWatcher::processOne()
    {
        static constexpr size_t MaxEvents = 10;
        static constexpr size_t BufSize = (MaxEvents * (sizeof (struct inotify_event) + NAME_MAX + 1));

        char buf[BufSize];
        memset(buf, 0, BufSize);

        ssize_t res = read(inotifyFd, buf, BufSize);
        if (res == -1) {
            if (errno == EAGAIN) {
                return false;
            }
            throw ML::Exception(errno, "read()");
        }

        for (const char *p = buf; p < buf + res; ) {
            const auto event = reinterpret_cast<const inotify_event *>(p);

            onNotify(Event(event->name, toWatchFor(event->mask)));

            p += sizeof (struct inotify_event) + event->len;
        }


        return false;
    }

    int
    FileWatcher::toInotifyMask(WatchFor mask) const
    {
        static constexpr int flags[][2] = {
            #define FLAG(from, to) { static_cast<int>(WatchFor::from), to },
                 INOTIFY_FLAGS
            #undef FLAG
        };

        int finalMask { 0 };
        const int m = static_cast<uint32_t>(mask);

        static constexpr size_t size = sizeof flags / sizeof *flags;
        for (size_t i = 0; i < size; ++i) {
            if ((m & flags[i][0]) == flags[i][0]) finalMask |= flags[i][1];
        }


        return finalMask;
    }
    
    WatchFor
    FileWatcher::toWatchFor(uint32_t mask) const
    {
        WatchFor result;

        static constexpr int flags[][2] = {
            #define FLAG(from, to) { to, static_cast<int>(WatchFor::from) },
                 INOTIFY_FLAGS
            #undef FLAG
        };

        static constexpr size_t size = sizeof flags / sizeof *flags;
        for (size_t i = 0; i < size; ++i) {
            if ((mask & flags[i][0]) == flags[i][0])
                 result = result | static_cast<WatchFor>(flags[i][1]);
        }

        return result;
    }


} // namespace Utils

} // namespace JamLoop
