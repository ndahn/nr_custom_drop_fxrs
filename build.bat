@echo off

mkdir build
pushd build
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake.exe .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5
msbuild.exe custom_drop_fxrs.vcxproj /p:configuration=release
copy Release\custom_drop_fxrs.dll ..\
popd
pause