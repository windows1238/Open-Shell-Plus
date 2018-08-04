if exist Output rd /Q /S Output
md Output
md Output\x64
md Output\PDB32
md Output\PDB64

echo -- Compiling

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set MSBuildDir=%%i\MSBuild\15.0\Bin\

REM ********* Build 64-bit solution
echo --- 64bit
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="x64" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1

REM ********* Build 32-bit solution (must be after 64-bit)
echo --- 32bit
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="Win32" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1


REM ********* Make en-US.dll
cd ..
Setup\Utility\Release\Utility.exe makeEN ClassicExplorer\Setup\ClassicExplorer32.dll Menu\Setup\MenuDLL.dll ClassicIE\Setup\ClassicIEDLL_32.dll Update\Release\Update.exe
@if ERRORLEVEL 1 exit /b 1

Setup\Utility\Release\Utility.exe extract en-US.dll en-US.csv
copy /B en-US.dll Localization\English > nul
move en-US.csv Localization\English > nul

cd Setup


REM ********* Copy binaries

copy /B ..\ClassicExplorer\Setup\ClassicExplorer32.dll Output > nul
copy /B ..\ClassicExplorer\Setup\ClassicExplorerSettings.exe Output > nul
copy /B ..\ClassicIE\Setup\ClassicIEDLL_32.dll Output > nul
copy /B ..\ClassicIE\Setup\ClassicIE_32.exe Output > nul
copy /B ..\Menu\Setup\Menu.exe Output > nul
copy /B ..\Menu\Setup\MenuDLL.dll Output > nul
copy /B ..\Update\Release\Update.exe Output > nul
copy /B ..\Menu\StartMenuHelper\Setup\StartMenuHelper32.dll Output > nul
copy /B ..\Setup\SetupHelper\Release\SetupHelper.exe Output > nul

copy /B ..\ClassicExplorer\Setup64\ClassicExplorer64.dll Output\x64 > nul
copy /B ..\ClassicIE\Setup64\ClassicIEDLL_64.dll Output\x64 > nul
copy /B ..\ClassicIE\Setup64\ClassicIE_64.exe Output\x64 > nul
copy /B ..\Menu\Setup64\Menu.exe Output\x64 > nul
copy /B ..\Menu\Setup64\MenuDLL.dll Output\x64 > nul
copy /B ..\Menu\StartMenuHelper\Setup64\StartMenuHelper64.dll Output\x64 > nul

copy /B "..\Menu\Skins\Classic Skin.skin" Output > nul
copy /B "..\Menu\Skins\Full Glass.skin" Output > nul
copy /B "..\Menu\Skins\Smoked Glass.skin" Output > nul
copy /B "..\Menu\Skins\Windows Aero.skin" Output > nul
copy /B "..\Menu\Skins\Windows Basic.skin" Output > nul
copy /B "..\Menu\Skins\Windows XP Luna.skin" Output > nul
copy /B "..\Menu\Skins\Windows 8.skin" Output > nul
copy /B "..\Menu\Skins\Metro.skin" Output > nul
copy /B "..\Menu\Skins\Classic Skin.skin7" Output > nul
copy /B "..\Menu\Skins\Windows Aero.skin7" Output > nul
copy /B "..\Menu\Skins\Windows 8.skin7" Output > nul
copy /B "..\Menu\Skins\Midnight.skin7" Output > nul
copy /B "..\Menu\Skins\Metro.skin7" Output > nul
copy /B "..\Menu\Skins\Metallic.skin7" Output > nul


REM ********* Collect debug info

REM Explorer 32
copy /B ..\ClassicExplorer\Setup\ClassicExplorer32.pdb Output\PDB32 > nul
copy /B Output\ClassicExplorer32.dll Output\PDB32 > nul
copy /B ..\ClassicExplorer\Setup\ClassicExplorerSettings.pdb Output\PDB32 > nul
copy /B Output\ClassicExplorerSettings.exe Output\PDB32 > nul

REM Explorer 64
copy /B ..\ClassicExplorer\Setup64\ClassicExplorer64.pdb Output\PDB64 > nul
copy /B Output\x64\ClassicExplorer64.dll Output\PDB64 > nul

REM IE 32
copy /B ..\ClassicIE\Setup\ClassicIEDLL_32.pdb Output\PDB32 > nul
copy /B Output\ClassicIEDLL_32.dll Output\PDB32 > nul
copy /B ..\ClassicIE\Setup\ClassicIE_32.exe Output\PDB32 > nul
copy /B Output\ClassicIE_32.exe Output\PDB32 > nul

REM IE 64
copy /B ..\ClassicIE\Setup64\ClassicIEDLL_64.pdb Output\PDB64 > nul
copy /B Output\x64\ClassicIEDLL_64.dll Output\PDB64 > nul
copy /B ..\ClassicIE\Setup64\ClassicIE_64.exe Output\PDB64 > nul
copy /B Output\x64\ClassicIE_64.exe Output\PDB64 > nul

REM Menu 32
copy /B ..\Menu\Setup\Menu.pdb Output\PDB32 > nul
copy /B Output\Menu.exe Output\PDB32 > nul
copy /B ..\Menu\Setup\MenuDLL.pdb Output\PDB32 > nul
copy /B Output\MenuDLL.dll Output\PDB32 > nul
copy /B ..\Menu\StartMenuHelper\Setup\StartMenuHelper32.pdb Output\PDB32 > nul
copy /B Output\StartMenuHelper32.dll Output\PDB32 > nul
copy /B ..\Update\Release\Update.pdb Output\PDB32 > nul
copy /B Output\Update.exe Output\PDB32 > nul

REM Menu 64
copy /B ..\Menu\Setup64\Menu.pdb Output\PDB64 > nul
copy /B Output\x64\Menu.exe Output\PDB64 > nul
copy /B ..\Menu\Setup64\MenuDLL.pdb Output\PDB64 > nul
copy /B Output\x64\MenuDLL.dll Output\PDB64 > nul
copy /B ..\Menu\StartMenuHelper\Setup64\StartMenuHelper64.pdb Output\PDB64 > nul
copy /B Output\x64\StartMenuHelper64.dll Output\PDB64 > nul


REM ********* Source Index PDBs

set PDBSTR_PATH="C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv\pdbstr.exe"

if exist %PDBSTR_PATH% (
	echo --- Adding source index to PDBs
	call CreateSourceIndex.bat ..\.. > Output\pdbstr.txt

	for %%f in (Output\PDB32\*.pdb) do (
		%PDBSTR_PATH% -w -p:%%f -s:srcsrv -i:Output\pdbstr.txt
	)

	for %%f in (Output\PDB64\*.pdb) do (
		%PDBSTR_PATH% -w -p:%%f -s:srcsrv -i:Output\pdbstr.txt
	)
)

REM ********* Build ADMX
echo --- ADMX
if exist Output\PolicyDefinitions.zip (
  del Output\PolicyDefinitions.zip
)
cd ..\Localization\English
..\..\Menu\Setup\Menu.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\ClassicExplorer\Setup\ClassicExplorerSettings.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\ClassicIE\Setup\ClassicIE_32.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
md en-US
copy /B *.adml en-US > nul
7z a ..\..\Setup\Output\PolicyDefinitions.zip *.admx en-US\*.adml PolicyDefinitions.rtf > nul
rd /Q /S en-US
cd ..\..\Setup

exit /b 0
