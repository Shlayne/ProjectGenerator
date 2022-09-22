@echo off
pushd ..
call Dependencies\premake\premake5.exe vs2022
popd
pause
