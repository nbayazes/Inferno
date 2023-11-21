#pragma once

namespace Inferno {
    // Timer that compares itself to the elapsed game time
    class GameTimer {
        double _timestamp = 0;

    public:
        GameTimer() = default;
        GameTimer(float delay);

        float Remaining() const;
        bool Expired() const;

        void operator +=(float value) { _timestamp += value; }
        void operator -=(float value) { _timestamp -= value; }

        bool operator < (float value) const;
        bool operator <= (float value) const;
        bool operator > (float value) const;
        bool operator >= (float value) const;

        auto operator<=>(const GameTimer&) const = default;
    };
}
