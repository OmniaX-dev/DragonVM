#!/bin/bash

MSYS2_ROOT=c:/msys64

cd ..
./build release

printf "\n\n\033[0;32mBuilding Windows release...\n\033[0m"

mkdir bin/DragonVM_w64

cp -r extra/* bin/DragonVM_w64/
cp other/dvm_appIcon.ico bin/DragonVM_w64
cp other/ddb_appIcon.ico bin/DragonVM_w64
cp other/dtools_appIcon.ico bin/DragonVM_w64
cp other/dasm_appIcon.ico bin/DragonVM_w64

cp bin/dvm.exe bin/DragonVM_w64
cp bin/ddb.exe bin/DragonVM_w64
cp bin/dasm.exe bin/DragonVM_w64
cp bin/dtools.exe bin/DragonVM_w64

for dll in libgcc_s_seh-1.dll libstdc++-6.dll libostd.dll libwinpthread-1.dll \
           SDL2.dll SDL2_image.dll; do
    src="$MSYS2_ROOT/ucrt64/bin/$dll"
    [[ -f "$src" ]] || { echo "Missing: $dll"; exit 1; }
    cp "$src" bin/DragonVM_w64/
done

cp -r licences bin/DragonVM_w64
cp LICENSE bin/DragonVM_w64/licences/DragonVM-LICENCE.txt

printf "\n\033[0;32mWindows release ready in bin/DragonVM_w64!\n\033[0m"
