#pragma once
#include "core/measurement.hpp"
#include "core/timing.hpp"
#include <array>

struct SensorConfig {
    int intervalMs = 10;
    int window = 100;
    int noisePercent = 10;
    int spikeEvery = 200;
    int missingEvery = 97;
    std::uint32_t seed = 42;
    std::array<double, 3> thresholds{32.0, 105.0, 0.65};
    std::array<double, 3> hysteresis{1.0, 1.0, 0.1};
    bool operator==(const SensorConfig&) const = default;
};
SensorConfig validated(SensorConfig config);

struct SensorInput {
    std::array<Reading<double>, 3> values;
    SampleTiming timing;
    std::uint64_t sequence{};
};
struct SensorValue {
    Reading<double> raw, average, minimum, maximum;
    std::size_t validCount{};
    bool alert{};
    std::uint64_t transitions{};
};
struct SensorFrame {
    std::array<SensorValue, 3> values;
    SampleTiming timing;
    std::uint64_t sequence{};
};

class SensorSimulator {
public:
    void reset(SensorConfig config);
    SensorInput sample(SampleTiming timing);
private:
    SensorConfig config_;
    std::uint32_t random_ = 42;
    std::uint64_t sequence_ = 0;
    double noise();
};
class SensorProcessor {
public:
    void reset(SensorConfig config);
    SensorFrame process(const SensorInput& input);
private:
    SensorConfig config_;
    std::array<std::array<Reading<double>, 256>, 3> history_{};
    std::array<bool, 3> alerts_{};
    std::array<std::uint64_t, 3> transitions_{};
    std::size_t cursor_{}, count_{};
};
