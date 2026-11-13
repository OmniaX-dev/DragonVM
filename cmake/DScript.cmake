# ============================================================================
# The DScript projects (what extra/reload_env.sh does), as build targets that
# only recompile what changed:
#
#   ds_env                  the SDK, the BIOS, Drake and kernel0 (no assembler flags)
#   ds_env_debug            the same with -Wa,-D (dasm's debug mode)
#   ds_env_debug_verbose    -Wa,-D,--verbose-full
#
# e.g. make -C bin ds_env_debug (./build run / dbg / dbgv / gdb use them).
#
# The sources are read from extra/ds (bin/ds is a copy that ./build refreshes,
# with new timestamps, on every build). Each mode compiles into its own folder,
# bin/ds-out/<mode>/, so switching modes doesn't recompile either. A program is
# compiled again when one of its sources, the SDK or dsc changes; the SDK when
# one of its sources or dsc changes.
#
# Every time a target runs (cheap, no compiling), the mode's results go where
# reload_env.sh puts them:
#   - the SDK: bin/ds/dragon-sdk/lib and extra/ds/dragon-sdk/lib
#   - the BIOS: bin/dragon/bios.bin
#   - Drake and kernel0: bin/dragon/drake.bin, bin/dragon/snake_kernel0.bin, loaded
#     into bin/dragon/disk.drd (Drake at 0, kernel0 at 0x00700200 and as
#     /boot/kernel0 on partition 4)
#   - the disassembly: bin/disassembly, with the names reload_env.sh gives them
#     (the md5 of the output path), so the debugger sees one copy of each program
# ============================================================================

option(DS_UPDATE_EXTRA_SDK "ds_env*: copy the built SDK to extra/ds/dragon-sdk/lib too" ON)

set(DS_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../extra/ds)
get_filename_component(DS_SOURCE_DIR ${DS_SOURCE_DIR} ABSOLUTE)
set(DS_RUN_DIR ${CMAKE_BINARY_DIR})

file(GLOB DS_SDK_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-sdk/src/*.ds)
file(GLOB_RECURSE DS_BIOS_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/bios/*.ds)
file(GLOB_RECURSE DS_DRAKE_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/drake/*.ds)
file(GLOB_RECURSE DS_KERNEL0_SOURCES CONFIGURE_DEPENDS ${DS_SOURCE_DIR}/dragon-os/*.ds)

# The disassembly file names reload_env.sh's dsc calls produce.
string(MD5 DS_DRAKE_DDS "dragon/drake.bin")
string(MD5 DS_KERNEL0_DDS "dragon/snake_kernel0.bin")

# ds_add_env(<target> <mode folder> <assembler flags (comma separated, may be empty)>)
function(ds_add_env TARGET MODE ASM_FLAGS)
	set(OUT ${DS_RUN_DIR}/ds-out/${MODE})
	set(DSC $<TARGET_FILE:${COMPILER_TARGET}>)
	set(DTOOLS $<TARGET_FILE:${TOOLS_TARGET}>)
	set(WA "-Wa,${ASM_FLAGS}")
	set(SDK ${OUT}/lib/dragon-sdk.dlib)
	if(DS_UPDATE_EXTRA_SDK)
		set(SDK_EXTRA_COPY ${DS_SOURCE_DIR}/dragon-sdk/lib/dragon-sdk.dlib)
	else()
		set(SDK_EXTRA_COPY ${DS_RUN_DIR}/ds/dragon-sdk/lib/dragon-sdk.dlib)
	endif()

	add_custom_command(
		OUTPUT ${SDK}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT}/lib ${OUT}/disassembly
		COMMAND ${DSC} --lib ${DS_SDK_SOURCES} -o ${SDK} ${WA}
		DEPENDS ${DS_SDK_SOURCES} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): dragon-sdk"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/bios.bin
		COMMAND ${DSC} ${DS_SOURCE_DIR}/bios/entry.ds -L ${OUT}/lib -o ${OUT}/bios.bin --verbose --raw --load 0x0 --fixed 16777216,0xFF --rom ${WA}
		DEPENDS ${DS_BIOS_SOURCES} ${SDK} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/drake.bin ${OUT}/disassembly/${DS_DRAKE_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/drake/entry.ds -L ${OUT}/lib -o ${OUT}/drake.bin --verbose --raw --load 0x01010000 --fixed 4194304,0x00
				--save-disassembly ${OUT}/disassembly/${DS_DRAKE_DDS}.dds ${WA}
		DEPENDS ${DS_DRAKE_SOURCES} ${SDK} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): Drake"
		VERBATIM)

	add_custom_command(
		OUTPUT ${OUT}/snake_kernel0.bin ${OUT}/disassembly/${DS_KERNEL0_DDS}.dds
		COMMAND ${DSC} ${DS_SOURCE_DIR}/dragon-os/kernel0/kernel0.ds -L ${OUT}/lib -o ${OUT}/snake_kernel0.bin --verbose --raw --header KERNEL0_BOOT
				--load 0x01410000 --fixed 1048576,0xFF --save-disassembly ${OUT}/disassembly/${DS_KERNEL0_DDS}.dds ${WA}
		DEPENDS ${DS_KERNEL0_SOURCES} ${SDK} ${COMPILER_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): kernel0"
		VERBATIM)

	# Always: the results where the runtime and the debugger look for them.
	add_custom_target(${TARGET}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${DS_RUN_DIR}/ds/dragon-sdk/lib ${DS_RUN_DIR}/dragon ${DS_RUN_DIR}/disassembly
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${DS_RUN_DIR}/ds/dragon-sdk/lib/dragon-sdk.dlib
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK} ${SDK_EXTRA_COPY}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/bios.bin ${DS_RUN_DIR}/dragon/bios.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/drake.bin ${DS_RUN_DIR}/dragon/drake.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/snake_kernel0.bin ${DS_RUN_DIR}/dragon/snake_kernel0.bin
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}/disassembly/${DS_DRAKE_DDS}.dds ${OUT}/disassembly/${DS_KERNEL0_DDS}.dds ${DS_RUN_DIR}/disassembly/
		COMMAND ${DTOOLS} load-binary dragon/disk.drd dragon/drake.bin 0x00000000
		COMMAND ${DTOOLS} load-binary dragon/disk.drd dragon/snake_kernel0.bin 0x00700200
		COMMAND ${DTOOLS} dfs dragon/disk.drd 4 install-kernel dragon/snake_kernel0.bin
		DEPENDS ${SDK} ${OUT}/bios.bin ${OUT}/drake.bin ${OUT}/snake_kernel0.bin ${TOOLS_TARGET}
		WORKING_DIRECTORY ${DS_RUN_DIR}
		COMMENT "DScript (${MODE}): BIOS, Drake and kernel0 in place"
		VERBATIM)
endfunction()

ds_add_env(ds_env release "")
ds_add_env(ds_env_debug debug "-D")
ds_add_env(ds_env_debug_verbose debug-verbose "-D,--verbose-full")
