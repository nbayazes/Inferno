xcopy shaders\*.hlsl publish\shaders\ /y
xcopy shaders\*.hlsli publish\shaders\ /y

xcopy *.txt publish\ /y
xcopy data\*.wav publish\data\ /y
xcopy textures\ publish\textures\ /y /s

xcopy d1\*.ied publish\d1\ /y
xcopy d1\missions\vignette\ publish\d1\missions\vignette\ /y
xcopy d1\*.yml publish\d1\ /y
xcopy d1\*.pof publish\d1\ /y
rem copy d1\demo publish\d1\demo /y

xcopy d2\*.ied publish\d2\ /y
xcopy d2\*.yml publish\d2\ /y
xcopy d2\*.pof publish\d2\ /y

xcopy ..\..\bin\Inferno\x64\Release\Inferno.exe publish\ /y
xcopy ..\..\bin\Inferno\x64\Release\*.dll publish\ /y

cmd /C "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

cd shaders
dxc Asteroid.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Asteroid.vs.bin
dxc Asteroid.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Asteroid.ps.bin

dxc Automap.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Automap.vs.bin
dxc Automap.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Automap.ps.bin

dxc AutomapObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\AutomapObject.vs.bin
dxc AutomapObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\AutomapObject.ps.bin

dxc AutomapOutline.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\AutomapOutline.vs.bin
dxc AutomapOutline.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\AutomapOutline.ps.bin

dxc BloomExtractDownsampleCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\BloomExtractDownsampleCS.bin
dxc BlurCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\BlurCS.bin

dxc BriefingObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\BriefingObject.vs.bin
dxc BriefingObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\BriefingObject.ps.bin

dxc Cloak.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Cloak.vs.bin
dxc Cloak.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Cloak.ps.bin

dxc Compose.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Compose.vs.bin
dxc Compose.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Compose.ps.bin

dxc Depth.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Depth.vs.bin
dxc Depth.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Depth.ps.bin

dxc DepthCutout.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\DepthCutout.vs.bin
dxc DepthCutout.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\DepthCutout.ps.bin

dxc DepthObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\DepthObject.vs.bin
dxc DepthObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\DepthObject.ps.bin

dxc DownsampleBloomCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\DownsampleBloomCS.bin
dxc DownsampleCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\DownsampleCS.bin

dxc editor.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\editor.vs.bin
dxc editor.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\editor.ps.bin

dxc FillLightGridCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\FillLightGridCS.bin -Zi

dxc HUD.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\HUD.vs.bin
dxc HUD.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\HUD.ps.bin

dxc imgui.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\imgui.vs.bin
dxc imgui.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\imgui.ps.bin

dxc level.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\level.vs.bin
dxc level.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\level.ps.bin

dxc levelflat.hlsl -T vs_6_0 -E vsmain -Fo..\publish\shaders\levelflat.vs.bin
dxc levelflat.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\levelflat.ps.bin

dxc LinearizeDepthCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\LinearizeDepthCS.bin

dxc MenuSun.hlsl -T vs_6_0 -E vsmain -Fo..\publish\shaders\MenuSun.vs.bin
dxc MenuSun.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\MenuSun.ps.bin

dxc object.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\object.vs.bin
dxc object.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\object.ps.bin

dxc ScanlineCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ScanlineCS.bin

dxc sprite.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\sprite.vs.bin
dxc sprite.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\sprite.ps.bin

dxc Stars.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Stars.vs.bin
dxc Stars.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Stars.ps.bin

dxc Sun.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Sun.vs.bin
dxc Sun.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Sun.ps.bin

dxc Terrain.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Terrain.vs.bin
dxc Terrain.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Terrain.ps.bin

dxc TerrainDepth.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\TerrainDepth.vs.bin
dxc TerrainDepth.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\TerrainDepth.ps.bin

dxc ToneMapCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ToneMapCS.bin
dxc ToneMapCS-NoUAVL.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ToneMapCS-NoUAVL.bin
dxc UnpackBufferCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\UnpackBufferCS.bin
dxc UpsampleAndBlurCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\UpsampleAndBlurCS.bin

cd ..

rem cd publish

rem set /p "NAME=release name: "
.\publish\7z.exe a publish\publish.zip d1\*.ied
.\publish\7z.exe a publish\publish.zip d1\*.pof
.\publish\7z.exe a publish\publish.zip d1\*.yml

.\publish\7z.exe a publish\publish.zip d2\*.ied
.\publish\7z.exe a publish\publish.zip d2\*.pof
.\publish\7z.exe a publish\publish.zip d2\*.yml

.\publish\7z.exe a publish\publish.zip d1\d1xr-hires.dxa
.\publish\7z.exe a publish\publish.zip d1\missions\vignette\*.ied
.\publish\7z.exe a publish\publish.zip data\*.wav
.\publish\7z.exe a publish\publish.zip shaders\*.hlsli
.\publish\7z.exe a publish\publish.zip shaders\*.hlsl
.\publish\7z.exe a publish\publish.zip shaders\*.bin
.\publish\7z.exe a publish\publish.zip textures\

.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\Inferno.exe
.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\fmt.dll
.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\SDL3.dll
pause
