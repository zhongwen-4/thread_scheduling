#pragma once

#include "model.h"

#include <cstdint>
#include <map>
#include <string>

namespace thread_sched {

class Sampler {
public:
    explicit Sampler(long clock_ticks_per_second);
    Sample BuildSample(const ThreadInfo& thread, const std::string& package,
                       const std::string& binding_result);
    static void AppendJsonLine(const std::string& path, const Sample& sample);

private:
    long clock_ticks_per_second_;
    std::map<ThreadIdentity, std::uint64_t> previous_cpu_ticks_;
};

}  // namespace thread_sched
