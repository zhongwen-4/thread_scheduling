#include "config.h"
#include "cpu_binder.h"
#include "proc_reader.h"
#include "sampler.h"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: threadschedd <config.json> <samples.jsonl>\n";
        return 2;
    }
    try {
        const thread_sched::Config config = thread_sched::Config::Load(argv[1]);
        const long ticks = sysconf(_SC_CLK_TCK);
        thread_sched::ProcReader reader(ticks);
        thread_sched::Sampler sampler(ticks);
        while (true) {
            int sleep_ms = 1000;
            for (const thread_sched::Rule& rule : config.rules()) {
                sleep_ms = std::min(sleep_ms, rule.sample_ms);
                for (const thread_sched::ThreadInfo& thread : reader.ReadThreads(rule)) {
                    std::string error;
                    const bool bound = thread_sched::CpuBinder::Bind(thread.tid, rule.cpus, &error);
                    const std::string result = bound ? "ok" : "error:" + error;
                    thread_sched::Sampler::AppendJsonLine(argv[2], sampler.BuildSample(thread, result));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(50, sleep_ms)));
        }
    } catch (const std::exception& error) {
        std::cerr << "threadschedd: " << error.what() << '\n';
        return 1;
    }
}
