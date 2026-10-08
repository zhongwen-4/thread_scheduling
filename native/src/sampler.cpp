#include "sampler.h"

#include "proc_reader.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace thread_sched {

namespace {

std::uint64_t ReadFrequency(int cpu) {
    if (cpu < 0) return 0;
    for (const std::string& file : {"/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq",
                                    "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/cpuinfo_cur_freq"}) {
        const std::string value = ProcReader::ReadTrimmed(file);
        if (!value.empty()) {
            try { return std::stoull(value) * 1000ULL; } catch (...) { return 0; }
        }
    }
    return 0;
}

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

Sampler::Sampler(long clock_ticks_per_second) : clock_ticks_per_second_(clock_ticks_per_second) {}

Sample Sampler::BuildSample(const ThreadInfo& thread, const std::string& binding_result) {
    const std::uint64_t ticks = thread.utime_ticks + thread.stime_ticks;
    const std::uint64_t previous = previous_cpu_ticks_[thread.tid];
    previous_cpu_ticks_[thread.tid] = ticks;
    const std::uint64_t delta_ticks = ticks >= previous ? ticks - previous : 0;
    Sample sample;
    sample.timestamp_ms = NowMs();
    sample.thread = thread;
    sample.delta_cpu_time_ns = clock_ticks_per_second_ > 0 ? delta_ticks * 1000000000ULL / static_cast<std::uint64_t>(clock_ticks_per_second_) : 0;
    sample.frequency_hz = ReadFrequency(thread.processor);
    sample.binding_result = binding_result;
    sample.power_source = "estimated";
    sample.power_estimate_uw = sample.frequency_hz == 0 ? 0 : sample.delta_cpu_time_ns * sample.frequency_hz / 1000000000ULL;
    return sample;
}

void Sampler::AppendJsonLine(const std::string& path, const Sample& sample) {
    std::ofstream output(path, std::ios::app);
    if (!output) return;
    output << std::fixed << std::setprecision(0)
           << "{\"timestamp_ms\":" << sample.timestamp_ms
           << ",\"pid\":" << sample.thread.pid
           << ",\"tid\":" << sample.thread.tid
           << ",\"thread_name\":\"" << sample.thread.thread_name
           << "\",\"cpu\":" << sample.thread.processor
           << ",\"delta_cpu_time_ns\":" << sample.delta_cpu_time_ns
           << ",\"frequency_hz\":" << sample.frequency_hz
           << ",\"binding_result\":\"" << sample.binding_result
           << "\",\"power_source\":\"" << sample.power_source
           << "\",\"power_estimate_uw\":" << sample.power_estimate_uw << "}\n";
}

}  // namespace thread_sched
