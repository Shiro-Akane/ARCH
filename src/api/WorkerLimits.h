#pragma once
#include "ApplicationContract.h"
#include <cstdint>
namespace arch::api {
void ApplyInspectionProcessLimits(int cpu_seconds = contract::worker_cpu_seconds);
class SessionProcessLimits {
    std::uint64_t inherited_cpu_ceiling_ = 0;
public:
    SessionProcessLimits();
    void begin_request(int cpu_seconds);
};
}
