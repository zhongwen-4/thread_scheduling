#include "core_topology.h"

#include "proc_reader.h"

#include <algorithm>
#include <dirent.h>
#include <map>
#include <sched.h>
#include <iterator>
#include <utility>

namespace thread_sched {
namespace {

std::vector<int> OnlineCpus(const std::string& root) {
    std::vector<int> cpus;
    DIR* dir = opendir(root.c_str());
    if (!dir) return cpus;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("cpu", 0) != 0 || name.size() == 3) continue;
        try {
            const int cpu = std::stoi(name.substr(3));
            const std::string online = ProcReader::ReadTrimmed(root + "/" + name + "/online");
            if (cpu == 0 || online.empty() || online == "1") cpus.push_back(cpu);
        } catch (...) {
        }
    }
    closedir(dir);
    std::sort(cpus.begin(), cpus.end());
    return cpus;
}

bool ReadCapacity(const std::string& root, int cpu, const std::string& source, long* capacity) {
    const std::string base = root + "/cpu" + std::to_string(cpu);
    const std::string path = source == "cpu_capacity"
        ? base + "/cpu_capacity" : base + "/cpufreq/cpuinfo_max_freq";
    const std::string value = ProcReader::ReadTrimmed(path);
    if (value.empty()) return false;
    try {
        *capacity = std::stol(value);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

CoreTopology::CoreTopology(std::string cpu_root) : cpu_root_(std::move(cpu_root)) {}

bool CoreTopology::DetectGroups(const std::vector<int>& present, std::vector<int>* efficiency,
                                std::vector<int>* middle, std::vector<int>* performance,
                                std::string* error) const {
    std::map<long, std::vector<int>> groups;
    std::string source;
    for (const std::string& candidate : {"cpu_capacity", "cpuinfo_max_freq"}) {
        std::map<long, std::vector<int>> candidate_groups;
        bool complete = true;
        for (const int cpu : present) {
            long capacity = 0;
            if (!ReadCapacity(cpu_root_, cpu, candidate, &capacity)) {
                complete = false;
                break;
            }
            candidate_groups[capacity].push_back(cpu);
        }
        if (complete && !candidate_groups.empty()) {
            groups = std::move(candidate_groups);
            source = candidate;
            break;
        }
    }
    if (groups.empty()) {
        if (error) *error = "neither cpu_capacity nor per-core cpuinfo_max_freq is available for all online CPUs";
        return false;
    }
    if (groups.empty()) {
        if (error) *error = "CPU capacity topology contains no groups";
        return false;
    }

    group_source_ = source;
    efficiency->clear();
    middle->clear();
    performance->clear();
    auto first = groups.begin();
    efficiency->insert(efficiency->end(), first->second.begin(), first->second.end());
    if (groups.size() > 2) {
        auto current = std::next(first);
        for (; std::next(current) != groups.end(); ++current) {
            middle->insert(middle->end(), current->second.begin(), current->second.end());
        }
    }
    const auto& fastest = groups.rbegin()->second;
    performance->insert(performance->end(), fastest.begin(), fastest.end());
    return true;
}

bool CoreTopology::Resolve(const std::vector<CpuSelector>& selectors,
                           std::vector<int>* cpus, std::string* error) const {
    cpus->clear();
    const std::vector<int> present = OnlineCpus(cpu_root_);
    if (present.empty()) {
        if (error) *error = "no online CPUs found";
        return false;
    }
    std::vector<int> efficiency, middle, performance;
    bool groups_loaded = false;
    for (const CpuSelector& selector : selectors) {
        if (selector.kind == CpuSelectorKind::kAll) {
            cpus->insert(cpus->end(), present.begin(), present.end());
            continue;
        }
        if (selector.kind == CpuSelectorKind::kGroup) {
            if (!groups_loaded) {
                if (!DetectGroups(present, &efficiency, &middle, &performance, error)) return false;
                groups_loaded = true;
            }
            const std::vector<int>* selected = nullptr;
            if (selector.group == "e-core") selected = &efficiency;
            else if (selector.group == "p-core") selected = &middle;
            else if (selector.group == "hp-core") selected = &performance;
            if (!selected || selected->empty()) {
                if (error) *error = "CPU group is unavailable: " + selector.group;
                return false;
            }
            cpus->insert(cpus->end(), selected->begin(), selected->end());
            continue;
        }
        if (selector.first < 0 || selector.last >= CPU_SETSIZE) {
            if (error) *error = "CPU selector exceeds CPU_SETSIZE";
            return false;
        }
        for (int cpu = selector.first; cpu <= selector.last; ++cpu) {
            if (!std::binary_search(present.begin(), present.end(), cpu)) {
                if (error) *error = "CPU is not online: " + std::to_string(cpu);
                return false;
            }
            cpus->push_back(cpu);
        }
    }
    std::sort(cpus->begin(), cpus->end());
    cpus->erase(std::unique(cpus->begin(), cpus->end()), cpus->end());
    if (cpus->empty()) {
        if (error) *error = "CPU selector resolved to an empty set";
        return false;
    }
    return true;
}

}  // namespace thread_sched
