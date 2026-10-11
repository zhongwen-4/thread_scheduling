#include "config.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <regex>
#include <sched.h>
#include <stdexcept>

namespace thread_sched {
namespace {

using json = nlohmann::json;

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int ParseCpuIndex(const std::string& text) {
    try {
        const long long cpu = std::stoll(text);
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            throw std::runtime_error("CPU index exceeds CPU_SETSIZE: " + text);
        }
        return static_cast<int>(cpu);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("invalid CPU index: " + text);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("CPU index is too large: " + text);
    }
}

CpuSelector ParseSelector(const json& value) {
    CpuSelector selector;
    if (value.is_number_integer()) {
        const int cpu = ParseCpuIndex(value.dump());
        selector.kind = CpuSelectorKind::kCpu;
        selector.first = selector.last = cpu;
        return selector;
    }
    if (!value.is_string()) {
        throw std::runtime_error("cpu_cores entries must be integer CPU IDs or strings");
    }

    const std::string text = Trim(value.get<std::string>());
    if (text == "all") {
        selector.kind = CpuSelectorKind::kAll;
        return selector;
    }
    if (text == "e-core" || text == "p-core" || text == "hp-core") {
        selector.kind = CpuSelectorKind::kGroup;
        selector.group = text;
        return selector;
    }

    if (std::regex_match(text, std::regex("^[0-9]+$"))) {
        const int cpu = ParseCpuIndex(text);
        selector.kind = CpuSelectorKind::kCpu;
        selector.first = selector.last = cpu;
        return selector;
    }

    static const std::regex range_pattern("^([0-9]+)\\s*-\\s*([0-9]+)$");
    std::smatch match;
    if (std::regex_match(text, match, range_pattern)) {
        selector.first = ParseCpuIndex(match[1].str());
        selector.last = ParseCpuIndex(match[2].str());
        if (selector.first > selector.last) {
            throw std::runtime_error("CPU range start must be <= range end: " + text);
        }
        selector.kind = selector.first == selector.last ? CpuSelectorKind::kCpu : CpuSelectorKind::kRange;
        return selector;
    }

    throw std::runtime_error("invalid cpu_cores selector: " + text);
}

BindingMode ParseMode(const json& value) {
    if (!value.is_string()) throw std::runtime_error("mode must be a string");
    const std::string mode = value.get<std::string>();
    if (mode == "scan") return BindingMode::kScan;
    if (mode == "single") return BindingMode::kSingle;
    if (mode == "event_driven") return BindingMode::kEventDriven;
    throw std::runtime_error("mode must be scan, single, or event_driven");
}

Rule ParseRule(const std::string& package_name, const json& value) {
    if (!value.is_object()) throw std::runtime_error("thread rule must be an object");
    static const std::regex package_pattern("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+$");
    if (!std::regex_match(package_name, package_pattern)) {
        throw std::runtime_error("invalid Android package name: " + package_name);
    }

    Rule rule;
    rule.package_name = package_name;
    if (!value.contains("thread_name") || !value["thread_name"].is_string()) {
        throw std::runtime_error("thread_name must be a string");
    }
    rule.thread_name = value["thread_name"].get<std::string>();
    if (rule.thread_name.empty()) throw std::runtime_error("thread_name must not be empty");

    if (!value.contains("cpu_cores") || !value["cpu_cores"].is_array() || value["cpu_cores"].empty()) {
        throw std::runtime_error("cpu_cores must be a non-empty array");
    }
    for (const auto& selector : value["cpu_cores"]) rule.cpu_selectors.push_back(ParseSelector(selector));

    const char* interval_key = value.contains("sample_interval_ms") ? "sample_interval_ms" :
                               value.contains("sample_interval") ? "sample_interval" : nullptr;
    if (!interval_key || !value[interval_key].is_number_integer()) {
        throw std::runtime_error("sample_interval_ms must be an integer");
    }
    rule.sample_interval_ms = value[interval_key].get<int>();
    if (rule.sample_interval_ms < 50 || rule.sample_interval_ms > 60000) {
        throw std::runtime_error("sample_interval_ms must be between 50 and 60000");
    }

    if (value.contains("mode")) rule.mode = ParseMode(value["mode"]);
    return rule;
}

}  // namespace

Config Config::Load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open applist: " + path);

    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string("invalid applist JSON: ") + error.what());
    }
    if (!root.is_object() || root.empty()) {
        throw std::runtime_error("applist root must be a non-empty object keyed by package name");
    }

    Config config;
    for (auto app = root.begin(); app != root.end(); ++app) {
        const std::string& package_name = app.key();
        if (app.value().is_object()) {
            config.rules_.push_back(ParseRule(package_name, app.value()));
        } else if (app.value().is_array() && !app.value().empty()) {
            for (const auto& rule : app.value()) config.rules_.push_back(ParseRule(package_name, rule));
        } else {
            throw std::runtime_error("each package must map to a rule object or non-empty rule array: " + package_name);
        }
    }
    return config;
}

}  // namespace thread_sched
