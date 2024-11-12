#pragma once

#include "Wall.h"

#include <vector>
#include <unordered_map>

namespace Inferno {

class WallsContainer {
	std::vector<Wall> walls_;
	mutable std::optional<std::vector<Wall const*>> serializableWalls_;
	size_t max_;
	WallsSerialization option_{ WallsSerialization::STANDARD };

public:
	using Iterator = std::vector<Wall>::iterator;
	using ConstIterator = std::vector<Wall>::const_iterator;
	using SerializationGuard = std::shared_ptr<void>;

	WallsContainer(size_t maxSize, WallsSerialization);

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
	bool Overfilled() const;

	WallsSerialization SerializationKind() const;
	void SerializationKind(WallsSerialization);
};



} //namespace Inferno