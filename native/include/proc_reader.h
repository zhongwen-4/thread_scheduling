#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace thread_sched {

class ProcReader {
public:
    explicit ProcReader(std::string proc_root = "/proc");
    std::vector<ThreadInfo> ReadThreads(const Rule& rule) const;
    bool ReadThreadById(int pid, int tid, ThreadInfo* output) const;
    static std::string ReadTrimmed(const std::string& path);

private:
    std::string proc_root_;
    std::vector<int> FindPids(const Rule& rule) const;
    bool ReadThread(int pid, int tid, ThreadInfo* output) const;
};

}  // namespace thread_sched
