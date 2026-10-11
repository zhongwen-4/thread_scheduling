#include "config.h"
#include "binding_policy.h"
#include "core_topology.h"
#include "cpu_binder.h"
#include "event_watcher.h"
#include "proc_reader.h"
#include "sampler.h"

#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct ResolvedRule {
    thread_sched::Rule rule;
    std::vector<int> cpus;
    Clock::time_point next_sample;
    Clock::time_point next_fallback;
    std::set<thread_sched::ThreadIdentity> single_threads;
    std::map<thread_sched::ThreadIdentity, std::string> binding_results;
};

const char* ModeName(thread_sched::BindingMode mode) {
    switch (mode) {
        case thread_sched::BindingMode::kScan: return "scan";
        case thread_sched::BindingMode::kSingle: return "single";
        case thread_sched::BindingMode::kEventDriven: return "event_driven";
    }
    return "unknown";
}

void BindThreads(ResolvedRule* resolved, const std::vector<thread_sched::ThreadInfo>& threads) {
    for (const auto& thread : threads) {
        std::string error;
        const bool bound = thread_sched::CpuBinder::Bind(thread.tid, resolved->cpus, &error);
        resolved->binding_results[thread_sched::IdentityOf(thread)] = bound ? "ok" : "error:" + error;
        if (!bound) {
            std::cerr << "threadschedd: bind " << resolved->rule.package_name << "/"
                      << thread.tid << " failed: " << error << '\n';
        }
    }
}

void RecordThreads(const ResolvedRule& resolved, const std::vector<thread_sched::ThreadInfo>& threads,
                   const std::string& output, thread_sched::Sampler* sampler) {
    for (const auto& thread : threads) {
        const auto binding = resolved.binding_results.find(thread_sched::IdentityOf(thread));
        const std::string result = binding == resolved.binding_results.end() ? "not_requested" : binding->second;
        thread_sched::Sampler::AppendJsonLine(
            output, sampler->BuildSample(thread, resolved.rule.package_name, result));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: threadschedd <applist.json> <samples.jsonl>\n";
        return 2;
    }
    try {
        const thread_sched::Config config = thread_sched::Config::Load(argv[1]);
        thread_sched::CoreTopology topology;
        std::vector<ResolvedRule> rules;
        rules.reserve(config.rules().size());
        for (const auto& rule : config.rules()) {
            ResolvedRule resolved{rule, {}, Clock::now(), Clock::now() + std::chrono::seconds(5), {}, {}};
            std::string error;
            if (!topology.Resolve(rule.cpu_selectors, &resolved.cpus, &error)) {
                throw std::runtime_error(rule.package_name + ": " + error);
            }
            std::cerr << "threadschedd: " << rule.package_name << " mode=" << ModeName(rule.mode)
                      << " sample_interval_ms=" << rule.sample_interval_ms << "\n";
            rules.push_back(std::move(resolved));
        }

        const long ticks = sysconf(_SC_CLK_TCK);
        thread_sched::ProcReader reader;
        thread_sched::Sampler sampler(ticks);
        thread_sched::EventWatcher watcher;
        bool event_available = false;
        for (const auto& resolved : rules) {
            if (resolved.rule.mode == thread_sched::BindingMode::kEventDriven) {
                event_available = true;
                break;
            }
        }
        if (event_available) {
            std::string error;
            event_available = watcher.Start(&error);
            if (!event_available) {
                std::cerr << "threadschedd: event_driven unavailable, using 5s scan fallback: " << error << '\n';
            }
        }

        // The single mode binds a startup snapshot; event mode starts with a discovery scan.
        for (auto& resolved : rules) {
            if (!thread_sched::ShouldBind(resolved.rule.mode, thread_sched::BindingTrigger::kStartup)) continue;
            const auto threads = reader.ReadThreads(resolved.rule);
            if (resolved.rule.mode == thread_sched::BindingMode::kSingle) {
                for (const auto& thread : threads) resolved.single_threads.insert(thread_sched::IdentityOf(thread));
            }
            BindThreads(&resolved, threads);
            RecordThreads(resolved, threads, argv[2], &sampler);
        }

        bool event_scan_due = false;
        while (true) {
            int pid = -1;
            std::string event_error;
            if (event_available && watcher.WaitForEvent(100, &pid, &event_error)) {
                event_scan_due = true;
            } else if (event_available && !event_error.empty()) {
                std::cerr << "threadschedd: event listener stopped, using 5s scan fallback: " << event_error << '\n';
                event_available = false;
            } else if (!event_available) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            const auto now = Clock::now();
            for (auto& resolved : rules) {
                const bool sample_due = now >= resolved.next_sample;
                const bool fallback_due = now >= resolved.next_fallback;
                const bool event_bind_due = resolved.rule.mode == thread_sched::BindingMode::kEventDriven &&
                                            (event_scan_due || fallback_due);
                if (!sample_due && !event_bind_due) continue;

                std::vector<thread_sched::ThreadInfo> threads;
                if (resolved.rule.mode == thread_sched::BindingMode::kSingle) {
                    for (auto identity_it = resolved.single_threads.begin(); identity_it != resolved.single_threads.end();) {
                        const auto identity = *identity_it;
                        thread_sched::ThreadInfo thread;
                        if (reader.ReadThreadById(identity.pid, identity.tid, &thread) &&
                            thread_sched::IdentityOf(thread) == identity) {
                            threads.push_back(thread);
                            ++identity_it;
                        } else {
                            identity_it = resolved.single_threads.erase(identity_it);
                        }
                    }
                } else {
                    threads = reader.ReadThreads(resolved.rule);
                    const bool bind_on_sample = sample_due && thread_sched::ShouldBind(
                        resolved.rule.mode, thread_sched::BindingTrigger::kSample);
                    const bool bind_on_event = event_scan_due && thread_sched::ShouldBind(
                        resolved.rule.mode, thread_sched::BindingTrigger::kEvent);
                    const bool bind_on_fallback = fallback_due && thread_sched::ShouldBind(
                        resolved.rule.mode, thread_sched::BindingTrigger::kFallback);
                    if (bind_on_sample || bind_on_event || bind_on_fallback) BindThreads(&resolved, threads);
                }
                if (sample_due) RecordThreads(resolved, threads, argv[2], &sampler);
                if (sample_due) resolved.next_sample = now + std::chrono::milliseconds(resolved.rule.sample_interval_ms);
                if (fallback_due) resolved.next_fallback = now + std::chrono::seconds(5);
            }
            event_scan_due = false;
            (void)pid;
        }
    } catch (const std::exception& error) {
        std::cerr << "threadschedd: " << error.what() << '\n';
        return 1;
    }
}
