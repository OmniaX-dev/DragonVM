# DragonVM

An amulator for a 16-bit machine that never existed

## Build instructions - All
This project requires the OmniaFramework library I wrote, which can be found at https://github.com/OmniaX-dev/OmniaFramework   
This library must be correctly installed in clang's search paths (include, lib, bin) before compiling this project.

## Build instructions - Windows
**Step 1:**
download MSYS2 from https://www.msys2.org/ and install it

**Step 2:**
run MSYS2, and in the terminal run:
```
pacman -Syuu
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-gdb mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-ucrt-x86_64-SDL2_image
```

**Step 3:**
open a UCRT64/MSYS2 command prompt inside the root directory of the project

**Step 4:**
execute this command:
```
./compile
```
## Build instructions - Linux (Arch)
**Step 1:**
open a terminal and run:
```
sudo pacman -Syuu
sudo pacman -S --needed base-devel clang gdb cmake make sdl2 sdl2_mixer sdl2_image
```

**Step 2:**
open a terminal inside the root directory of the project and run this command:
```
./compile
```
## Build instructions - Linux (Ubuntu)
**Step 1:**
open a terminal and run:
```
sudo apt update && sudo apt upgrade
sudo apt install clang gdb make cmake libsdl2-dev libsdl2-mixer-dev libsdl2-image-dev libxcb-randr0-dev
```

**Step 2:**
open a terminal inside the root directory of the project and run this command:
```
./compile
```

#### For other Linux distros, install the dependencies using your package manager.