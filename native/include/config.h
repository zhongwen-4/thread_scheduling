#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace thread_sched {

class Config {
public:
    static Config Load(const std::string& path);
    const std::vector<Rule>& rules() const { return rules_; }

private:
    std::vector<Rule> rules_;
};

}  // namespace thread_sched
