cmake_minimum_required(VERSION 3.5)
project(uffs-fuse-project)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(TestBigEndian)
TEST_BIG_ENDIAN(HOST_IS_BIG_ENDIAN)

if(HOST_IS_BIG_ENDIAN)
    message(STATUS "Host architecture is BIG ENDIAN")
    add_compile_definitions(HOST_BIG_ENDIAN=1)
else()
    message(STATUS "Host architecture is LITTLE ENDIAN")
    add_compile_definitions(HOST_LITTLE_ENDIAN=1)
endif()

option(FLASH_IS_BIG_ENDIAN "Set to ON if the target flash image is Big-Endian" ON)

if(FLASH_IS_BIG_ENDIAN)
    message(STATUS "Target Flash Image is configured as BIG ENDIAN")
    add_compile_definitions(UFFS_BIG_ENDIAN=1)
else()
    message(STATUS "Target Flash Image is configured as LITTLE ENDIAN")
    add_compile_definitions(UFFS_LITTLE_ENDIAN=1)
endif()

# Build mudified UFFS library 
add_subdirectory(vendor/uffs)

# Build FUSE bridge
add_subdirectory(bridge)
