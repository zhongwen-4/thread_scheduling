#include "sampler.h"

#include "proc_reader.h"

#include <chrono>
#include <fstream>
#include <iomanip>

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

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') escaped += '\\';
        escaped += ch;
    }
    return escaped;
}

}  // namespace

Sampler::Sampler(long clock_ticks_per_second) : clock_ticks_per_second_(clock_ticks_per_second) {}

Sample Sampler::BuildSample(const ThreadInfo& thread, const std::string& package,
                            const std::string& binding_result) {
    const std::uint64_t ticks = thread.utime_ticks + thread.stime_ticks;
    const ThreadIdentity identity = IdentityOf(thread);
    const auto previous = previous_cpu_ticks_.find(identity);
    const std::uint64_t previous_ticks = previous == previous_cpu_ticks_.end() ? ticks : previous->second;
    previous_cpu_ticks_[identity] = ticks;
    const std::uint64_t delta_ticks = ticks >= previous_ticks ? ticks - previous_ticks : 0;

    Sample sample;
    sample.timestamp_ms = NowMs();
    sample.thread = thread;
    sample.binding_package = package;
    sample.delta_cpu_time_ns = clock_ticks_per_second_ > 0
        ? delta_ticks * 1000000000ULL / static_cast<std::uint64_t>(clock_ticks_per_second_) : 0;
    sample.frequency_hz = ReadFrequency(thread.processor);
    sample.binding_result = binding_result;
    sample.power_source = "unavailable";
    return sample;
}

void Sampler::AppendJsonLine(const std::string& path, const Sample& sample) {
    std::ofstream output(path, std::ios::app);
    if (!output) return;
    output << std::fixed << std::setprecision(0)
           << "{\"timestamp_ms\":" << sample.timestamp_ms
           << ",\"package\":\"" << EscapeJson(sample.binding_package)
           << "\",\"uid\":" << sample.thread.uid
           << ",\"pid\":" << sample.thread.pid
           << ",\"tid\":" << sample.thread.tid
           << ",\"thread_name\":\"" << EscapeJson(sample.thread.thread_name)
           << "\",\"cpu\":" << sample.thread.processor
           << ",\"delta_cpu_time_ns\":" << sample.delta_cpu_time_ns
           << ",\"frequency_hz\":" << sample.frequency_hz
           << ",\"binding_result\":\"" << EscapeJson(sample.binding_result)
           << "\",\"power_source\":\"" << sample.power_source
           << "\",\"power_estimate_uw\":null}\n";
}

}  // namespace thread_sched
