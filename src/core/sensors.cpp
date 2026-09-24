#include "core/sensors.hpp"
#include <algorithm>
#include <cmath>

SensorConfig validated(SensorConfig config) {
    config.intervalMs = std::clamp(config.intervalMs, 1, 1000);
    config.window = std::clamp(config.window, 1, 256);
    config.noisePercent = std::clamp(config.noisePercent, 0, 100);
    config.spikeEvery = std::clamp(config.spikeEvery, 0, 1000000);
    config.missingEvery = std::clamp(config.missingEvery, 0, 1000000);
    if (!config.seed) config.seed = 42;
    const SensorConfig defaults;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(config.thresholds[i])) config.thresholds[i] = defaults.thresholds[i];
        if (!std::isfinite(config.hysteresis[i])) config.hysteresis[i] = defaults.hysteresis[i];
        config.hysteresis[i] = std::clamp(config.hysteresis[i], 0.0, 1000000.0);
    }
    return config;
}
void SensorSimulator::reset(SensorConfig config) {
    config_ = validated(config); random_ = config_.seed; sequence_ = 0;
}
double SensorSimulator::noise() {
    random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
    return (static_cast<double>(random_) / 4294967295.0 * 2 - 1) * config_.noisePercent / 100.0;
}
SensorInput SensorSimulator::sample(SampleTiming timing) {
    SensorInput result;
    result.sequence = ++sequence_; result.timing = timing;
    const double time = static_cast<double>(sequence_) * config_.intervalMs / 1000.0;
    const bool spike = config_.spikeEvery > 0 && sequence_ % config_.spikeEvery == 0;
    const std::array<double, 3> values{
        25 + 5 * std::sin(time * 0.3) + noise() * 5 + (spike ? 12 : 0),
        101.3 + 2 * std::sin(time * 0.8) + noise() * 2 + (spike ? 10 : 0),
        std::max(0.0, 0.3 + 0.1 * std::sin(time * 7) + noise() * 0.2 + (spike ? 0.8 : 0))};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const bool missing = config_.missingEvery > 0 && (sequence_ + i) % config_.missingEvery == 0;
        if (!missing) result.values[i] = Reading<double>::ready(values[i]);
    }
    return result;
}
void SensorProcessor::reset(SensorConfig config) {
    config_ = validated(config); history_ = {}; alerts_ = {}; transitions_ = {}; cursor_ = count_ = 0;
}
SensorFrame SensorProcessor::process(const SensorInput& input) {
    SensorFrame result;
    result.timing = input.timing; result.sequence = input.sequence;
    count_ = std::min(count_ + 1, static_cast<std::size_t>(config_.window));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        auto value = input.values[channel];
        if (value.valid() && !std::isfinite(value.value)) value = {};
        history_[channel][cursor_] = value;
        auto& output = result.values[channel];
        output.raw = value;
        double sum = 0, minimum = 0, maximum = 0;
        for (std::size_t i = 0; i < count_; ++i) if (history_[channel][i].valid()) {
            const double item = history_[channel][i].value;
            if (output.validCount == 0) minimum = maximum = item;
            minimum = std::min(minimum, item); maximum = std::max(maximum, item);
            sum += item; ++output.validCount;
        }
        if (output.validCount) {
            output.average = Reading<double>::ready(sum / static_cast<double>(output.validCount));
            output.minimum = Reading<double>::ready(minimum); output.maximum = Reading<double>::ready(maximum);
        }
        const bool previous = alerts_[channel];
        if (value.valid()) {
            if (!previous && value.value >= config_.thresholds[channel]) alerts_[channel] = true;
            else if (previous && value.value <= config_.thresholds[channel] - config_.hysteresis[channel]) alerts_[channel] = false;
        }
        if (previous != alerts_[channel]) ++transitions_[channel];
        output.alert = alerts_[channel]; output.transitions = transitions_[channel];
    }
    cursor_ = (cursor_ + 1) % static_cast<std::size_t>(config_.window);
    return result;
}
