#pragma once

#include "model.h"

namespace thread_sched {

enum class BindingTrigger {
    kStartup,
    kSample,
    kEvent,
    kFallback,
};

inline bool ShouldBind(BindingMode mode, BindingTrigger trigger) {
    switch (mode) {
        case BindingMode::kScan:
            return trigger == BindingTrigger::kSample;
        case BindingMode::kSingle:
            return trigger == BindingTrigger::kStartup;
        case BindingMode::kEventDriven:
            return trigger == BindingTrigger::kStartup ||
                   trigger == BindingTrigger::kEvent ||
                   trigger == BindingTrigger::kFallback;
    }
    return false;
}

}  // namespace thread_sched
