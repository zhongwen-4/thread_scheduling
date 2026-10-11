#include "proc_reader.h"

#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <utility>
#include <unistd.h>

namespace thread_sched {
namespace {

bool IsNumber(const char* value) {
    if (!value || !*value) return false;
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

std::string ProcessName(const std::string& cmdline) {
    return cmdline.substr(0, cmdline.find(':'));
}

bool Matches(const ThreadInfo& thread, const Rule& rule) {
    if (!rule.package_name.empty() && thread.package_name != rule.package_name) return false;
    return rule.thread_name == "*" || thread.thread_name == rule.thread_name;
}

}  // namespace

ProcReader::ProcReader(std::string proc_root) : proc_root_(std::move(proc_root)) {}

std::string ProcReader::ReadTrimmed(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::size_t nul = value.find('\0');
    if (nul != std::string::npos) value.resize(nul);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
    return value;
}

bool ProcReader::ReadThread(int pid, int tid, ThreadInfo* output) const {
    const std::string process = proc_root_ + "/" + std::to_string(pid);
    const std::string base = process + "/task/" + std::to_string(tid);
    std::ifstream stat(base + "/stat");
    if (!stat) return false;
    std::string line;
    std::getline(stat, line);
    const std::size_t close = line.rfind(')');
    if (close == std::string::npos) return false;
    std::istringstream fields(line.substr(close + 2));
    char state = 0;
    std::uint64_t utime = 0, stime = 0, start_time = 0;
    int processor = -1;
    fields >> state;
    std::string ignored;
    for (int index = 4; index <= 39; ++index) {
        if (index == 14) fields >> utime;
        else if (index == 15) fields >> stime;
        else if (index == 22) fields >> start_time;
        else if (index == 39) fields >> processor;
        else fields >> ignored;
        if (!fields) return false;
    }
    output->pid = pid;
    output->tid = tid;
    output->thread_name = ReadTrimmed(base + "/comm");
    output->process_name = ReadTrimmed(process + "/cmdline");
    output->package_name = ProcessName(output->process_name);
    output->utime_ticks = utime;
    output->stime_ticks = stime;
    output->start_time_ticks = start_time;
    output->processor = processor;
    std::ifstream status(base + "/status");
    while (std::getline(status, line)) {
        if (line.rfind("Uid:", 0) == 0) {
            std::istringstream(line.substr(4)) >> output->uid;
            break;
        }
    }
    return true;
}

bool ProcReader::ReadThreadById(int pid, int tid, ThreadInfo* output) const {
    return ReadThread(pid, tid, output);
}

std::vector<int> ProcReader::FindPids(const Rule& rule) const {
    std::vector<int> pids;
    DIR* dir = opendir(proc_root_.c_str());
    if (!dir) return pids;
    while (dirent* entry = readdir(dir)) {
        if (!IsNumber(entry->d_name)) continue;
        const int pid = std::atoi(entry->d_name);
        ThreadInfo main_thread;
        if (ReadThread(pid, pid, &main_thread) && main_thread.package_name == rule.package_name) pids.push_back(pid);
    }
    closedir(dir);
    return pids;
}

std::vector<ThreadInfo> ProcReader::ReadThreads(const Rule& rule) const {
    std::vector<ThreadInfo> result;
    for (const int pid : FindPids(rule)) {
        const std::string path = proc_root_ + "/" + std::to_string(pid) + "/task";
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
