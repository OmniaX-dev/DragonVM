#!/bin/bash

rm -r ./win-release

./compile

cd bin

printf "${green}Compiling vBIOS...\n${clear}"
./dasm dss/bios.dss -o dragon/bios.bin --save-disassembly disassembly/bios.dds --verbose

printf "\n${green}Creating Virtual Disk...\n${clear}"
rm dragon/disk1.dr
./dtools new-vdisk dragon/disk1.dr 1048576

printf "\n${green}Compiling MBR Block...\n${clear}"
./dasm dss/mbr.dss -o dragon/mbr.bin --save-disassembly disassembly/mbr.dds --verbose

printf "\n${green}Loading MBR Block...\n${clear}"
./dtools load-binary dragon/disk1.dr dragon/mbr.bin 0x00000000

printf "${green}Compiling Test Program...\n"
./dasm dss/test.dss -o test.bin --save-disassembly disassembly/test.dds --verbose

printf "${green}Creating Windows Release...\n"
cd ..
mkdir win-release
mkdir win-release/disassembly
cp ./bin/dasm.exe ./win-release
cp ./bin/ddb.exe ./win-release
cp ./bin/dtools.exe ./win-release
cp ./bin/dvm.exe ./win-release
cp ./bin/font.bmp ./win-release
# cp ./bin/test.bin ./win-release

cp -r ./bin/config ./win-release
cp -r ./bin/disassembly/bios.dds ./win-release/disassembly
cp -r ./bin/disassembly/mbr.dds ./win-release/disassembly
cp -r ./bin/dragon ./win-release

mkdir ./win-release/dss
cp ./bin/dss/bios.dss ./win-release/dss
cp ./bin/dss/mbr.dss ./win-release/dss
cp ./bin/dss/test.dss ./win-release/dss

cp C:/msys64/ucrt64/bin/libgcc_s_seh-1.dll ./win-release
cp C:/msys64/ucrt64/bin/libstdc++-6.dll ./win-release
cp C:/msys64/ucrt64/bin/libwinpthread-1.dll ./win-release
cp C:/msys64/ucrt64/bin/SDL2.dll ./win-release
cp C:/msys64/ucrt64/bin/libostd.dll ./win-release

cp ./run-test.bat ./win-release