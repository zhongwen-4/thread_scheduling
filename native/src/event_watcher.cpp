#include "event_watcher.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__) && __has_include(<linux/cn_proc.h>) && __has_include(<linux/connector.h>)
#define THREAD_SCHED_HAS_PROC_CONNECTOR 1
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#endif

namespace thread_sched {

EventWatcher::EventWatcher() = default;

EventWatcher::~EventWatcher() {
    if (socket_fd_ >= 0) close(socket_fd_);
}

bool EventWatcher::Start(std::string* error) {
#if defined(THREAD_SCHED_HAS_PROC_CONNECTOR)
    socket_fd_ = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (socket_fd_ < 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = CN_IDX_PROC;
    address.nl_pid = static_cast<unsigned>(getpid());
    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        if (error) *error = std::strerror(errno);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    struct {
        nlmsghdr header;
        cn_msg message;
        proc_cn_mcast_op operation;
    } request{};
    request.header.nlmsg_len = sizeof(request);
    request.header.nlmsg_type = NLMSG_DONE;
    request.header.nlmsg_pid = static_cast<unsigned>(getpid());
    request.message.id.idx = CN_IDX_PROC;
    request.message.id.val = CN_VAL_PROC;
    request.message.len = sizeof(request.operation);
    request.operation = PROC_CN_MCAST_LISTEN;
    sockaddr_nl kernel_address{};
    kernel_address.nl_family = AF_NETLINK;
    if (sendto(socket_fd_, &request, sizeof(request), 0,
               reinterpret_cast<sockaddr*>(&kernel_address), sizeof(kernel_address)) < 0) {
        if (error) *error = std::strerror(errno);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    return true;
#else
    if (error) *error = "proc connector headers are unavailable";
    return false;
#endif
}

bool EventWatcher::WaitForEvent(int timeout_ms, int* pid, std::string* error) {
    if (socket_fd_ < 0) return false;
    pollfd descriptor{socket_fd_, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeout_ms);
    if (ready <= 0) {
        if (ready < 0 && errno != EINTR && error) *error = std::strerror(errno);
        return false;
    }
    char buffer[4096]{};
    const ssize_t size = recv(socket_fd_, buffer, sizeof(buffer), 0);
    if (size <= 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
#if defined(THREAD_SCHED_HAS_PROC_CONNECTOR)
    const auto* header = reinterpret_cast<const nlmsghdr*>(buffer);
    int remaining = static_cast<int>(size);
    for (const nlmsghdr* current = header; NLMSG_OK(current, remaining); current = NLMSG_NEXT(current, remaining)) {
        if (NLMSG_PAYLOAD(current, 0) < sizeof(cn_msg)) continue;
        const auto* message = reinterpret_cast<const cn_msg*>(NLMSG_DATA(current));
        if (message->id.idx != CN_IDX_PROC || message->id.val != CN_VAL_PROC) continue;
        if (message->len < sizeof(proc_event) ||
            message->len > NLMSG_PAYLOAD(current, 0) - sizeof(cn_msg)) continue;
        const auto* event = reinterpret_cast<const proc_event*>(message->data);
        if (event->what == PROC_EVENT_FORK) {
            *pid = static_cast<int>(event->event_data.fork.child_pid);
            return true;
        }
    }
#else
    (void)pid;
#endif
    return false;
}

}  // namespace thread_sched
