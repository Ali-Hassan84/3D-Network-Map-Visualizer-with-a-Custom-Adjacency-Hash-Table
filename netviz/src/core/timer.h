// timer.h - monotonic nanosecond clock used for the live metrics and benchmarks.
#pragma once
#include <chrono>

inline double NowNs() {
    using namespace std::chrono;
    return (double)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
