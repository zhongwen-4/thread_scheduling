#include "config.h"

#include <fstream>
#include <regex>
#include <stdexcept>

namespace thread_sched {

namespace {

std::string FindString(const std::string& object, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return {};
    return match[1].str();
}

int FindInt(const std::string& object, const std::string& key, int fallback) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return fallback;
    return std::stoi(match[1].str());
}

std::vector<int> FindCpus(const std::string& object) {
    const std::regex pattern("\\\"cpus\\\"\\s*:\\s*\\[([^]]*)\\]");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return {};
    std::vector<int> cpus;
    const std::regex number("[0-9]+");
    for (std::sregex_iterator it(match[1].first, match[1].second, number), end; it != end; ++it) {
        cpus.push_back(std::stoi(it->str()));
    }
    return cpus;
}

}  // namespace

Config Config::Load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open config: " + path);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    Config config;
    const std::regex object("\\{[^{}]*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), object), end; it != end; ++it) {
        const std::string value = it->str();
        Rule rule;
        rule.package_name = FindString(value, "package");
        rule.uid = FindInt(value, "uid", -1);
        rule.process_name = FindString(value, "process");
        rule.thread_name = FindString(value, "thread_name");
        rule.cpus = FindCpus(value);
        rule.sample_ms = FindInt(value, "sample_ms", 1000);
        rule.enforce = FindString(value, "mode") != "once";
        if (rule.cpus.empty()) throw std::runtime_error("rule has no CPUs");
        config.rules_.push_back(std::move(rule));
    }
    if (config.rules_.empty()) throw std::runtime_error("config contains no rules");
    return config;
}

}  // namespace thread_sched
