# Toolchain: NXP S32 Design Studio GCC for S32K344 (Cortex-M7, bare metal).
#
# Point S32DS_ROOT at the S32 Design Studio installation and pick the GCC
# version used by the project (README/.cproject say 10.2). The compiler flags
# themselves are not set here: they are taken from the S32DS generated
# *.args files by CMakeLists.txt so the CMake build matches the IDE build.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(S32DS_ROOT "D:/NXP/S32DS" CACHE PATH "S32 Design Studio installation directory")
set(S32DS_GCC_VERSION "10.2" CACHE STRING "S32DS ARM GCC version (9.2 / 10.2 / 11.4)")

set(S32DS_GCC_DIR
    "${S32DS_ROOT}/build_tools/gcc_v${S32DS_GCC_VERSION}/gcc-${S32DS_GCC_VERSION}-arm32-eabi"
    CACHE PATH "S32DS ARM GCC toolchain directory")

if(NOT EXISTS "${S32DS_GCC_DIR}/bin/arm-none-eabi-gcc.exe")
  message(FATAL_ERROR
    "S32DS GCC not found at '${S32DS_GCC_DIR}'.\n"
    "Set S32DS_ROOT / S32DS_GCC_VERSION (or S32DS_GCC_DIR) to the right location.")
endif()

set(CMAKE_C_COMPILER   "${S32DS_GCC_DIR}/bin/arm-none-eabi-gcc.exe" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${S32DS_GCC_DIR}/bin/arm-none-eabi-g++.exe" CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${S32DS_GCC_DIR}/bin/arm-none-eabi-gcc.exe" CACHE FILEPATH "")

# Bare-metal: the compiler test must not try to link a hosted program.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The firmware build uses the exact flag set from the S32DS project (-O3 -g3
# ...); keep CMake from adding its own -g / -O3 / -DNDEBUG on top.
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_C_FLAGS_${_cfg}   "" CACHE STRING "" FORCE)
  set(CMAKE_ASM_FLAGS_${_cfg} "" CACHE STRING "" FORCE)
endforeach()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
