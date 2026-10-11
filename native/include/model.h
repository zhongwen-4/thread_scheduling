#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace thread_sched {

enum class BindingMode {
    kScan,
    kSingle,
    kEventDriven,
};

enum class CpuSelectorKind {
    kCpu,
    kRange,
    kGroup,
};

struct CpuSelector {
    CpuSelectorKind kind = CpuSelectorKind::kCpu;
    int first = 0;
    int last = 0;
    std::string group;
};

struct Rule {
    std::string package_name;
    std::string thread_name;
    std::vector<CpuSelector> cpu_selectors;
    BindingMode mode = BindingMode::kScan;
    int sample_interval_ms = 1000;
};

struct ThreadInfo {
    int pid = 0;
    int tid = 0;
    int uid = -1;
    std::string package_name;
    std::string process_name;
    std::string thread_name;
    std::uint64_t utime_ticks = 0;
    std::uint64_t stime_ticks = 0;
    std::uint64_t start_time_ticks = 0;
    int processor = -1;
};

struct Sample {
    std::int64_t timestamp_ms = 0;
    ThreadInfo thread;
    std::string binding_package;
    std::uint64_t delta_cpu_time_ns = 0;
    std::uint64_t frequency_hz = 0;
    std::string binding_result;
    std::string power_source = "unavailable";
};

struct ThreadIdentity {
    int pid;
    int tid;
    std::uint64_t start_time_ticks;

    bool operator<(const ThreadIdentity& other) const {
        return std::tie(pid, tid, start_time_ticks) <
               std::tie(other.pid, other.tid, other.start_time_ticks);
    }

    bool operator==(const ThreadIdentity& other) const {
        return pid == other.pid && tid == other.tid && start_time_ticks == other.start_time_ticks;
    }
};

inline ThreadIdentity IdentityOf(const ThreadInfo& thread) {
    return {thread.pid, thread.tid, thread.start_time_ticks};
}

}  // namespace thread_sched
