#pragma once
#include "OutrageTable.h"
#include "DirectX.h"

namespace Inferno {
    void CreateTestProcedural(Outrage::TextureInfo& texture);
    void CopyProceduralToTexture(const string& srcName, TexID destId);
    void UploadChangedProcedurals();
    void FreeProceduralTextures();
}