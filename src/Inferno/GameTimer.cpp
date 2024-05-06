#include "pch.h"
#include "GameTimer.h"
#include "Game.h"

namespace Inferno {
    GameTimer::GameTimer(float delay) {
        _timestamp = Game::Time + delay;
    }

    bool GameTimer::Expired() const {
        return Game::Time >= _timestamp && _timestamp != 0;
    }

    float GameTimer::Remaining() const {
        return std::max(float(_timestamp - Game::Time), 0.0f);
    }

    bool GameTimer::operator<(float value) const {
        return _timestamp - Game::Time < value;
    }

    bool GameTimer::operator<=(float value) const {
        return _timestamp - Game::Time <= value;
    }

    bool GameTimer::operator>(float value) const {
        return _timestamp - Game::Time > value;
    }

    bool GameTimer::operator>=(float value) const {
        return _timestamp - Game::Time >= value;
    }
}
