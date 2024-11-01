#include "pch.h"

#include "walls_container.h"

namespace Inferno {

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
    assert(serializableWalls_->size() <= static_cast<int>(WallID::Max));

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
    if (type == WallType::Closed) {
        for (auto&& wall : walls_)
            if (wall.IsSimplyClosed())
                return true; //can always add another simply closed
    }
    //not a closed or no closed 
    return ShrinkableSize() < static_cast<size_t>(WallID::Max) - 1;
}

size_t WallsContainer::Size() const {
    return walls_.size();
}
size_t WallsContainer::ShrinkableSize() const {
    size_t count = 0;
    bool first = true;
    for (auto&& w : walls_) {
        if (!w.IsSimplyClosed() || first)
            ++count;
        if (first && w.IsSimplyClosed())
            first = false;
    }
    return count;
}

}//namespace Inferno