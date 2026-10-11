#pragma once

#include <string>

namespace thread_sched {

class EventWatcher {
public:
    EventWatcher();
    ~EventWatcher();

    bool Start(std::string* error);
    bool WaitForEvent(int timeout_ms, int* pid, std::string* error);
    bool available() const { return socket_fd_ >= 0; }

private:
    int socket_fd_ = -1;
};

}  // namespace thread_sched
