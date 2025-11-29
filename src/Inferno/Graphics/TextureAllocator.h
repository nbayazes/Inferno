#pragma once


namespace Inferno {
    class Image;
    class GpuResource;
}

/*
 * Inferno texture allocator
 *
 * Textures are allocated with a name and uploaded to the GPU.
 *
 */
namespace Inferno::textures {
    struct UploadStats {
        uint genNormals = 0;
        uint genSpecular = 0;
        uint genMips = 0;
        uint uploads = 0;
        int64 elapsedRead = 0; // microseconds
        int64 elapsedGen = 0; // microseconds
        int64 elapsedUpload = 0; // microseconds
    };

    //void Allocate(string_view name, Image& image);
    void BeginUpload();
    void QueueUpload(string_view name, Image&& image);
    UploadStats EndUpload();
    void QueueLoadMaterial(string_view name);
    void Shutdown();

    // Finds a previously uploaded texture
    GpuResource* Find(string_view name);
}
