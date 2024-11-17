#include "pch.h"

#include "walls_container.h"
#include "Types.h"

namespace Inferno {

WallsContainer::WallsContainer(size_t maxSize, WallsSerialization option)
    : max_{ maxSize }
    , option_{ option }
{ }

WallsContainer::Iterator WallsContainer::begin() {
	return walls_.begin();
}
WallsContainer::Iterator WallsContainer::end() {
	return walls_.end();
}

WallsContainer::ConstIterator WallsContainer::begin() const {
    return walls_.begin();
}
WallsContainer::ConstIterator WallsContainer::end() const {
    return walls_.end();
}

WallsContainer::SerializationGuard WallsContainer::PrepareSerialization() const {
    serializableWalls_.emplace();

    switch (option_) {
    case WallsSerialization::STANDARD:
        for (auto& wall : walls_) {
            auto id = static_cast<WallID>(serializableWalls_->size());
            serializableWalls_->push_back(&wall);
            wall.SerializationId = id;
        }
        break;
    case WallsSerialization::SHARED_SIMPLE_WALLS:
        {
            WallID firstClosed = WallID::None;
            for (auto& wall : walls_) {
                auto id = static_cast<WallID>(serializableWalls_->size());
                if (!wall.IsSimplyClosed() || firstClosed == WallID::None)
                    serializableWalls_->push_back(&wall);
        
                if (wall.IsSimplyClosed()) {
                    if (firstClosed == WallID::None)
                        firstClosed = id;
                    id = firstClosed;
                }

                wall.SerializationId = id;
            }
        }
        break;
    }

    assert(serializableWalls_->size() <= max_);

    return SerializationGuard(&serializableWalls_, [this](auto) {
        serializableWalls_.reset();
        for (auto& wall : walls_)
            wall.SerializationId = WallID::None;
    });
}

std::vector<Wall const*> const& WallsContainer::SeralizableWalls() const {
    //fails if no Prepare has been called or the guard is destroyed
    return *serializableWalls_;
}

Wall& WallsContainer::operator[](WallID id) {
	return *TryGetWall(id);
}
Wall const& WallsContainer::operator[](WallID id) const {
	return *TryGetWall(id);
}

Wall* WallsContainer::TryGetWall(TriggerID trigger) {
    if (trigger == TriggerID::None) 
        return nullptr;

    for (auto& wall : *this) {
        if (wall.Trigger == trigger)
            return &wall;
    }

    return nullptr;
}

Wall* WallsContainer::TryGetWall(WallID id) {
    if (id == WallID::None)
        return nullptr;

    auto iid = static_cast<int>(id);
    return iid < walls_.size()
        ? (walls_[iid].IsValid() ? &walls_[iid] : nullptr) //the current implementation actually keeps walls always valid: this seems to be some ghost from the past 
        : nullptr;
}

Wall const* WallsContainer::TryGetWall(WallID id) const { 
    return const_cast<WallsContainer&>(*this).TryGetWall(id);
}

WallID WallsContainer::Append(Wall wall) {
    walls_.push_back(std::move(wall));
    return static_cast<WallID>(walls_.size() - 1);
}

void WallsContainer::Erase(WallID id) {
    walls_.erase(walls_.begin() + static_cast<size_t>(id));
}

bool WallsContainer::CanAdd(WallType type) const {
    if (WallsSerialization::SHARED_SIMPLE_WALLS == option_
        && type == WallType::Closed) {
        for (auto&& wall : walls_)
            if (wall.IsSimplyClosed())
                return true; //can always add another simply closed
    }
    //not a closed or no closed 
    return ShrinkableSize() < max_;
}

bool WallsContainer::Overfilled() const {
    return ShrinkableSize() > max_;
}

size_t WallsContainer::Size() const {
    return walls_.size();
}
size_t WallsContainer::ShrinkableSize() const {
    if (WallsSerialization::STANDARD == option_)
        return Size();

    size_t count = 0;
    size_t shared = 0;
    for (auto&& w : walls_) {
        if (w.IsSimplyClosed())
            shared = 1;
        else
            ++count;
    }
    return count + shared;
}

WallsSerialization WallsContainer::SerializationKind() const {
    return option_;
}

void WallsContainer::SerializationKind(WallsSerialization option) {
    if (option == option_)
        return;

    if (WallsSerialization::SHARED_SIMPLE_WALLS == option_
        && Size() > max_)
        throw Exception("WallsContainer: cannot switch serialization kind, too many walls");

    option_ = option;
}

}//namespace Inferno