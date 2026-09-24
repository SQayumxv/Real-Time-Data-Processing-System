#pragma once
#include "core/measurement.hpp"
struct MemoryUsage { std::uint64_t totalBytes{}, availableBytes{}; };
Reading<MemoryUsage> getMemoryUsage();
