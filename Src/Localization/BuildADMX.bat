cd %2
..\..\Menu\Setup\Menu.exe -saveadmx %1
..\..\ClassicExplorer\Setup\ClassicExplorerSettings.exe -saveadmx %1
..\..\ClassicIE\Setup\ClassicIE_32.exe -saveadmx %1
cd ..
md PolicyDefinitions\%1
copy %2\*.adml PolicyDefinitions\%1
