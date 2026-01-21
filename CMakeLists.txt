#Variables
#-----------------------------------------------------------------------------------------
set(PROJ_NAME DragonVM)
set(MAJOR_VER 0)
set(MINOR_VER 4)
#-----------------------------------------------------------------------------------------

#Setup
#-----------------------------------------------------------------------------------------
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.18)
project(${PROJ_NAME} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
file(STRINGS "./other/build.nr" BUILD_NUMBER)
#-----------------------------------------------------------------------------------------

message("** Building ${PROJ_NAME} ${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(STATUS "Debug Configuration")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
	message(STATUS "Release Configuration")
endif()

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
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualMMU.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/GlobalData.cpp
)
list(APPEND DEBUGGER_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/debugger_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/DisassemblyLoader.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/Debugger.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/DebuggerNew.cpp

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
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualMMU.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/runtime/DragonRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/ConfigLoader.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/assembler/Assembler.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/IncludePreprocessor.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/DASMApp.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/GlobalData.cpp
)
list(APPEND ASSEMBLER_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/assembler_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/Assembler.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/IncludePreprocessor.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/assembler/DASMApp.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
)
list(APPEND TOOLS_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/tools/tools_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Tools.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/debugger/DisassemblyLoader.cpp
)
#-----------------------------------------------------------------------------------------

#Targets
#-----------------------------------------------------------------------------------------
set(RUNTIME_TARGET dvm)
add_executable(${RUNTIME_TARGET} ${RUNTIME_SOURCE_FILES})
if(WIN32)
	target_sources(${MAIN_EXECUTABLE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/other/dvm_appIcon.rc")
endif()
target_include_directories(${RUNTIME_TARGET} PUBLIC ${INCLUDE_DIRS})


set(DEBUGGER_TARGET ddb)
add_executable(${DEBUGGER_TARGET} ${DEBUGGER_SOURCE_FILES})
if(WIN32)
	target_sources(${MAIN_EXECUTABLE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/other/ddb_appIcon.rc")
endif()
target_include_directories(${DEBUGGER_TARGET} PUBLIC ${INCLUDE_DIRS})


set(ASSEMBLER_TARGET dasm)
add_executable(${ASSEMBLER_TARGET} ${ASSEMBLER_SOURCE_FILES})
if(WIN32)
	target_sources(${MAIN_EXECUTABLE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/other/dasm_appIcon.rc")
endif()
target_include_directories(${ASSEMBLER_TARGET} PUBLIC ${INCLUDE_DIRS})


set(TOOLS_TARGET dtools)
add_executable(${TOOLS_TARGET} ${TOOLS_SOURCE_FILES})
if(WIN32)
	target_sources(${MAIN_EXECUTABLE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/other/dtools_appIcon.rc")
endif()
target_include_directories(${TOOLS_TARGET} PUBLIC ${INCLUDE_DIRS})

target_compile_definitions(${RUNTIME_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${DEBUGGER_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${ASSEMBLER_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
target_compile_definitions(${TOOLS_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")


if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_definitions(${RUNTIME_TARGET} PUBLIC BUILD_CONFIG_DEBUG)
	target_compile_definitions(${DEBUGGER_TARGET} PUBLIC BUILD_CONFIG_DEBUG)
	target_compile_definitions(${ASSEMBLER_TARGET} PUBLIC BUILD_CONFIG_DEBUG)
	target_compile_definitions(${TOOLS_TARGET} PUBLIC BUILD_CONFIG_DEBUG)
	add_compile_options(-O3 -MMD -MP -Wall -ggdb)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
	target_compile_definitions(${RUNTIME_TARGET} PUBLIC BUILD_CONFIG_RELEASE)
	target_compile_definitions(${DEBUGGER_TARGET} PUBLIC BUILD_CONFIG_RELEASE)
	target_compile_definitions(${ASSEMBLER_TARGET} PUBLIC BUILD_CONFIG_RELEASE)
	target_compile_definitions(${TOOLS_TARGET} PUBLIC BUILD_CONFIG_RELEASE)
	add_compile_options(-MMD -MP -Wall)
endif()

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
	set_target_properties(${RUNTIME_TARGET} PROPERTIES
								WIN32_EXECUTABLE TRUE
				)
	set_target_properties(${DEBUGGER_TARGET} PROPERTIES
								WIN32_EXECUTABLE TRUE
				)
	set_target_properties(${ASSEMBLER_TARGET} PROPERTIES
								WIN32_EXECUTABLE TRUE
				)
	set_target_properties(${TOOLS_TARGET} PROPERTIES
								WIN32_EXECUTABLE TRUE
				)
endif()
#-----------------------------------------------------------------------------------------

#Linking
#-----------------------------------------------------------------------------------------
if (WIN32)
	target_link_libraries(${RUNTIME_TARGET} mingw32)
	target_link_libraries(${ASSEMBLER_TARGET} mingw32)
	target_link_libraries(${TOOLS_TARGET} mingw32)
	target_link_libraries(${DEBUGGER_TARGET} mingw32)
endif (WIN32)

if (UNIX)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,'$ORIGIN',-rpath,/usr/lib"					)
	target_link_libraries(${RUNTIME_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${ASSEMBLER_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${TOOLS_TARGET} xcb xcb-randr boost_regex)
	target_link_libraries(${DEBUGGER_TARGET} xcb xcb-randr boost_regex)
endif (UNIX)

target_link_libraries(${RUNTIME_TARGET} SDL2main SDL2 SDL2_image)
target_link_libraries(${DEBUGGER_TARGET} SDL2main SDL2 SDL2_image)

target_link_libraries(${RUNTIME_TARGET} ostd)
target_link_libraries(${DEBUGGER_TARGET} ostd ogfx)
target_link_libraries(${ASSEMBLER_TARGET} ostd)
target_link_libraries(${TOOLS_TARGET} ostd)
#-----------------------------------------------------------------------------------------
