#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace thread_sched {

class CoreTopology {
public:
    explicit CoreTopology(std::string cpu_root = "/sys/devices/system/cpu");
    bool Resolve(const std::vector<CpuSelector>& selectors,
                 std::vector<int>* cpus, std::string* error) const;
    const std::string& group_source() const { return group_source_; }

private:
    std::string cpu_root_;
    mutable std::string group_source_;
    bool DetectGroups(const std::vector<int>& present, std::vector<int>* efficiency, std::vector<int>* middle,
                      std::vector<int>* performance, std::string* error) const;
};

}  // namespace thread_sched
