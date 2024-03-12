@echo off

set CCFLAGS=-Od -nologo -MTd -GR- -EHa- -Oi -W4 -wd4100 -wd4189 -wd4505 -wd4201 -FC -Z7 -DINTERNAL_BUILD=1 
set CLFLAGS=User32.lib Gdi32.lib Winmm.lib opengl32.lib

if not exist ..\build mkdir ..\build
pushd ..\build

if exist *.pdb del *.pdb
cl %CCFLAGS% ..\src\game.cpp -LD /link -incremental:no -PDB:sw_game_%random%.pdb -EXPORT:GameMain
cl %CCFLAGS% ..\src\win32.cpp /link -incremental:no %CLFLAGS% 
popd
