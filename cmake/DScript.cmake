# ============================================================================
# The DScript projects, as build targets that
# only recompile what changed:
#
#   ds_env                  the libraries, the BIOS, Drake, the kernel and DragonOS's user
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
# extra/ds is organized as:
#   machine/        what runs on the bare machine: bios, drake (the boot loader), dragon-sdk
#   libs/           the libraries DragonOS programs use: libcore, libstd
#   dragon-os/      the kernel
#   user_programs/  DragonOS's programs (one folder each, see below)
#
# The libraries (each in extra/ds/<group>/<name>/src, built into <name>.dlib):
#   libcore      freestanding utilities (std::itoa, std::BitField32, std::VaList, the calendar, ...):
#                no system calls, no __assert_fail; everything links it
#   dragon-sdk   the hardware and the BIOS services, for supervisor-mode programs (the BIOS,
#                Drake, the kernels); imports libcore
#   libstd       the userland runtime of DragonOS programs (system calls, __assert_fail);
#                imports libcore
#
# Every time a target runs (cheap, no compiling), the mode's results go where
# the runtime and the debugger look for them:
#   - the libraries: bin/ds/<group>/<name>/lib and extra/ds/<group>/<name>/lib
#   - the BIOS: bin/dragon/bios.bin
#   - Drake: bin/dragon/drake.bin (a DRX, as /init/boot.img on partition 1 of bin/dragon/disk.drd)
#   - DragonOS: bin/dragon/dragon-os.bin (installed as the kernel of partition 2)
#   - the shared libraries of DragonOS programs (dsc --lib --shared: libcore.drl, libstd.drl, see
#     other/info/dynamic-linking.md): next to the .dlib files, and on DragonOS's partition (2) as
#     /lib/<name>.drl; their disassembly in bin/disassembly (ddb places it where they are loaded)
#   - DragonOS's user programs (linking libstd): every folder of extra/ds/user_programs with a
#     main.ds is one, named after the folder. Each is compiled to bin/dragon/<folder>.drx and put on
#     DragonOS's partition (2) as /bin/<folder>, except dinit, which goes to /boot/dinit (the first
#     process the kernel starts). A new program is a new folder: the glob below is checked on every
#     build (CONFIGURE_DEPENDS), so cmake picks it up by itself. (Removing a folder doesn't remove
#     the program from the disk image.)
#   - the disassembly: bin/disassembly, each file named after the md5 of its output path,
#     so the debugger sees one copy of each program
# ============================================================================

option(DS_UPDATE_EXTRA_LIBS "ds_env*: copy the built libraries to extra/ds/<group>/<name>/lib too (libcore, dragon-sdk, libstd)" ON)

set(DS_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../extra/ds)
get_filename_component(DS_SOURCE_DIR ${DS_SOURCE_DIR} ABSOLUTE)
set(DS_RUN_DIR ${CMAKE_BINARY_DIR})

file(GLOB_RECURSE DS_LIBCORE_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/libs/libcore/src/*.ds)
file(GLOB_RECURSE DS_SDK_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/machine/dragon-sdk/src/*.ds)
file(GLOB_RECURSE DS_LIBSTD_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/libs/libstd/src/*.ds)
file(GLOB_RECURSE DS_BIOS_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/machine/bios/*.ds)
file(GLOB_RECURSE DS_DRAKE_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/machine/drake/*.ds)
file(GLOB_RECURSE DS_DRAGON_OS_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-os/*.ds)

# The user programs: the folders of user_programs with a main.ds (DS_USER_PROGRAMS, their names),
# and each one's sources (DS_USER_PROGRAM_SOURCES_<name>: every .ds in its folder, for the rebuilds).
file(GLOB DS_USER_PROGRAM_MAINS CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/user_programs/*/main.ds)
set(DS_USER_PROGRAMS "")
foreach(MAIN ${DS_USER_PROGRAM_MAINS})
	get_filename_component(DIR ${MAIN} DIRECTORY)
	get_filename_component(NAME ${DIR} NAME)
	list(APPEND DS_USER_PROGRAMS ${NAME})
	file(GLOB_RECURSE DS_USER_PROGRAM_SOURCES_${NAME} CONFIGURE_DEPENDS ${DIR}/*.ds)
endforeach()
list(SORT DS_USER_PROGRAMS)
if(NOT "dinit" IN_LIST DS_USER_PROGRAMS)
	message(WARNING "DScript: no user_programs/dinit/main.ds: DragonOS has no first process to start")
endif()

# The disassembly file names (the md5 of each output path, as dsc names them).
string(MD5 DS_BIOS_DDS "dragon/bios.bin")
string(MD5 DS_DRAKE_DDS "dragon/drake.bin")
string(MD5 DS_DRAGON_OS_DDS "dragon/dragon-os.bin")
string(MD5 DS_LIBCORE_DRL_DDS "lib/libcore.drl")
string(MD5 DS_LIBSTD_DRL_DDS "lib/libstd.drl")

# ds_add_env(<target> <mode folder> <assembler flags (comma separated, may be empty)>)
function(ds_add_env TARGET MODE ASM_FLAGS)
	set(OUT ${DS_RUN_DIR}/ds-out/${MODE})
	set(DSC $<TARGET_FILE:${COMPILER_TARGET}>)
	set(DTOOLS $<TARGET_FILE:${TOOLS_TARGET}>)
	set(WA "-Wa,${ASM_FLAGS}")
	set(LIBCORE ${OUT}/lib/libcore.dlib)
	set(SDK ${OUT}/lib/dragon-sdk.dlib)
	set(LIBSTD ${OUT}/lib/libstd.dlib)
	set(LIBCORE_DRL ${OUT}/lib/libcore.drl)
	set(LIBSTD_DRL ${OUT}/lib/libstd.drl)
	# What a supervisor-mode program (the BIOS, Drake, the kernels) links.
	set(SYSTEM_LIBS ${LIBCORE} ${SDK})
	# What a DragonOS user program links.
	set(USER_LIBS ${LIBCORE} ${LIBSTD})
	if(DS_UPDATE_EXTRA_LIBS)
		set(LIBS_EXTRA_DIR ${DS_SOURCE_DIR})
	else()
		set(LIBS_EXTRA_DIR ${DS_RUN_DIR}/ds)
	endif()

	# libcore and libstd (what DragonOS programs use) are also shared libraries: --shared writes
	# both the .dlib and the .drl (other/info/dynamic-linking.md).
	add_custom_command(
		OUTPUT ${LIBCORE} ${LIBCORE_DRL} ${OUT}/disassembly/${DS_LIBCORE_DRL_DDS}.dds
		COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT}/lib ${OUT}/disassembly
		COMMAND ${DSC} --lib --shared ${DS_LIBCORE_SOURCES} -o ${LIBCORE} -O --save-disassembly ${OUT}/disassembly/${DS_LIBCORE_DRL_DDS}.dds ${WA}
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
		OUTPUT ${LIBSTD} ${LIBSTD_DRL} ${OUT}/disassembly/${DS_LIBSTD_DRL_DDS}.dds
		COMMAND ${DSC} --lib --shared ${DS_LIBSTD_SOURCES} -o ${LIBSTD} -L ${OUT}/lib -O --save-disassembly ${OUT}/disassembly/${DS_LIBSTD_DRL_DDS}.dds ${WA}
		DEPENDS ${DS_LIBSTD_SOURCES} ${LIBCORE} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): libstd"
		VERBATIM)


	add_custom_command(
		OUTPUT ${OUT}/bios.bin
		COMMAND ${DSC} ${DS_SOURCE_DIR}/machine/bios/entry.ds -L ${OUT}/lib -o ${OUT}/bios.bin
	 																								-O --verbose --save-disassembly ${OUT}/disassembly/${DS_BIOS_DDS}.dds
																									--raw --load 0x0 --fixed 16777216,0xFF --rom ${WA}
		DEPENDS ${DS_BIOS_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/drake.bin ${OUT}/disassembly/${DS_DRAKE_DDS}.dds
		# A DRX: its disassembly is at offset 0, with its id; ddb moves it to where the BIOS loads it.
		COMMAND ${DSC} ${DS_SOURCE_DIR}/machine/drake/entry.ds -L ${OUT}/lib -o ${OUT}/drake.bin
																									 -O --verbose --save-disassembly ${OUT}/disassembly/${DS_DRAKE_DDS}.dds ${WA}
		DEPENDS ${DS_DRAKE_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): Drake"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/dragon-os.bin ${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/dragon-os/main.ds -L ${OUT}/lib -o ${OUT}/dragon-os.bin
																																	  --verbose --save-disassembly ${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds ${WA}
		DEPENDS ${DS_DRAGON_OS_SOURCES} ${SYSTEM_LIBS} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): DragonOS"
		VERBATIM)

	# The user programs: one command each (compiled when its sources, a library it links or dsc
	# change), and the steps that put it in place (USER_PROGRAM_STEPS, run by the target below:
	# bin/dragon/<name>.drx, its disassembly, and the disk image).
	set(USER_PROGRAM_DRXS "")
	set(USER_PROGRAM_STEPS "")
	foreach(NAME ${DS_USER_PROGRAMS})
		set(DRX ${OUT}/programs/${NAME}.drx)
		string(MD5 DDS "dragon/${NAME}.drx")				# (the disassembly's name: the md5 of where it ends up)
		if(NAME STREQUAL "dinit")
			set(DISK_PATH /boot/dinit)
		else()
			set(DISK_PATH /bin/${NAME})
		endif()
		add_custom_command(
			OUTPUT ${DRX} ${OUT}/disassembly/${DDS}.dds
			COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT}/programs ${OUT}/disassembly
			COMMAND ${DSC} ${DS_SOURCE_DIR}/user_programs/${NAME}/main.ds -L ${OUT}/lib -o ${DRX}
																									-O --verbose --save-disassembly ${OUT}/disassembly/${DDS}.dds ${WA}
			DEPENDS ${DS_USER_PROGRAM_SOURCES_${NAME}} ${USER_LIBS} ${COMPILER_TARGET}
			WORKING_DIRECTORY ${DS_RUN_DIR}
			COMMENT "DScript (${MODE}): DragonOS program ${NAME} (${DISK_PATH})"
			VERBATIM)
		list(APPEND USER_PROGRAM_DRXS ${DRX})
		list(APPEND USER_PROGRAM_STEPS
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DRX} ${DS_RUN_DIR}/dragon/${NAME}.drx
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/disassembly/${DDS}.dds ${DS_RUN_DIR}/disassembly/
			COMMAND ${DTOOLS} dfs dragon/disk.drd 2 put dragon/${NAME}.drx ${DISK_PATH})
	endforeach()

	# Always: the results where the runtime and the debugger look for them.
	add_custom_target(${TARGET}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${DS_RUN_DIR}/ds/libs/libcore/lib ${DS_RUN_DIR}/ds/machine/dragon-sdk/lib ${DS_RUN_DIR}/ds/libs/libstd/lib
																									${LIBS_EXTRA_DIR}/libs/libcore/lib ${LIBS_EXTRA_DIR}/machine/dragon-sdk/lib ${LIBS_EXTRA_DIR}/libs/libstd/lib
																									${DS_RUN_DIR}/dragon ${DS_RUN_DIR}/disassembly
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE} ${DS_RUN_DIR}/ds/libs/libcore/lib/libcore.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${DS_RUN_DIR}/ds/machine/dragon-sdk/lib/dragon-sdk.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBSTD} ${DS_RUN_DIR}/ds/libs/libstd/lib/libstd.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE} ${LIBS_EXTRA_DIR}/libs/libcore/lib/libcore.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${LIBS_EXTRA_DIR}/machine/dragon-sdk/lib/dragon-sdk.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBSTD} ${LIBS_EXTRA_DIR}/libs/libstd/lib/libstd.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE_DRL} ${DS_RUN_DIR}/ds/libs/libcore/lib/libcore.drl
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBSTD_DRL} ${DS_RUN_DIR}/ds/libs/libstd/lib/libstd.drl
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBCORE_DRL} ${LIBS_EXTRA_DIR}/libs/libcore/lib/libcore.drl
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBSTD_DRL} ${LIBS_EXTRA_DIR}/libs/libstd/lib/libstd.drl
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/bios.bin ${DS_RUN_DIR}/dragon/bios.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/drake.bin ${DS_RUN_DIR}/dragon/drake.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/dragon-os.bin ${DS_RUN_DIR}/dragon/dragon-os.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/disassembly/${DS_BIOS_DDS}.dds
																									${OUT}/disassembly/${DS_DRAKE_DDS}.dds
																									${OUT}/disassembly/${DS_DRAGON_OS_DDS}.dds
																									${OUT}/disassembly/${DS_LIBCORE_DRL_DDS}.dds
																									${OUT}/disassembly/${DS_LIBSTD_DRL_DDS}.dds
																									${DS_RUN_DIR}/disassembly/
		COMMAND ${DTOOLS} dfs dragon/disk.drd 1 mkdir -p /init
		COMMAND ${DTOOLS} dfs dragon/disk.drd 1 put dragon/drake.bin /init/boot.img
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 install-kernel dragon/dragon-os.bin
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 mkdir -p /boot
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 mkdir -p /lib
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 put ${LIBCORE_DRL} /lib/libcore.drl
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 put ${LIBSTD_DRL} /lib/libstd.drl
		COMMAND ${DTOOLS} dfs dragon/disk.drd 2 mkdir -p /bin
		${USER_PROGRAM_STEPS}
		DEPENDS ${LIBCORE} ${SDK} ${LIBSTD} ${LIBCORE_DRL} ${LIBSTD_DRL} ${OUT}/bios.bin ${OUT}/drake.bin ${OUT}/dragon-os.bin
																									${USER_PROGRAM_DRXS} ${TOOLS_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS, Drake, DragonOS and its programs in place"
		VERBATIM)
endfunction()

ds_add_env(ds_env release "")
ds_add_env(ds_env_debug debug "-D")
ds_add_env(ds_env_debug_verbose debug-verbose "-D,--verbose-full")
