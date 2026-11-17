@echo off
REM Builds the custom layer (layer\4coder_byp.cpp) into build\custom_4coder.dll.
REM usage: build.bat [release]
setlocal
set root=%~dp0
pushd "%root%build"
call "%root%layer\custom\bin\buildsuper_x64-win.bat" "%root%layer\4coder_byp.cpp" %1
popd
