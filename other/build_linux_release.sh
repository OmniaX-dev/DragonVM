#!/bin/bash

cd ..
./build release

printf "\n\n\033[0;32mBuilding Linux release...\n\033[0m"

mkdir bin/DragonVM_linux64

cp -r extra/* bin/DragonVM_linux64/
cp bin/dvm bin/DragonVM_linux64
cp bin/ddb bin/DragonVM_linux64
cp bin/dasm bin/DragonVM_linux64
cp bin/dtools bin/DragonVM_linux64


cp -r licences bin/DragonVM_linux64
cp LICENSE bin/DragonVM_linux64/licences/DragonVM-LICENCE.txt

printf "\n\033[0;32mLinux release ready in bin/DragonVM_linux64!\n\033[0m"
