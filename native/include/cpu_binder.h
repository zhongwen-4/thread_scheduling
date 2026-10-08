#pragma once

#include "model.h"

#include <string>

namespace thread_sched {

class CpuBinder {
public:
    static bool Bind(int tid, const std::vector<int>& cpus, std::string* error);
};

}  // namespace thread_sched
