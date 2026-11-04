#!/bin/bash

set -e

green='\033[0;32m'
clear='\033[0m'

printf "${green}Flashing BIOS...\n${clear}"
./dsc ./ds/bios/entry.ds -I ds/sdk/ --verbose -o ./dragon/bios.bin --raw --load 0x0 --fixed 16777216,0xFF --rom -Wa,$1

printf "\n${green}Compiling Drake...\n${clear}"
./dsc ./ds/drake/entry.ds -I ds/sdk/ --verbose -o dragon/drake.bin --raw --load 0x01010000 --fixed 4194304,0x00 --save-disassembly disassembly/ -Wa,$1
./dtools load-binary dragon/disk2.drd dragon/drake.bin 0x00000000

printf "\n${green}Compiling DragonOS...\n${clear}"
printf "\n${green}1) kernel0.dss\n${clear}"
./dasm dss/DragonOS/kernel0/kernel0.dss -o dragon/kernel0.bin -I dss --save-final-stage ./kernel0_final_stage.txt $1
./dtools load-binary dragon/disk2.drd dragon/kernel0.bin 0x00700200

printf "\n"
