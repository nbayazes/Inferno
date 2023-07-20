#pragma once
#include "Graphics/GpuResources.h"

namespace Inferno {
    enum class TextureState {
        Vacant,   // Default state 
        Resident, // Texture is loaded
        PagingIn  // Texture is being loaded
    };

    struct Material2D {
        enum { Diffuse, SuperTransparency, Emissive, Specular, Normal, Count };

        Texture2D Textures[Count]{};
        // SRV handles
        D3D12_GPU_DESCRIPTOR_HANDLE Handles[Count] = {};
        uint UploadIndex = 0;
        TexID ID = TexID::Invalid;
        string Name;
        TextureState State = TextureState::Vacant;

        explicit operator bool() const { return State == TextureState::Resident; }
        UINT64 Pointer() const { return Handles[Diffuse].ptr; }

        // Returns the handle of the first texture in the material. Materials are created so that all textures are contiguous.
        // In most cases only the first handle is necessary.
        D3D12_GPU_DESCRIPTOR_HANDLE Handle() const { return Handles[Diffuse]; }
    };

}
