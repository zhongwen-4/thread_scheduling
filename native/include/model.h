#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thread_sched {

struct Rule {
    std::string package_name;
    int uid = -1;
    std::string process_name;
    std::string thread_name;
    std::vector<int> cpus;
    bool enforce = true;
    int sample_ms = 1000;
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
    int processor = -1;
};

struct Sample {
    std::int64_t timestamp_ms = 0;
    ThreadInfo thread;
    std::uint64_t delta_cpu_time_ns = 0;
    std::uint64_t frequency_hz = 0;
    std::string binding_result;
    std::string power_source = "estimated";
    std::uint64_t power_estimate_uw = 0;
};

}  // namespace thread_sched
