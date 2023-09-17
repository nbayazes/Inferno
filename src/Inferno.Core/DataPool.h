#pragma once

#include <vector>
#include <functional>
#include <span>

namespace Inferno {
    // Contiguous data pool that reuses elements when a condition is met.
    // todo: skip expired items when iterating
    template<class TData, class TKey = int>
    class DataPool {
        std::vector<TData> _data;
        std::function<bool(const TData&)> _aliveFn;
        size_t _liveItems = 0;

    public:
        DataPool(std::function<bool(const TData&)> aliveFn, size_t capacity)
            : _aliveFn(aliveFn) {
            _data.reserve(capacity);
        }

        TData& Get(TKey key) {
            assert(InRange(key));
            return _data[(int64)key];
        }

        size_t Count() const { return _liveItems; }

        // Adds an element to the container
        TKey Add(const TData& data) {
            for (size_t i = 0; i < _data.size(); i++) {
                if (!_aliveFn(_data[i])) {
                    _data[i] = data;
                    return (TKey)i;
                }
            }

            _liveItems++;
            _data.push_back(data);
            return TKey(_data.size() - 1);
        }

        TKey AddBack(TData&& data) {
            for (size_t i = 0; i < _data.size(); i++) {
                if (!_aliveFn(_data[i])) {
                    _data[i] = std::move(data);
                    return (TKey)i;
                }
            }

            _liveItems++;
            _data.push_back(std::move(data));
            return TKey(_data.size() - 1);
        }

        // Allocates an element
        [[nodiscard]] TData& Alloc() {
            return Get(Add({}));
        }

        void Clear() {
            _data.clear();
            _liveItems = 0;
        }

        // Updates the live item count
        void Prune() {
            _liveItems = 0;

            for (size_t i = 0; i < _data.size(); i++) {
                if (_aliveFn(_data[i]))
                    _liveItems = i + 1;
            }

            _data.resize(_liveItems);
            _data.shrink_to_fit();
        }

        bool InRange(TKey index) const { return index >= (TKey)0 && index < (TKey)_data.size(); }

        // return a span of mostly-live data
        std::span<const TData> GetLiveData() const {
            return std::span<const TData>(_data.begin(), _liveItems);
        }

        [[nodiscard]] auto at(size_t index) { return _data.at(index); }
        [[nodiscard]] auto begin() { return _data.begin(); }
        [[nodiscard]] auto end() { return _data.end(); }
        [[nodiscard]] const auto begin() const { return _data.begin(); }
        [[nodiscard]] const auto end() const { return _data.end(); }
    };
}