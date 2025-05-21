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

rem cmd /C "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
rem 
rem cd shaders
rem dxc Asteroid.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Asteroid.vs.bin
rem dxc Asteroid.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Asteroid.ps.bin
rem 
rem dxc Automap.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Automap.vs.bin
rem dxc Automap.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Automap.ps.bin
rem 
rem dxc AutomapObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\AutomapObject.vs.bin
rem dxc AutomapObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\AutomapObject.ps.bin
rem 
rem dxc AutomapOutline.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\AutomapOutline.vs.bin
rem dxc AutomapOutline.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\AutomapOutline.ps.bin
rem 
rem dxc BloomExtractDownsampleCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\BloomExtractDownsampleCS.bin
rem dxc BlurCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\BlurCS.bin
rem 
rem dxc BriefingObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\BriefingObject.vs.bin
rem dxc BriefingObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\BriefingObject.ps.bin
rem 
rem dxc Cloak.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Cloak.vs.bin
rem dxc Cloak.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Cloak.ps.bin
rem 
rem dxc Compose.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Compose.vs.bin
rem dxc Compose.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Compose.ps.bin
rem 
rem dxc Depth.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Depth.vs.bin
rem dxc Depth.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Depth.ps.bin
rem 
rem dxc DepthCutout.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\DepthCutout.vs.bin
rem dxc DepthCutout.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\DepthCutout.ps.bin
rem 
rem dxc DepthObject.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\DepthObject.vs.bin
rem dxc DepthObject.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\DepthObject.ps.bin
rem 
rem dxc DownsampleBloomCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\DownsampleBloomCS.bin
rem dxc DownsampleCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\DownsampleCS.bin
rem 
rem dxc editor.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\editor.vs.bin
rem dxc editor.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\editor.ps.bin
rem 
rem dxc FillLightGridCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\FillLightGridCS.bin -Zi
rem 
rem dxc HUD.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\HUD.vs.bin
rem dxc HUD.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\HUD.ps.bin
rem 
rem dxc imgui.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\imgui.vs.bin
rem dxc imgui.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\imgui.ps.bin
rem 
rem dxc level.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\level.vs.bin
rem dxc level.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\level.ps.bin
rem 
rem dxc levelflat.hlsl -T vs_6_0 -E vsmain -Fo..\publish\shaders\levelflat.vs.bin
rem dxc levelflat.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\levelflat.ps.bin
rem 
rem dxc LinearizeDepthCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\LinearizeDepthCS.bin
rem 
rem dxc MenuSun.hlsl -T vs_6_0 -E vsmain -Fo..\publish\shaders\MenuSun.vs.bin
rem dxc MenuSun.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\MenuSun.ps.bin
rem 
rem dxc object.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\object.vs.bin
rem dxc object.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\object.ps.bin
rem 
rem dxc ScanlineCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ScanlineCS.bin
rem 
rem dxc sprite.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\sprite.vs.bin
rem dxc sprite.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\sprite.ps.bin
rem 
rem dxc Stars.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Stars.vs.bin
rem dxc Stars.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Stars.ps.bin
rem 
rem dxc Sun.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Sun.vs.bin
rem dxc Sun.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Sun.ps.bin
rem 
rem dxc Terrain.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\Terrain.vs.bin
rem dxc Terrain.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\Terrain.ps.bin
rem 
rem dxc TerrainDepth.hlsl -T vs_6_0 -E vsmain -Fo ..\publish\shaders\TerrainDepth.vs.bin
rem dxc TerrainDepth.hlsl -T ps_6_0 -E psmain -Fo ..\publish\shaders\TerrainDepth.ps.bin
rem 
rem dxc ToneMapCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ToneMapCS.bin
rem dxc ToneMapCS-NoUAVL.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\ToneMapCS-NoUAVL.bin
rem dxc UnpackBufferCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\UnpackBufferCS.bin
rem dxc UpsampleAndBlurCS.hlsl -T cs_6_0 -E main -Fo ..\publish\shaders\UpsampleAndBlurCS.bin

rem cd ..

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

rem zip demo data and dxcompiler
cd publish

copy publish.zip publish-full.zip /Y

7z.exe a publish-full.zip d1\demo\descent.hog
7z.exe a publish-full.zip d1\demo\descent.pig
7z.exe a publish-full.zip dxcompiler.dll
7z.exe a publish-full.zip dxil.dll

pause
