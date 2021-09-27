@echo off

set CommonLinkerFlags=-opt:ref -incremental:no
set CommonCompilerFlags=-Od -MTd -nologo -GR- -Gm- -EHa- -Oi -fp:fast -fp:except- -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -FC -Zo -Z7

mkdir build
pushd build
del *.pdb
cl %CommonCompilerFlags% ..\code\kilo.c -Fekilo.exe -D_CRT_SECURE_NO_WARNINGS /link %CommonLinkerFlags%
popd

echo Done
