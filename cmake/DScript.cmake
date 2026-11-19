# ============================================================================
# The DScript projects, as build targets that
# only recompile what changed:
#
#   ds_env                  the libraries, the BIOS, Drake, the kernels and DragonOS's user
#                           programs (no assembler flags)
#   ds_env_debug            the same with -Wa,-D (dasm's debug mode)
#   ds_env_debug_verbose    -Wa,-D,--verbose-full
#
# e.g. make -C bin ds_env_debug (./build run / dbg / dbgv / gdb use them).
#
# The sources are read from extra/ds (bin/ds is a copy that ./build refreshes,
# with new timestamps, on every build). Each mode compiles into its own folder,
# bin/ds-out/<mode>/, so switching modes doesn't recompile either. A program is
# compiled again when one of its sources, a library it links or dsc changes; a
# library when one of its sources, a library it imports or dsc changes.
#
# The libraries (each in extra/ds/<name>/src, built into <name>.dlib):
#   libcore      freestanding utilities (std::itoa, std::BitField32, std::VaList, the calendar, ...):
#                no system calls, no __assert_fail; everything links it
#   dragon-sdk   the hardware and the BIOS services, for supervisor-mode programs (the BIOS,
#                Drake, the kernels); imports libcore
#   stdlib       the userland runtime of DragonOS programs (system calls, __assert_fail);
#                imports libcore
#
# Every time a target runs (cheap, no compiling), the mode's results go where
# the runtime and the debugger look for them:
#   - the libraries: bin/ds/<name>/lib and extra/ds/<name>/lib
#   - the BIOS: bin/dragon/bios.bin
#   - Drake and kernel0: bin/dragon/drake.bin (a DRX, as /init/boot.img on partition 1),
#     bin/dragon/snake_kernel0.bin (at 0x00401000 and as /boot/kernel0 on partition 3),
#     in bin/dragon/disk.drd
#   - DragonOS's user programs (extra/ds/dragon-os/user_programs, linking stdlib): DRX files in
#     bin/dragon (dinit.drx, wingsh.drx), put on DragonOS's partition (2) as /boot/dinit (the
#     first process the kernel starts) and /bin/wingsh
#   - the disassembly: bin/disassembly, each file named after the md5 of its output path,
#     so the debugger sees one copy of each program
# ============================================================================

option(DS_UPDATE_EXTRA_LIBS "ds_env*: copy the built libraries to extra/ds/<name>/lib too (libcore, dragon-sdk, stdlib)" ON)

set(DS_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../extra/ds)
get_filename_component(DS_SOURCE_DIR ${DS_SOURCE_DIR} ABSOLUTE)
set(DS_RUN_DIR ${CMAKE_BINARY_DIR})

file(GLOB_RECURSE DS_LIBCORE_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/libcore/src/*.ds)
file(GLOB_RECURSE DS_SDK_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-sdk/src/*.ds)
file(GLOB_RECURSE DS_STDLIB_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/stdlib/src/*.ds)
file(GLOB_RECURSE DS_BIOS_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/bios/*.ds)
file(GLOB_RECURSE DS_DRAKE_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/drake/*.ds)
file(GLOB_RECURSE DS_SNAKE_KERNEL0_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/snake-game/*.ds)
file(GLOB_RECURSE DS_DRAGON_OS_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-os/*.ds)
list(FILTER DS_DRAGON_OS_SOURCES EXCLUDE REGEX "/dragon-os/user_programs/")	# (not the kernel's)
file(GLOB_RECURSE DS_DINIT_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-os/user_programs/dinit/*.ds)
file(GLOB_RECURSE DS_WINGSH_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-os/user_programs/wing-shell/*.ds)

# The disassembly file names (the md5 of each output path, as dsc names them).
string(MD5 DS_BIOS_DDS "dragon/bios.bin")
string(MD5 DS_DRAKE_DDS "dragon/drake.bin")
string(MD5 DS_SNAKE_KERNEL0_DDS "dragon/snake_kernel0.bin")
string(MD5 DS_DRAGON_OS_DDS "dragon/dragon-os.bin")
string(MD5 DS_DINIT_DDS "dragon/dinit.drx")
string(MD5 DS_WINGSH_DDS "dragon/wingsh.drx")

# ds_add_env(<target> <mode folder> <assembler flags (comma separated, may be empty)>)
function(ds_add_env TARGET MODE ASM_FLAGS)
	set(OUT ${DS_RUN_DIR}/ds-out/${MODE})
	set(DSC $<TARGET_FILE:${COMPILER_TARGET}>)
	set(DTOOLS $<TARGET_FILE:${TOOLS_TARGET}>)
	set(WA "-Wa,${ASM_FLAGS}")
	set(LIBCORE ${OUT}/lib/libcore.dlib)
	set(SDK ${OUT}/lib/dragon-sdk.dlib)
	set(STDLIB ${OUT}/lib/stdlib.dlib)
	# What a supervisor-mode program (the BIOS, Drake, the kernels) links.
	set(SYSTEM_LIBS ${LIBCORE} ${SDK})
	# What a DragonOS user program links.
	set(USER_LIBS ${LIBCORE} ${STDLIB})
	if(DS_UPDATE_EXTRA_LIBS)
		set(LIBS_EXTRA_DIR ${DS_SOURCE_DIR})
	else()
		set(LIBS_EXTRA_DIR ${DS_RUN_DIR}/ds)
	endif()

	add_custom_command(
		OUTPUT ${LIBCORE}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT}/lib ${OUT}/disassembly
		COMMAND ${DSC} --lib ${DS_LIBCORE_SOURCES} -o ${LIBCORE} -O ${WA}
		DEPENDS ${DS_LIBCORE_SOURCES} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): libcore"
		VERBATIM)

	add_custom_command(
		OUTPUT ${SDK}
		COMMAND ${DSC} --lib ${DS_SDK_SOURCES} -o ${SDK} -L ${OUT}/lib -O ${WA}
		DEPENDS ${DS_SDK_SOURCES} ${LIBCORE} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): dragon-sdk"
		VERBATIM)

	add_custom_command(
		OUTPUT ${STDLIB}
		COMMAND ${DSC} --lib ${DS_STDLIB_SOURCES} -o ${STDLIB} -L ${OUT}/lib -O ${WA}
		DEPENDS ${DS_STDLIB_SOURCES} ${LIBCORE} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): stdlib"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/bios.bin
		COMMAND ${DSC} ${DS_SOURCE_DIR}/bios/entry.ds -L ${OUT}/lib -o ${OUT}/bios.bin
	 																								-O --verbose --save-disassembly ${OUT}/disassembly/${DS_BIOS_DDS}.dds
																									--raw --load 0x0 --fixed 16777216,0xFF --rom ${WA}
		DEPENDS ${DS_BIOS_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/drake.bin ${OUT}/disassembly/${DS_DRAKE_DDS}.dds
		# A DRX: its disassembly is at offset 0, with its id; ddb moves it to where the BIOS loads it.
		COMMAND ${DSC} ${DS_SOURCE_DIR}/drake/entry.ds -L ${OUT}/lib -o ${OUT}/drake.bin
																									 -O --verbose --save-disassembly ${OUT}/disassembly/${DS_DRAKE_DDS}.dds ${WA}
		DEPENDS ${DS_DRAKE_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): Drake"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/snake_kernel0.bin ${OUT}/disassembly/${DS_SNAKE_KERNEL0_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/snake-game/kernel0/kernel0.ds -L ${OUT}/lib -o ${OUT}/snake_kernel0.bin
																																	-O --verbose --save-disassembly ${OUT}/disassembly/${DS_SNAKE_KERNEL0_DDS}.dds ${WA}
		DEPENDS ${DS_SNAKE_KERNEL0_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): SnakeGame"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/dragon-os.bin ${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/dragon-os/main.ds -L ${OUT}/lib -o ${OUT}/dragon-os.bin
																																	  --verbose --save-disassembly ${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds ${WA}
		DEPENDS ${DS_DRAGON_OS_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): DragonOS"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/dinit.drx ${OUT}/disassembly/${DS_DINIT_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/dragon-os/user_programs/dinit/main.ds -L ${OUT}/lib -o ${OUT}/dinit.drx
																																	  			-O --verbose --save-disassembly ${OUT}/disassembly/${DS_DINIT_DDS}.dds ${WA}
		DEPENDS ${DS_DINIT_SOURCES} ${USER_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): DragonOS dinit"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/wingsh.drx ${OUT}/disassembly/${DS_WINGSH_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/dragon-os/user_programs/wing-shell/main.ds -L ${OUT}/lib -o ${OUT}/wingsh.drx
																																	  					 -O --verbose --save-disassembly ${OUT}/disassembly/${DS_WINGSH_DDS}.dds ${WA}
		DEPENDS ${DS_WINGSH_SOURCES} ${USER_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): DragonOS wing-shell"
		VERBATIM)

	# Always: the results where the runtime and the debugger look for them.
	add_custom_target(${TARGET}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${DS_RUN_DIR}/ds/libcore/lib ${DS_RUN_DIR}/ds/dragon-sdk/lib ${DS_RUN_DIR}/ds/stdlib/lib
																									${LIBS_EXTRA_DIR}/libcore/lib ${LIBS_EXTRA_DIR}/dragon-sdk/lib ${LIBS_EXTRA_DIR}/stdlib/lib
																									${DS_RUN_DIR}/dragon ${DS_RUN_DIR}/disassembly
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE} ${DS_RUN_DIR}/ds/libcore/lib/libcore.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${DS_RUN_DIR}/ds/dragon-sdk/lib/dragon-sdk.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STDLIB} ${DS_RUN_DIR}/ds/stdlib/lib/stdlib.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE} ${LIBS_EXTRA_DIR}/libcore/lib/libcore.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${LIBS_EXTRA_DIR}/dragon-sdk/lib/dragon-sdk.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STDLIB} ${LIBS_EXTRA_DIR}/stdlib/lib/stdlib.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/bios.bin ${DS_RUN_DIR}/dragon/bios.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/drake.bin ${DS_RUN_DIR}/dragon/drake.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/snake_kernel0.bin ${DS_RUN_DIR}/dragon/snake_kernel0.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/dragon-os.bin ${DS_RUN_DIR}/dragon/dragon-os.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/dinit.drx ${DS_RUN_DIR}/dragon/dinit.drx
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/wingsh.drx ${DS_RUN_DIR}/dragon/wingsh.drx
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/disassembly/${DS_BIOS_DDS}.dds
																									${OUT}/disassembly/${DS_DRAKE_DDS}.dds
																									${OUT}/disassembly/${DS_SNAKE_KERNEL0_DDS}.dds
																									${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds
																									${OUT}/disassembly/${DS_DINIT_DDS}.dds
																									${OUT}/disassembly/${DS_WINGSH_DDS}.dds
																									${DS_RUN_DIR}/disassembly/
		COMMAND ${DTOOLS} dfs dragon/disk.drd 1 mkdir -p /init
		COMMAND ${DTOOLS} dfs dragon/disk.drd 1 put dragon/drake.bin /init/boot.img
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 install-kernel dragon/dragon-os.bin
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 mkdir -p /boot
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 put dragon/dinit.drx /boot/dinit
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 mkdir -p /bin
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 put dragon/wingsh.drx /bin/wingsh
		COMMAND ${DTOOLS} dfs dragon/disk.drd 3 install-kernel dragon/snake_kernel0.bin
		DEPENDS ${LIBCORE} ${SDK} ${STDLIB} ${OUT}/bios.bin ${OUT}/drake.bin ${OUT}/snake_kernel0.bin ${OUT}/dragon-os.bin
																									${OUT}/dinit.drx ${OUT}/wingsh.drx ${TOOLS_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS, Drake, the kernels and DragonOS's programs in place"
		VERBATIM)
endfunction()

ds_add_env(ds_env release "")
ds_add_env(ds_env_debug debug "-D")
ds_add_env(ds_env_debug_verbose debug-verbose "-D,--verbose-full")
