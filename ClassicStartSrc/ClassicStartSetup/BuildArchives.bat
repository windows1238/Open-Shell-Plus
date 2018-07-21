REM ***** Collect PDBs

set CS_SYMBOLS_NAME=ClassicStartPDB_%CS_VERSION_STR%.7z

if exist Final\%CS_SYMBOLS_NAME% del Final\%CS_SYMBOLS_NAME%
cd Output
7z a -mx9 ..\Final\%CS_SYMBOLS_NAME% PDB32 PDB64
cd ..

if defined APPVEYOR (
	appveyor PushArtifact Final\%CS_SYMBOLS_NAME%
)

cd ..

REM ***** Collect Localization files

del ClassicStartSetup\Final\ClassicStartLoc.zip
cd Localization
7z a -r -x!en-US -x!*WixUI_en-us.wxl -x!*.adml -x!*.admx -x!*LocComments.txt ..\ClassicStartSetup\Final\ClassicStartLoc.zip English ..\ClassicExplorer\ExplorerL10N.ini ..\ClassicStartMenu\StartMenuL10N.ini ..\ClassicStartMenu\StartMenuHelper\StartMenuHelperL10N.ini English\ClassicStartText-en-US.wxl English\ClassicStartEULA.rtf
cd ..

cd ClassicStartSetup
