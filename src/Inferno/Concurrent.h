#pragma once

#include "WorkerThread.h"

template<class T>
class ConcurrentList {
    std::mutex _lock;
    std::vector<T> _data;
public:
    ConcurrentList() = default;
    ConcurrentList(size_t size) : _data(size) {}

    void ForEach(auto&& fn) {
        std::scoped_lock lock(_lock);
        for (auto& x : _data)
            fn(x);
    }

    const auto Get() { return _data; }

    void Add(T&& data) {
        std::scoped_lock lock(_lock);
        _data.push_back(std::move(data));
    }

    void Add(T& data) {
        std::scoped_lock lock(_lock);
        _data.push_back(data);
    }

    void Clear() {
        std::scoped_lock lock(_lock); // an exception here means thread tried to lock twice
        _data.clear();
    }

    size_t Size() const { return _data.size(); }
    bool IsEmpty() const { return _data.empty(); }

    const T& operator[](size_t index) const { return _data[index]; }
    T& operator[](int index) { return _data[index]; }
};