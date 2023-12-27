# DragonVM

Compile instructions on Windows

**Step 1:**
download MSYS2 from https://www.msys2.org/ and install it

**Step 2:**
run MSYS2, and in the terminal run:
```
pacman -Syuu
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-gdb mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-sfml mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2
```

**Step 3:**
open a UCRT64/MSYS2 command prompt inside the root directory of the project

**Step 4:**
execute this command:
```
./compile
```
