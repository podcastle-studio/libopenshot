// Benchmark scenarios: representative production-style timelines, built with the same recipes the
// golden suite uses (so they drive the library the way video-rendering-service does).
#pragma once

#include "Harness.h"

#include <functional>
#include <string>
#include <vector>

namespace bench {

struct BenchScenario {
    std::string name;
    std::string description;
    std::function<void(golden::Scene&)> build;   // Scene has width/height/fps/mediaDir set; must call Open()
};

// Directory with the large bench-only media (tests/bench/media).
void setBenchMediaDir(const std::string& dir);
const std::vector<BenchScenario>& scenarios();

} // namespace bench
