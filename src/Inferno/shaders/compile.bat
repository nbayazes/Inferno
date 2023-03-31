rem Run from developer cmd prompt otherwise signing will fail
dxc level.hlsl -T vs_6_0 -E vsmain -Fo bin/level.vs.bin -Zi
dxc level.hlsl -T ps_6_0 -E psmain -Fo bin/level.ps.bin -Zi

dxc levelflat.hlsl -T vs_6_0 -E vsmain -Fo bin/levelflat.vs.bin
dxc levelflat.hlsl -T ps_6_0 -E psmain -Fo bin/levelflat.ps.bin

dxc editor.hlsl -T vs_6_0 -E vsmain -Fo bin/editor.vs.bin
dxc editor.hlsl -T ps_6_0 -E psmain -Fo bin/editor.ps.bin

dxc imgui.hlsl -T vs_6_0 -E vsmain -Fo bin/imgui.vs.bin
dxc imgui.hlsl -T ps_6_0 -E psmain -Fo bin/imgui.ps.bin

dxc sprite.hlsl -T vs_6_0 -E vsmain -Fo bin/sprite.vs.bin
dxc sprite.hlsl -T ps_6_0 -E psmain -Fo bin/sprite.ps.bin

dxc object.hlsl -T vs_6_0 -E vsmain -Fo bin/object.vs.bin
dxc object.hlsl -T ps_6_0 -E psmain -Fo bin/object.ps.bin

dxc depth.hlsl -T vs_6_0 -E vsmain -Fo bin/depth.vs.bin
dxc depth.hlsl -T ps_6_0 -E psmain -Fo bin/depth.ps.bin

dxc DepthObject.hlsl -T vs_6_0 -E vsmain -Fo bin/DepthObject.vs.bin
dxc DepthObject.hlsl -T ps_6_0 -E psmain -Fo bin/DepthObject.ps.bin

dxc DepthCutout.hlsl -T vs_6_0 -E vsmain -Fo bin/DepthCutout.vs.bin
dxc DepthCutout.hlsl -T ps_6_0 -E psmain -Fo bin/DepthCutout.ps.bin

dxc HUD.hlsl -T vs_6_0 -E vsmain -Fo bin/HUD.vs.bin
dxc HUD.hlsl -T ps_6_0 -E psmain -Fo bin/HUD.ps.bin


dxc BloomExtractDownsampleCS.hlsl -T cs_6_0 -E main -Fo bin/BloomExtractDownsampleCS.bin
dxc DownsampleBloomCS.hlsl -T cs_6_0 -E main -Fo bin/DownsampleBloomCS.bin
dxc UpsampleAndBlurCS.hlsl -T cs_6_0 -E main -Fo bin/UpsampleAndBlurCS.bin
dxc ToneMapCS.hlsl -T cs_6_0 -E main -Fo bin/ToneMapCS.bin
dxc BlurCS.hlsl -T cs_6_0 -E main -Fo bin/BlurCS.bin
dxc ScanlineCS.hlsl -T cs_6_0 -E main -Fo bin/ScanlineCS.bin
dxc FillLightGridCS.hlsl -T cs_6_0 -E main -Fo bin/FillLightGridCS.bin -Zi

