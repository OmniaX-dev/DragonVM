#!/bin/bash

green='\033[0;32m'
red='\033[0;31m'
clear='\033[0m'

printf "Counting lines of code...\n"

(find ./src/ -type f \( -iname \*.hpp -o -iname \*.cpp \) | xargs -n1 cpp -fpreprocessed -P | awk '!/^[{[:space:]}]*$/' | wc -l) > _lines.tmp 2> /dev/null
typeset -i netto_lines=$(cat _lines.tmp)
rm _lines.tmp
total_lines=$(find ./src/ -type f \( -iname \*.hpp -o -iname \*.cpp \) | xargs wc -l | sort -n | tail -1 | tr -d -c 0-9)
printf "Total: ${red}${total_lines}\n${clear}"
printf "Netto: ${green}${netto_lines}${clear} (No braces, comments or empty lines.)\n"