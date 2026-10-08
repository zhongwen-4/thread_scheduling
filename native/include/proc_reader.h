#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace thread_sched {

class ProcReader {
public:
    explicit ProcReader(long clock_ticks_per_second);
    std::vector<ThreadInfo> ReadThreads(const Rule& rule) const;
    static std::string ReadTrimmed(const std::string& path);

private:
    long clock_ticks_per_second_;
    std::vector<int> FindPids(const Rule& rule) const;
    bool ReadThread(int pid, int tid, ThreadInfo* output) const;
};

}  // namespace thread_sched
