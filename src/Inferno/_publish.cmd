xcopy shaders\*.hlsl publish\shaders\ /y
xcopy shaders\*.hlsli publish\shaders\ /y

xcopy *.txt publish\ /y
xcopy data\*.wav publish\data\ /y
xcopy textures\ publish\textures\ /y /s

xcopy d1\*.ied publish\d1\ /y
xcopy d1\*.yml publish\d1\ /y
xcopy d1\*.pof publish\d1\ /y
rem copy d1\demo publish\d1\demo /y

xcopy d2\*.ied publish\d2\ /y
xcopy d2\*.yml publish\d2\ /y
xcopy d2\*.pof publish\d2\ /y

xcopy ..\..\bin\Inferno\x64\Release\Inferno.exe publish\ /y
xcopy ..\..\bin\Inferno\x64\Release\*.dll publish\ /y

pause