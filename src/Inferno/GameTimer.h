#pragma once

namespace Inferno {
    // Timer that compares itself to the elapsed game time
    class GameTimer {
        double _timestamp = 0;

    public:
        GameTimer() = default;
        GameTimer(float delay);

        float Remaining() const;
        // Returns true if a timer has expired. Returns false if running or timer was never set.
        // Prefer using the comparison operators to determine if the timer is running.
        bool Expired() const;
        void Reset() { _timestamp = 0; }

        // Returns true if the timer is counting down
        bool IsSet() const { return _timestamp > 0; }

        void operator +=(float value) { _timestamp += value; }
        void operator -=(float value) { _timestamp -= value; }

        bool operator <(float value) const;
        bool operator <=(float value) const;
        bool operator >(float value) const;
        bool operator >=(float value) const;

        auto operator<=>(const GameTimer&) const = default;
    };
}
