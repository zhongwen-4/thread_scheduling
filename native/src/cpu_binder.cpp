#include "cpu_binder.h"

#include <cerrno>
#include <cstring>
#include <sched.h>

namespace thread_sched {

bool CpuBinder::Bind(int tid, const std::vector<int>& cpus, std::string* error) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (const int cpu : cpus) {
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            if (error) *error = "cpu index out of range";
            return false;
        }
        CPU_SET(cpu, &mask);
    }
    if (sched_setaffinity(tid, sizeof(mask), &mask) != 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    return true;
}

}  // namespace thread_sched
