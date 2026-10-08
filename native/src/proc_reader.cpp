#include "proc_reader.h"

#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace thread_sched {

namespace {

bool IsNumber(const char* value) {
    if (!value || !*value) return false;
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

bool Matches(const ThreadInfo& thread, const Rule& rule) {
    if (rule.uid >= 0 && thread.uid != rule.uid) return false;
    if (!rule.package_name.empty() && thread.package_name != rule.package_name) return false;
    if (!rule.process_name.empty() && thread.process_name != rule.process_name) return false;
    if (!rule.thread_name.empty() && thread.thread_name != rule.thread_name) return false;
    return true;
}

}  // namespace

ProcReader::ProcReader(long clock_ticks_per_second) : clock_ticks_per_second_(clock_ticks_per_second) {}

std::string ProcReader::ReadTrimmed(const std::string& path) {
    std::ifstream input(path);
    if (!input) return {};
    std::string value;
    std::getline(input, value);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
    return value;
}

std::vector<int> ProcReader::FindPids(const Rule& rule) const {
    std::vector<int> pids;
    DIR* dir = opendir("/proc");
    if (!dir) return pids;
    while (dirent* entry = readdir(dir)) {
        if (!IsNumber(entry->d_name)) continue;
        const int pid = std::atoi(entry->d_name);
        ThreadInfo main_thread;
        if (ReadThread(pid, pid, &main_thread) && Matches(main_thread, rule)) pids.push_back(pid);
    }
    closedir(dir);
    return pids;
}

bool ProcReader::ReadThread(int pid, int tid, ThreadInfo* output) const {
    const std::string base = "/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid);
    std::ifstream stat(base + "/stat");
    if (!stat) return false;
    std::string line;
    std::getline(stat, line);
    const std::size_t close = line.rfind(')');
    if (close == std::string::npos) return false;
    std::istringstream fields(line.substr(close + 2));
    char state = 0;
    std::uint64_t utime = 0, stime = 0;
    int processor = -1;
    fields >> state;
    std::string ignored;
    for (int index = 4; index <= 39; ++index) {
        if (index == 14) fields >> utime;
        else if (index == 15) fields >> stime;
        else if (index == 39) fields >> processor;
        else fields >> ignored;
        if (!fields) return false;
    }
    output->pid = pid;
    output->tid = tid;
    output->thread_name = ReadTrimmed(base + "/comm");
    output->process_name = ReadTrimmed("/proc/" + std::to_string(pid) + "/cmdline");
    output->package_name = output->process_name;
    output->utime_ticks = utime;
    output->stime_ticks = stime;
    output->processor = processor;
    return true;
}

std::vector<ThreadInfo> ProcReader::ReadThreads(const Rule& rule) const {
    std::vector<ThreadInfo> result;
    for (const int pid : FindPids(rule)) {
        const std::string path = "/proc/" + std::to_string(pid) + "/task";
        DIR* dir = opendir(path.c_str());
        if (!dir) continue;
        while (dirent* entry = readdir(dir)) {
            if (!IsNumber(entry->d_name)) continue;
            ThreadInfo thread;
            if (ReadThread(pid, std::atoi(entry->d_name), &thread) && Matches(thread, rule)) result.push_back(thread);
        }
        closedir(dir);
    }
    return result;
}

}  // namespace thread_sched
