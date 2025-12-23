#!/bin/bash

set -e

green='\033[0;32m'
clear='\033[0m'

printf "${green}Flashing BIOS...\n${clear}"
./dasm dss/bios/entry.dss -o dragon/bios.bin $1

printf "\n${green}Compiling Drake...\n${clear}"
printf "\n${green}1) loader.dss\n${clear}"
./dasm dss/drake/loader.dss -o dragon/drake_loader.bin $1
./dtools load-binary dragon/disk1.dr dragon/drake_loader.bin 0x00000000
printf "\n${green}2) bootsector.dss\n${clear}"
./dasm dss/drake/bootsector.dss -o dragon/drake_bootsector.bin $1
./dtools load-binary dragon/disk1.dr dragon/drake_bootsector.bin 0x00000400

printf "\n${green}Compiling DragonOS...\n${clear}"
printf "\n${green}1) kernel0.dss\n${clear}"
./dasm dss/DragonOS/kernel0/kernel0.dss -o dragon/kernel0.bin $1
./dtools load-binary dragon/disk1.dr dragon/kernel0.bin 0x00018000

printf "\n"
