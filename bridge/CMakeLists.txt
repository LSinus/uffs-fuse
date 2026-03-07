# Find FUSE dynamically 
find_package(PkgConfig QUIET)
pkg_check_modules(FUSE QUIET fuse)

if(NOT FUSE_FOUND)
    message(STATUS "pkg-config didn't find FUSE, searching manual macOS paths...")
    
    find_path(FUSE_INCLUDE_DIRS fuse.h 
        PATHS /usr/local/include/fuse /usr/local/include /opt/homebrew/include/fuse
    )
    
    find_library(FUSE_LIBRARIES fuse 
        PATHS /usr/local/lib /opt/homebrew/lib
    )

    if(FUSE_INCLUDE_DIRS AND FUSE_LIBRARIES)
        set(FUSE_FOUND TRUE)
        message(STATUS "Manually found FUSE include: ${FUSE_INCLUDE_DIRS}")
        message(STATUS "Manually found FUSE library: ${FUSE_LIBRARIES}")
    else()
        message(FATAL_ERROR "Could not find FUSE! Please install macFUSE.")
    endif()
endif()

add_executable(uffs-fuse uffs-fuse.c)

target_include_directories(uffs-fuse PRIVATE 
    ${CMAKE_SOURCE_DIR}/vendor/uffs/src/inc/uffs
    ${CMAKE_SOURCE_DIR}/vendor/uffs/src/platform/posix
    ${CMAKE_SOURCE_DIR}/vendor/uffs/src/inc
    ${CMAKE_SOURCE_DIR}/vendor/uffs/src
    ${FUSE_INCLUDE_DIRS}
)


target_link_libraries(uffs-fuse PRIVATE 
    uffs_shared 
    ${FUSE_LIBRARIES}
)

if (APPLE)
    target_compile_definitions(uffs-fuse PRIVATE OS_APPLE _FILE_OFFSET_BITS=64)
else()
    target_compile_definitions(uffs-fuse PRIVATE _FILE_OFFSET_BITS=64)
endif()
