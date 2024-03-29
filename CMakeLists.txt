#Variables
#-----------------------------------------------------------------------------------------
set(PROJ_NAME DragonVM)
set(MAJOR_VER 0)
set(MINOR_VER 2)
#-----------------------------------------------------------------------------------------

#Setup
#-----------------------------------------------------------------------------------------
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.18)
project(${PROJ_NAME} LANGUAGES C CXX)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
file(STRINGS "./build.nr" BUILD_NUMBER)
#-----------------------------------------------------------------------------------------

message("** Building ${PROJ_NAME} ${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")

#Sources
#-----------------------------------------------------------------------------------------
list(APPEND INCLUDE_DIRS
	${CMAKE_CURRENT_LIST_DIR}/src
)
list(APPEND RUNTIME_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/runtime_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/DragonRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/ConfigLoader.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/gui/Window.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/RawTextRenderer.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/Renderer.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualCPU.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualRAM.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/MemoryMapper.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualIODevices.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualDisplay.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/CPUExtensions.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/GlobalData.cpp
)
list(APPEND DEBUGGER_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/debugger_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/DisassemblyLoader.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/Debugger.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/gui/Window.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/RawTextRenderer.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/Renderer.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualCPU.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualRAM.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/MemoryMapper.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualIODevices.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualDisplay.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/CPUExtensions.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/runtime/DragonRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/ConfigLoader.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/assembler/Assembler.cpp
	
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/GlobalData.cpp
)
list(APPEND ASSEMBLER_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/assembler_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/Assembler.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
)
list(APPEND TOOLS_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/tools/tools_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Tools.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp
)
#-----------------------------------------------------------------------------------------

#Targets
#-----------------------------------------------------------------------------------------
set(RUNTIME_TARGET dvm)
add_executable(${RUNTIME_TARGET} ${RUNTIME_SOURCE_FILES})
target_include_directories(${RUNTIME_TARGET} PUBLIC ${INCLUDE_DIRS})


set(DEBUGGER_TARGET ddb)
add_executable(${DEBUGGER_TARGET} ${DEBUGGER_SOURCE_FILES})
target_include_directories(${DEBUGGER_TARGET} PUBLIC ${INCLUDE_DIRS})


set(ASSEMBLER_TARGET dasm)
add_executable(${ASSEMBLER_TARGET} ${ASSEMBLER_SOURCE_FILES})
target_include_directories(${ASSEMBLER_TARGET} PUBLIC ${INCLUDE_DIRS})


set(TOOLS_TARGET dtools)
add_executable(${TOOLS_TARGET} ${TOOLS_SOURCE_FILES})
target_include_directories(${TOOLS_TARGET} PUBLIC ${INCLUDE_DIRS})

target_compile_definitions(${RUNTIME_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${DEBUGGER_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${ASSEMBLER_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${TOOLS_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")

#TODO: Different flags for Release/Debug
add_compile_options(-O3 -m32 -MMD -MP -Wall -ggdb)
if (UNIX)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath='$ORIGIN'")
	target_link_libraries(${RUNTIME_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${ASSEMBLER_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${TOOLS_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${DEBUGGER_TARGET} xcb xcb-randr boost_regex)
endif (UNIX)
if (WIN32)
	target_link_libraries(${RUNTIME_TARGET} mingw32)
	target_link_libraries(${ASSEMBLER_TARGET} mingw32)
	target_link_libraries(${TOOLS_TARGET} mingw32)
	target_link_libraries(${DEBUGGER_TARGET} mingw32)
endif (WIN32)
target_link_libraries(${RUNTIME_TARGET} SDL2main SDL2)
target_link_libraries(${DEBUGGER_TARGET} SDL2main SDL2)
#-----------------------------------------------------------------------------------------

#Linking 
#-----------------------------------------------------------------------------------------
target_link_libraries(${RUNTIME_TARGET} ostd)
target_link_libraries(${DEBUGGER_TARGET} ostd)
target_link_libraries(${ASSEMBLER_TARGET} ostd)
target_link_libraries(${TOOLS_TARGET} ostd)
#-----------------------------------------------------------------------------------------

add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/extra/ $<TARGET_FILE_DIR:${RUNTIME_TARGET}>
	VERBATIM
)
