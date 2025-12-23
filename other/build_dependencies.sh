#!/bin/bash

detect_package_manager() {
  if [[ -f /etc/os-release ]]; then
    source /etc/os-release
    case "$ID" in
      arch|manjaro|endeavouros|garuda)
        echo "pacman"
        ;;
      ubuntu|debian|linuxmint)
        echo "apt"
        ;;
      fedora)
        echo "dnf"
        ;;
      *)
        echo "unsupported"
        ;;
    esac
  else
    echo "unsupported"
  fi
}

install_manual_dependencies_linux() {
    # Build OmniaFramework
	git clone https://github.com/OmniaX-dev/OmniaFramework.git
    cd OmniaFramework
    ./build release
    ./build install
    cd ../..
}

set -e
mkdir ../dependencies && cd ../dependencies

if [ "$(expr substr $(uname -s) 1 10)" == "MINGW64_NT" ]; then
    # Setup environment
    pacman -Syuu --noconfirm
    pacman -S --noconfirm --needed base-devel mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-gdb \
    							   mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-boost \
              					   mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_mixer \
                      			   mingw-w64-ucrt-x86_64-SDL2_image mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_gfx \

    # Build OmniaFramework
    pacman -S --noconfirm --needed base-devel mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-gdb \
    							   mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make \
              					   mingw-w64-ucrt-x86_64-boost mingw-w64-ucrt-x86_64-SDL2 \
                      			   mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-ucrt-x86_64-SDL2_image \
                            	   mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_gfx


    git clone https://github.com/OmniaX-dev/OmniaFramework.git
    cd OmniaFramework
    ./build release
    ./build install
    cd ../..
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
	pkgmgr=$(detect_package_manager)
	case "$pkgmgr" in
	  pacman) # Arch Based ==================================================================================================
	    sudo pacman -Syu --noconfirm
	    sudo pacman -S --noconfirm --needed base-devel clang openssl gdb cmake make boost sdl2 sdl2_mixer sdl2_image sdl2_ttf sdl2_gfx
	    ;;
	  apt) # Debian Based ==================================================================================================
	    sudo apt update
	    sudo apt install -y build-essential dkms linux-headers-$(uname -r) clang gdb make cmake libssl-dev libboost-all-dev libsdl2-dev \
						    libsdl2-mixer-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-gfx-dev libxcb-randr0-dev
	    ;;
	  dnf) # Fedora ==================================================================================================
	    sudo dnf install -y gcc gcc-c++ make clang gdb cmake clang-tools-extra boost boost-devel openssl openssl-devel SDL2 SDL2_image \
						    SDL2_mixer SDL2_ttf SDL2_gfx SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel SDL2_gfx-devel
	    ;;
	  *)
	    echo "Unsupported distro. Supported distros are: Arch, EndeavourOS, Garuda, Manjaro, Ubuntu, Mint, Debian, Fedora."
	    exit 1
	    ;;
	esac
	install_manual_dependencies_linux
fi
