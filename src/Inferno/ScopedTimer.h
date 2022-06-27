#pragma once

#include <chrono>

namespace Inferno {
    class ScopedTimer {
        using SteadyClock = std::chrono::steady_clock;
        const char* _name = nullptr;
        std::chrono::time_point<SteadyClock> _begin;
        int64_t* _value;
    public:
        ScopedTimer(const char* name, int64_t* value) : _name(name), _value(value) {
            assert(value);
            _begin = SteadyClock::now();
        }

        ScopedTimer(int64_t* value) : _value(value) {
            assert(value);
            _begin = SteadyClock::now();
        }

        ~ScopedTimer() 	{
            auto end = SteadyClock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(end - _begin).count();
            *_value += elapsedTime;
        }

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer(ScopedTimer&&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;
        ScopedTimer& operator=(ScopedTimer&&) = delete;
    };
}