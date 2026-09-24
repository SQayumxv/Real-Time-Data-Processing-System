#pragma once
#include <cstdint>
#include <optional>
#include <string>

enum class ReadingState { ready, collecting, unavailable, accessDenied };
template<class T> struct Reading {
    T value{};
    ReadingState state = ReadingState::unavailable;
    bool valid() const { return state == ReadingState::ready; }
    static Reading ready(T value) { return {value, ReadingState::ready}; }
};
inline std::wstring stateText(ReadingState state) {
    switch (state) {
    case ReadingState::collecting: return L"Collecting...";
    case ReadingState::accessDenied: return L"Access denied";
    case ReadingState::unavailable: return L"Unavailable";
    default: return L"Ready";
    }
}
struct CpuCounters { std::uint64_t idle{}, kernel{}, user{}; };
class CpuSampler {
public:
    Reading<double> sample(std::optional<CpuCounters> counters);
    void reset() { previous_.reset(); }
private:
    std::optional<CpuCounters> previous_;
};
class ProcessCpuSampler {
public:
    Reading<double> sample(std::uint64_t creation, std::uint64_t ticks,
                           double seconds, unsigned processors);
private:
    std::optional<std::uint64_t> creation_, ticks_;
    double previousSeconds_{};
};
