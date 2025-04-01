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
.\publish\7z.exe a publish\publish.zip textures\

.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\Inferno.exe
.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\fmt.dll
.\publish\7z.exe a publish\publish.zip ..\..\bin\Inferno\x64\Release\SDL3.dll
pause
