#Setup
#-----------------------------------------------------------------------------------------
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.18)
project(${PROJ_NAME} LANGUAGES C CXX)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
file(STRINGS "./tools/build.nr" BUILD_NUMBER)
#-----------------------------------------------------------------------------------------



#Variables
#-----------------------------------------------------------------------------------------
set(PROJ_NAME DragonVM)
set(MAJOR_VER 0)
set(MINOR_VER 1)

message("** Building ${PROJ_NAME} ${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER}")
#-----------------------------------------------------------------------------------------



#Sources
#-----------------------------------------------------------------------------------------
list(APPEND INCLUDE_DIRS
	${CMAKE_CURRENT_LIST_DIR}/src
	${CMAKE_CURRENT_LIST_DIR}/../omnia-framework/src
)
list(APPEND RUNTIME_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/runtime_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/DragonRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/ConfigLoader.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/gui/Window.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/widgets/VirtualConsoleWidget.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/RawTextRenderer.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/VirtualConsoleOutputHandler.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/VirtualConsole.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualCPU.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualRAM.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/MemoryMapper.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualIODevices.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
)
list(APPEND DEBUGGER_SOURCE_FILES
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/debugger_main.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/DisassemblyLoader.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/debugger/Debugger.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/gui/DebuggerWindow.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/Window.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/widgets/VirtualConsoleWidget.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/RawTextRenderer.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/gui/VirtualConsoleOutputHandler.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualCPU.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualRAM.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/MemoryMapper.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualIODevices.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/hardware/VirtualHardDrive.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/runtime/DragonRuntime.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/runtime/ConfigLoader.cpp

	${CMAKE_CURRENT_LIST_DIR}/src/assembler/Assembler.cpp
	
	${CMAKE_CURRENT_LIST_DIR}/src/tools/Utils.cpp
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

target_compile_definitions(${RUNTIME_TARGET} PUBLIC  BUILD_NR=${BUILD_NUMBER} MAJ_V=${MAJOR_VER} MIN_V=${MINOR_VER} VERSION_STR="${MAJOR_VER}.${MINOR_VER}.${BUILD_NUMBER} - Alpha")

#TODO: Different flags for Release/Debug
add_compile_options(-O3 -m32 -MMD -MP -Wall -ggdb)
if (UNIX)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath='$ORIGIN'")
	#target_link_libraries(${RUNTIME_TARGET} X11 GL)
	target_link_libraries(${RUNTIME_TARGET} xcb xcb-randr)
	target_link_libraries(${ASSEMBLER_TARGET} xcb xcb-randr)
	target_link_libraries(${TOOLS_TARGET} xcb xcb-randr)
	target_link_libraries(${DEBUGGER_TARGET} xcb xcb-randr)
endif (UNIX)
if (WIN32)
	target_link_libraries(${RUNTIME_TARGET} mingw32)
	target_link_libraries(${ASSEMBLER_TARGET} mingw32)
	target_link_libraries(${TOOLS_TARGET} mingw32)
	target_link_libraries(${DEBUGGER_TARGET} mingw32)
endif (WIN32)
target_link_libraries(${RUNTIME_TARGET} SDL2main SDL2 SDL2_mixer SDL2_image)
target_link_libraries(${DEBUGGER_TARGET} SDL2main SDL2 SDL2_mixer SDL2_image)

# target_link_libraries(${RUNTIME_TARGET} sfml-system sfml-window sfml-graphics)
#-----------------------------------------------------------------------------------------



#Linking 
#-----------------------------------------------------------------------------------------
find_library(OMNIA_STD_LIB
	NAMES ostd
	HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../omnia-framework/bin/"
	NO_CACHE
)
target_link_libraries(${RUNTIME_TARGET}  ${OMNIA_STD_LIB})
target_link_libraries(${DEBUGGER_TARGET}  ${OMNIA_STD_LIB})
target_link_libraries(${ASSEMBLER_TARGET}  ${OMNIA_STD_LIB})
target_link_libraries(${TOOLS_TARGET}  ${OMNIA_STD_LIB})
#-----------------------------------------------------------------------------------------



#BuildNumber Target
#-----------------------------------------------------------------------------------------
if (WIN32)
	add_custom_command ( OUTPUT ./tools/build.nr
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/inc_bnr.exe
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools/
	)
endif (WIN32)
if (UNIX)
	add_custom_command ( OUTPUT ./tools/build.nr
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/inc_bnr
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools/
	)
endif (UNIX)

add_custom_target(
  IncBnr ALL
  DEPENDS ./tools/build.nr
)
#-----------------------------------------------------------------------------------------


add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/extra/ $<TARGET_FILE_DIR:${RUNTIME_TARGET}>
	VERBATIM
)
