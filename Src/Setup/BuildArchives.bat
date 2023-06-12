REM ***** Collect PDBs

echo -- Creating symbols package
set CS_SYMBOLS_NAME=OpenShellPDB_%CS_VERSION_STR%
if %ARCH%==ARM64 set CS_SYMBOLS_NAME=%CS_SYMBOLS_NAME%_ARM64
set CS_SYMBOLS_NAME=%CS_SYMBOLS_NAME%.7z

if exist Final\%CS_SYMBOLS_NAME% del Final\%CS_SYMBOLS_NAME% > nul
7z a -mx9 .\Final\%CS_SYMBOLS_NAME% .\Output\symbols\* > nul

if defined APPVEYOR (
	appveyor PushArtifact Final\%CS_SYMBOLS_NAME%
)

cd ..

REM ***** Collect Localization files

echo -- Creating localization package
cd Localization
7z a -r -x!en-US -x!*WixUI_en-us.wxl -x!*.adml -x!*.admx -x!*LocComments.txt ..\Setup\Final\OpenShellLoc.zip English ..\ClassicExplorer\ExplorerL10N.ini ..\StartMenu\StartMenuL10N.ini ..\StartMenu\StartMenuHelper\StartMenuHelperL10N.ini English\OpenShellText-en-US.wxl English\OpenShellEULA.rtf > nul
cd ..

cd Setup

exit /b 0
