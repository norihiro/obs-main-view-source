call "%~dp0..\ci_includes.generated.cmd"

mkdir package
cd package

git describe --tags --always > package-version.txt
set /p PackageVersion=<package-version.txt
del package-version.txt

copy ..\LICENSE          ..\release\data\obs-plugins\%PluginName%\LICENCE-%PluginName%.txt

REM Package ZIP archive
7z a "%PluginName%-%PackageVersion%-obs%1-Windows.zip" "..\release\*"

certutil.exe -hashfile "%PluginName%-%PackageVersion%-obs%1-Windows.zip" SHA1
