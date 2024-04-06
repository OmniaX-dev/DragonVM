#!/bin/bash

rm -r ./win-release

./compile

cd bin

printf "${green}Compiling vBIOS...\n${clear}"
./dasm dss/bios/entry.dss -o dragon/bios.bin --save-disassembly disassembly/bios.dds --verbose --save-exports

printf "\n${green}Creating Virtual Disk...\n${clear}"
rm dragon/disk1.dr
./dtools new-vdisk dragon/disk1.dr 1048576

printf "\n${green}Compiling MBR Block...\n${clear}"
./dasm dss/drake/entry.dss -o dragon/drake.bin --save-disassembly disassembly/drake.dds --verbose

printf "\n${green}Loading MBR Block...\n${clear}"
./dtools load-binary dragon/disk1.dr dragon/drake.bin 0x00000000

printf "${green}Compiling Test Program 1...\n"
./dasm dss/tests/test1.dss -o test1.bin --save-disassembly disassembly/test1.dds --verbose
printf "${green}Compiling Test Program 2...\n"
./dasm dss/tests/test2.dss -o test2.bin --save-disassembly disassembly/test2.dds --verbose

printf "${green}Creating Windows Release...\n"
cd ..
mkdir win-release
mkdir win-release/disassembly
cp ./bin/dasm.exe ./win-release
cp ./bin/ddb.exe ./win-release
cp ./bin/dtools.exe ./win-release
cp ./bin/dvm.exe ./win-release
cp ./bin/font.bmp ./win-release

cp -r ./bin/config ./win-release
cp -r ./bin/disassembly/bios.dds ./win-release/disassembly
cp -r ./bin/disassembly/drake.dds ./win-release/disassembly
cp -r ./bin/disassembly/test1.dds ./win-release/disassembly
cp -r ./bin/disassembly/test2.dds ./win-release/disassembly
cp -r ./bin/dragon ./win-release

rm ./bin/disassembly/test1.dds
rm ./bin/disassembly/test2.dds

mkdir ./win-release/dss4
mkdir ./win-release/dss/bios
mkdir ./win-release/dss/drake
cp ./bin/dss/bios/* ./win-release/dss/bios
cp ./bin/dss/drake/* ./win-release/dss/drake
cp ./bin/dss/tests/test1.dss ./win-release/dss
cp ./bin/dss/tests/test2.dss ./win-release/dss

cp C:/msys64/ucrt64/bin/libgcc_s_seh-1.dll ./win-release
cp C:/msys64/ucrt64/bin/libstdc++-6.dll ./win-release
cp C:/msys64/ucrt64/bin/libwinpthread-1.dll ./win-release
cp C:/msys64/ucrt64/bin/SDL2.dll ./win-release
cp C:/msys64/ucrt64/bin/libostd.dll ./win-release

cp ./extra/scripts/run-test-1.bat ./win-release
cp ./extra/scripts/run-test-2.bat ./win-release