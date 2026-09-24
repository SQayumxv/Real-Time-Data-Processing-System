#include "core/measurement.hpp"
#include <algorithm>

Reading<double> CpuSampler::sample(std::optional<CpuCounters> counters) {
    if (!counters) { reset(); return {}; }
    const auto previous = previous_;
    previous_ = counters;
    if (!previous || counters->idle < previous->idle ||
        counters->kernel < previous->kernel || counters->user < previous->user)
        return {0, ReadingState::collecting};
    const auto idle = counters->idle - previous->idle;
    const auto kernel = counters->kernel - previous->kernel;
    const auto user = counters->user - previous->user;
    const double total = static_cast<double>(kernel) + static_cast<double>(user);
    if (total <= 0 || static_cast<double>(idle) > total) return {};
    return Reading<double>::ready(std::clamp(100.0 * (total - static_cast<double>(idle)) / total, 0.0, 100.0));
}
Reading<double> ProcessCpuSampler::sample(std::uint64_t creation, std::uint64_t ticks,
                                         double seconds, unsigned processors) {
    const auto previousTicks = ticks_;
    const auto previousCreation = creation_;
    const double interval = seconds - previousSeconds_;
    creation_ = creation;
    ticks_ = ticks;
    previousSeconds_ = seconds;
    if (!previousTicks || previousCreation != creation || ticks < *previousTicks)
        return {0, ReadingState::collecting};
    if (interval <= 0 || processors == 0) return {};
    const double cpuSeconds = static_cast<double>(ticks - *previousTicks) / 10000000.0;
    return Reading<double>::ready(std::clamp(cpuSeconds / interval / processors * 100.0, 0.0, 100.0));
}
