#!/bin/bash

set -e

green='\033[0;32m'
clear='\033[0m'

printf "${green}Building dragon-sdk...\n${clear}"
./dsc --lib ./ds/dragon-sdk/src/* -o ds/dragon-sdk/lib/dragon-sdk.dlib -Wa,$1
cp ./ds/dragon-sdk/lib/dragon-sdk.dlib ../extra/ds/dragon-sdk/lib/dragon-sdk.dlib

printf "${green}Flashing BIOS...\n${clear}"
./dsc ./ds/bios/entry.ds -L ds/dragon-sdk/lib --verbose -o ./dragon/bios.bin --raw --load 0x0 --fixed 16777216,0xFF --rom -Wa,$1

printf "\n${green}Compiling Drake...\n${clear}"
./dsc ./ds/drake/entry.ds -L ds/dragon-sdk/lib --verbose -o dragon/drake.bin --raw --load 0x01010000 --fixed 4194304,0x00 --save-disassembly disassembly/ -Wa,$1
./dtools load-binary dragon/disk.drd dragon/drake.bin 0x00000000

printf "\n${green}Compiling DragonOS...\n${clear}"
printf "\n${green}1) kernel0.dss\n${clear}"
./dsc ./ds/dragon-os/kernel0/kernel0.ds -L ds/dragon-sdk/lib --verbose -o dragon/kernel0.bin --raw --header KERNEL0_BOOT --load 0x01410000 --fixed 1048576,0xFF --save-disassembly disassembly/ -Wa,$1
./dtools load-binary dragon/disk.drd dragon/kernel0.bin 0x00700200
./dtools load-binary dragon/disk.drd dragon/kernel0.bin 0x01600200

printf "\n"
