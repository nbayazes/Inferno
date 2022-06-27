rem Run from developer cmd prompt otherwise signing will fail
dxc level.hlsl -T vs_6_0 -E VSLevel -Fo bin/level.vs.bin
dxc level.hlsl -T ps_6_0 -E PSLevel -Fo bin/level.ps.bin

dxc levelflat.hlsl -T vs_6_0 -E VSLevel -Fo bin/levelflat.vs.bin
dxc levelflat.hlsl -T ps_6_0 -E PSLevel -Fo bin/levelflat.ps.bin

dxc editor.hlsl -T vs_6_0 -E VSFlat -Fo bin/editor.vs.bin
dxc editor.hlsl -T ps_6_0 -E PSFlat -Fo bin/editor.ps.bin

dxc imgui.hlsl -T vs_6_0 -E VSMain -Fo bin/imgui.vs.bin
dxc imgui.hlsl -T ps_6_0 -E PSMain -Fo bin/imgui.ps.bin

dxc sprite.hlsl -T vs_6_0 -E VSMain -Fo bin/sprite.vs.bin
dxc sprite.hlsl -T ps_6_0 -E PSMain -Fo bin/sprite.ps.bin

dxc object.hlsl -T vs_6_0 -E VSMain -Fo bin/object.vs.bin
dxc object.hlsl -T ps_6_0 -E PSMain -Fo bin/object.ps.bin

dxc BloomExtractDownsampleCS.hlsl -T cs_6_0 -E main -Fo bin/BloomExtractDownsampleCS.bin
dxc DownsampleBloomCS.hlsl -T cs_6_0 -E main -Fo bin/DownsampleBloomCS.bin
dxc UpsampleAndBlurCS.hlsl -T cs_6_0 -E main -Fo bin/UpsampleAndBlurCS.bin
dxc ToneMapCS.hlsl -T cs_6_0 -E main -Fo bin/ToneMapCS.bin
dxc BlurCS.hlsl -T cs_6_0 -E main -Fo bin/BlurCS.bin
