#include "pch.h"
#include "BitmapCache.h"
#include "Resources.h"

namespace Inferno {
    //// Finds the entry for a texture based on name (not bitmap name)
    //MaterialHandle TextureCache::Find(string name) {
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (String::InvariantEquals(_materials[i].Name, name))
    //            return (MaterialHandle)i;
    //    }

    //    return MaterialHandle::None;
    //}

    //MaterialHandle TextureCache::Find(TexID id) {
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (_materials[i].PigID == id)
    //            return (MaterialHandle)i;
    //    }

    //    return MaterialHandle::None;
    //}

    //MaterialHandle TextureCache::Alloc(string name) {
    //    if (!name.empty()) {
    //        auto handle = Find(name);
    //        if (handle != MaterialHandle::None)
    //            return handle; // already allocated
    //    }

    //    auto handle = Alloc();
    //    if (!name.empty())
    //        Get(handle).Tablefile = 0; // hard code 0 for now

    //    return handle;
    //}

    //MaterialHandle TextureCache::Alloc(TexID id) {
    //    if (id == TexID::None) return MaterialHandle::None;

    //    auto handle = Find(id);
    //    if (handle != MaterialHandle::None)
    //        return handle; // already allocated

    //    return Alloc();
    //}

    //MaterialHandle TextureCache::Alloc() {
    //    // Find a free slot
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (!_materials[i].Used)
    //            return (MaterialHandle)i;
    //    }

    //    // Alloc a new slot
    //    _materials.emplace_back();
    //    return (MaterialHandle)(_materials.size() - 1);
    //}
}
