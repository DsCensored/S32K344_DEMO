# CMake build for the S32K344 application firmware, driven by the NXP S32
# Design Studio toolchain (arm-none-eabi-gcc).
#
# This does not re-declare the project settings: the source list and the
# compiler flags are read from the files S32DS already generated in
# Debug_FLASH (subdir.mk / *.args). That keeps the CMake build byte-for-byte
# close to the IDE build, so the same S32K344.elf can be flashed.
#
# Requires the "S32DS ARM GCC" toolchain file, see cmake/toolchain-s32ds-arm.cmake
# and CMakePresets.json. Open this folder in Visual Studio and pick a
# "S32DS GCC" preset, or from a shell:
#   cmake --preset s32ds-gcc && cmake --build --preset s32ds-gcc
cmake_minimum_required(VERSION 3.20)

project(S32K344 LANGUAGES C ASM)

if(NOT DEFINED S32DS_GCC_DIR)
  message(FATAL_ERROR
    "No S32DS toolchain configured. Use the CMake preset 's32ds-gcc' "
    "(CMakePresets.json) or pass -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-s32ds-arm.cmake.")
endif()

set(S32DS_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Debug_FLASH"
    CACHE PATH "S32DS build directory that holds subdir.mk / *.args")
set(S32DS_FLAGS_FILE "${S32DS_BUILD_DIR}/src/App_Info.args"
    CACHE FILEPATH "S32DS *.args file the compile flags are read from")
set(S32DS_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/Project_Settings/Linker_Files/S32K344_flash.ld"
    CACHE FILEPATH "Linker script")
set(S32DS_ELF2S19 "${CMAKE_CURRENT_SOURCE_DIR}/ELF2s19.exe"
    CACHE FILEPATH "S32DS ELF2s19 converter (optional)")

if(NOT EXISTS "${S32DS_FLAGS_FILE}")
  message(FATAL_ERROR "S32DS compile flags not found: ${S32DS_FLAGS_FILE}")
endif()

# ---------------------------------------------------------------------------
# Sources: every C/asm file listed in the generated subdir.mk files.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE S32DS_SUBDIR_MK "${S32DS_BUILD_DIR}/*/subdir.mk")
if(NOT S32DS_SUBDIR_MK)
  message(FATAL_ERROR "No subdir.mk found under ${S32DS_BUILD_DIR}")
endif()
# Re-run CMake automatically when S32DS regenerates its makefiles/flags
# (e.g. after adding a source file or changing compiler options in the IDE).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${S32DS_SUBDIR_MK} "${S32DS_FLAGS_FILE}")

set(FW_SOURCES "")
foreach(_mk IN LISTS S32DS_SUBDIR_MK)
  file(READ "${_mk}" _mk_text)
  # Match the C_SRCS / S_SRCS entries ("../dir/file.c"); skip the
  # pattern rules ("../dir/%.c") from the compile recipes.
  string(REGEX MATCHALL "\\.\\./[^ \t\r\n\\\\%]+\\.(c|s|S)" _mk_srcs "${_mk_text}")
  foreach(_rel IN LISTS _mk_srcs)
    get_filename_component(_abs "${S32DS_BUILD_DIR}/${_rel}" ABSOLUTE)
    list(APPEND FW_SOURCES "${_abs}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES FW_SOURCES)
list(SORT FW_SOURCES)
list(LENGTH FW_SOURCES FW_SOURCE_COUNT)
if(FW_SOURCE_COUNT EQUAL 0)
  message(FATAL_ERROR "No sources parsed from ${S32DS_BUILD_DIR}")
endif()
message(STATUS "S32K344: ${FW_SOURCE_COUNT} sources from ${S32DS_BUILD_DIR}")

# ---------------------------------------------------------------------------
# Flags: includes / defines come from the same *.args file S32DS uses.
# ---------------------------------------------------------------------------
file(STRINGS "${S32DS_FLAGS_FILE}" _s32ds_args)
set(FW_INCLUDES "")
set(FW_DEFINES "")
foreach(_arg IN LISTS _s32ds_args)
  if(_arg MATCHES "^-I\"?([^\"]+)\"?$")
    set(_inc "${CMAKE_MATCH_1}")
    file(TO_CMAKE_PATH "${_inc}" _inc)
    string(FIND "${_inc}" "${CMAKE_CURRENT_SOURCE_DIR}/" _pos)
    if(_pos EQUAL 0)
      string(LENGTH "${CMAKE_CURRENT_SOURCE_DIR}/" _prefix_len)
      string(SUBSTRING "${_inc}" ${_prefix_len} -1 _inc)
    endif()
    list(APPEND FW_INCLUDES "${_inc}")
  elseif(_arg MATCHES "^-D(.+)$")
    list(APPEND FW_DEFINES "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FW_INCLUDES)
list(REMOVE_DUPLICATES FW_DEFINES)

# Compile flags, taken verbatim from the S32DS *.args (-c/-I/-D handled by
# CMake itself).
set(FW_COMPILE_FLAGS "")
foreach(_arg IN LISTS _s32ds_args)
  if(_arg MATCHES "^-I" OR _arg MATCHES "^-D" OR _arg STREQUAL "-c")
    # handled above / by CMake
  elseif(NOT _arg STREQUAL "")
    list(APPEND FW_COMPILE_FLAGS "${_arg}")
  endif()
endforeach()

add_executable(S32K344 ${FW_SOURCES})

set_target_properties(S32K344 PROPERTIES
  OUTPUT_NAME "S32K344"
  SUFFIX ".elf"
  LINK_DEPENDS "${S32DS_LINKER_SCRIPT}")

target_include_directories(S32K344 PRIVATE ${FW_INCLUDES})
target_compile_definitions(S32K344 PRIVATE ${FW_DEFINES})
target_compile_options(S32K344 PRIVATE ${FW_COMPILE_FLAGS})

# The S32DS startup files are preprocessed assembly (compiled with
# "-x assembler-with-cpp" in the IDE build).
set(FW_ASM_SOURCES "")
foreach(_src IN LISTS FW_SOURCES)
  if(_src MATCHES "\\.[sS]$")
    list(APPEND FW_ASM_SOURCES "${_src}")
  endif()
endforeach()
if(FW_ASM_SOURCES)
  set_source_files_properties(${FW_ASM_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp")
endif()

# ---------------------------------------------------------------------------
# Link: same options as Debug_FLASH/S32K344.args (minus the object list).
# ---------------------------------------------------------------------------
target_link_options(S32K344 PRIVATE
  -nostartfiles
  "-T${S32DS_LINKER_SCRIPT}"
  "-Wl,-Map=${CMAKE_BINARY_DIR}/S32K344.map"
  -Xlinker
  --gc-sections
  -n
  -mcpu=cortex-m7
  -specs=rdimon.specs)

# ---------------------------------------------------------------------------
# Post build: S32DS converts the ELF to a Motorola S-record with ELF2s19.exe.
# ---------------------------------------------------------------------------
if(EXISTS "${S32DS_ELF2S19}")
  # ELF2s19.exe calls "..\powerpc-eabivle-objcopy.exe" relative to its working
  # directory, so it has to run from the S32DS build folder (as it does in the
  # IDE build).
  add_custom_command(TARGET S32K344 POST_BUILD
    COMMAND "${S32DS_ELF2S19}" "$<TARGET_FILE:S32K344>" "${CMAKE_BINARY_DIR}/S32K344.s19"
    WORKING_DIRECTORY "${S32DS_BUILD_DIR}"
    COMMENT "Generating S32K344.s19")
endif()

add_custom_target(size
  COMMAND "${S32DS_GCC_DIR}/bin/arm-none-eabi-size.exe" --format=berkeley "$<TARGET_FILE:S32K344>"
  DEPENDS S32K344
  COMMENT "Firmware size"
  VERBATIM)
