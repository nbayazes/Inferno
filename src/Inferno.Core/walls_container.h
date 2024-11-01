#pragma once

#include "Wall.h"

#include <vector>
#include <unordered_map>

namespace Inferno {

class WallsContainer {
	std::vector<Wall> walls_;
	mutable std::optional<std::vector<Wall const*>> serializableWalls_;

public:
	using Iterator = std::vector<Wall>::iterator;
	using ConstIterator = std::vector<Wall>::const_iterator;
	using SerializationGuard = std::shared_ptr<void>;

	Iterator begin();
	Iterator end();

	ConstIterator begin() const;
	ConstIterator end() const;

	size_t Size() const;
	size_t ShrinkableSize() const;

	SerializationGuard PrepareSerialization() const;
	std::vector<Wall const*> const& SeralizableWalls() const;

	Wall& operator[](WallID);
	Wall const& operator[](WallID) const;

	WallID Append(Wall w);
	void Erase(WallID);

	Wall* TryGetWall(TriggerID);
	Wall* TryGetWall(WallID);
	Wall const* TryGetWall(WallID) const;

	bool CanAdd(WallType) const;
};



} //namespace Inferno