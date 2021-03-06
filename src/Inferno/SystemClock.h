#pragma once

#include <chrono>
#include <thread>
#include <assert.h>
#include <wtypes.h>
#include "timeapi.h"

// Adaptation of i_time.cpp from gzdoom
namespace Inferno {
    constexpr uint64_t TickToNS(double tick, double tickRate) {
        return static_cast<uint64_t>(tick * 1'000'000'000 / tickRate);
    }

    constexpr uint64_t NSToMS(uint64_t ns) {
        return static_cast<uint64_t>(ns / 1'000'000);
    }

    constexpr int NSToTick(uint64_t ns, double tickRate) {
        return static_cast<int>(ns * tickRate / 1'000'000'000);
    }

    class SystemClock {
        uint64_t _firstFrameStartTime = 0;
        uint64_t _currentFrameStartTime = 0, _prevFrameStartTime = 0;
        uint64_t _frameTime = 0;
        uint64_t _freezeTime = 0;
        double _lastinputtime = 0;
        UINT _timerPeriod = 1; // Assume minimum resolution of 1 ms
        int _prevTick = 0;

        int TickRate = 60; // Updates per second
    public:
        SystemClock() {
            // Set the Windows timer to be as accurate as possible
            TIMECAPS tc{};
            if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR)
                _timerPeriod = tc.wPeriodMin;

            timeBeginPeriod(_timerPeriod);
        }

        ~SystemClock() {
            timeEndPeriod(_timerPeriod);
        }

        SystemClock(const SystemClock&) = delete;
        SystemClock(SystemClock&&) = default;
        SystemClock& operator=(const SystemClock&) = delete;
        SystemClock& operator=(SystemClock&&) = default;

        // Freezes tick counting temporarily. While frozen, calls to GetClockTime()
        // will always return the same value.
        void Freeze(bool frozen) {
            if (frozen) {
                assert(_freezeTime == 0);
                _freezeTime = GetClockTimeNS();
            }
            else {
                assert(_freezeTime != 0);
                if (_firstFrameStartTime != 0) _firstFrameStartTime += GetClockTimeNS() - _freezeTime;
                _freezeTime = 0;
                UpdateFrameTime();
            }
        }


        float TimeScale = 1.0f;
        int Ticks = 0;

        // Reset the timer after a lengthy operation
        void ResetFrameTime() {
            auto ft = _currentFrameStartTime;
            UpdateFrameTime();
            _firstFrameStartTime += (_currentFrameStartTime - ft);
        }

        void Update(bool useTickRate) {
            if (useTickRate && !_freezeTime) {
                int tick = WaitForTick();
                Ticks = tick - _prevTick;
                _prevTick = tick;
            }
            else {
                UpdateFrameTime();
            }

            _frameTime = _currentFrameStartTime - _prevFrameStartTime;
            _prevFrameStartTime = _currentFrameStartTime;
        }

        uint64_t GetTotalMilliseconds()
        {
            return _firstFrameStartTime == 0 ? 0 : NSToMS(GetClockTimeNS() - _firstFrameStartTime);
        }

        // Time since the last update
        double GetElapsedSeconds() {
            return _frameTime / 1'000'000'000.0;
        }

        //double GetInputFrac(bool synchronised, double tickRate) {
        //    if (synchronised) return 1;

        //    const double max = 1000. / tickRate;
        //    const double now = msTimeF();
        //    const double elapsedInputTicks = std::min(now - _lastinputtime, max);
        //    _lastinputtime = now;

        //    if (elapsedInputTicks >= max) return 1;

        //    // Calculate an amplification to apply to the result before returning,
        //    // factoring in the game's ticrate and the value of the result.
        //    // This rectifies a deviation of 100+ ms or more depending on the length
        //    // of the operation to be within 1-2 ms of synchronised input
        //    // from 60 fps to at least 1000 fps at ticrates of 30 and 40 Hz.
        //    const double result = elapsedInputTicks * tickRate * (1. / 1000.);
        //    return result * (1. + 0.35 * (1. - tickRate * (1. / 50.)) * (1. - result));
        //}

    private:
        // Returns current time in ticks
        int GetElapsedTicks() {
            return NSToTick(_currentFrameStartTime - _firstFrameStartTime, TickRate);
        }


        void UpdateFrameTime() {
            if (_freezeTime != 0) return;
            _currentFrameStartTime = GetClockTimeNS();
            if (_firstFrameStartTime == 0)
                _firstFrameStartTime = _currentFrameStartTime;
        }

        // Waits until the next tick before returning
        int WaitForTick() {
            int time{};
            while ((time = GetElapsedTicks()) <= _prevTick) {
                // The minimum amount of time a thread can sleep is controlled by timeBeginPeriod().
                const auto next = _firstFrameStartTime + TickToNS(_prevTick + 1, TickRate);
                const auto now = GetClockTimeNS();
                assert(next > 0);

                if (next > now) {
                    const auto sleepTime = NSToMS(next - now);
                    assert(sleepTime < 1000);

                    if (sleepTime > 2)
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime - 2));
                }

                UpdateFrameTime();
            }

            return time;
        }

        uint64_t GetClockTimeNS() {
            using namespace std::chrono;
            auto time = (uint64_t)(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
            return TimeScale == 1.0 ? time : time * (uint64_t)(TimeScale * 1000);
        }
    };
}